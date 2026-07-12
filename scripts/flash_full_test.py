#!/usr/bin/env python3
"""Flash Prism and launch its hidden full-test cartridge over USB."""

from __future__ import annotations

import argparse
import ctypes.util
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time


PRISM_VID = 0x2E8A
PRISM_PID = 0x000A
MANAGEMENT_MAGIC = 0x4D535250
MANAGEMENT_VERSION = 1
MANAGEMENT_LAUNCH = 0x16
MANAGEMENT_RESPONSE = 1 << 0
MANAGEMENT_ERROR = 1 << 2
DEFAULT_APP_ID = "dev.preyneyv.prism.full-test"


def find_picotool(explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).expanduser().resolve()
        if candidate.is_file():
            return candidate
        raise FileNotFoundError(f"picotool does not exist: {candidate}")

    on_path = shutil.which("picotool")
    if on_path:
        return Path(on_path)

    executable = "picotool.exe" if os.name == "nt" else "picotool"
    roots = [
        Path.home() / ".pico-sdk" / "picotool",
        Path(os.environ.get("PICO_SDK_PATH", "")).parent / "picotool",
    ]
    candidates: list[Path] = []
    for root in roots:
        if root.is_dir():
            candidates.extend(root.glob(f"*/picotool/{executable}"))
            candidates.extend(root.glob(f"*/{executable}"))
    if candidates:
        def version_key(candidate: Path):
            version = candidate.parents[1].name
            return ("-" not in version, version)

        return max(candidates, key=version_key)
    raise FileNotFoundError(
        "picotool was not found on PATH or under ~/.pico-sdk/picotool"
    )


def run_picotool(picotool: Path, arguments: list[str]) -> None:
    command = [str(picotool), *arguments]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def flash(picotool: Path, firmware: Path) -> None:
    run_picotool(picotool, ["reboot", "-f", "-u"])
    time.sleep(0.75)

    deadline = time.monotonic() + 15.0
    last_error: subprocess.CalledProcessError | None = None
    while time.monotonic() < deadline:
        try:
            run_picotool(picotool, ["load", str(firmware), "-fx"])
            return
        except subprocess.CalledProcessError as error:
            last_error = error
            time.sleep(0.25)
    if last_error is not None:
        raise last_error
    raise RuntimeError("Prism did not enter BOOTSEL mode")


def pyusb_backend():
    try:
        import usb.backend.libusb1
    except ImportError as error:
        raise RuntimeError(
            "PyUSB is required. Install it with: python -m pip install pyusb"
        ) from error

    backend = usb.backend.libusb1.get_backend()
    if backend is not None:
        return backend

    libraries: list[Path] = []
    if os.name == "nt":
        openocd = Path.home() / ".pico-sdk" / "openocd"
        if openocd.is_dir():
            libraries.extend(openocd.glob("*/libusb-1.0.dll"))
    system_library = ctypes.util.find_library("usb-1.0")
    if system_library:
        libraries.append(Path(system_library))

    for library in reversed(sorted(libraries)):
        backend = usb.backend.libusb1.get_backend(
            find_library=lambda _name, value=str(library): value
        )
        if backend is not None:
            return backend

    try:
        import libusb_package

        backend = libusb_package.get_libusb1_backend()
        if backend is not None:
            return backend
    except ImportError:
        pass

    raise RuntimeError(
        "No libusb backend was found. Install libusb-package with: "
        "python -m pip install libusb-package"
    )


def wait_for_prism(backend, timeout: float):
    import usb.core
    import usb.util

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        device = usb.core.find(
            idVendor=PRISM_VID, idProduct=PRISM_PID, backend=backend
        )
        if device is not None:
            try:
                try:
                    device.get_active_configuration()
                except usb.core.USBError:
                    device.set_configuration()
                    device.get_active_configuration()
                return device
            except usb.core.USBError:
                usb.util.dispose_resources(device)
        time.sleep(0.1)
    raise TimeoutError("Prism did not re-enumerate after flashing")


def management_interface(device):
    import usb.core
    import usb.util

    try:
        configuration = device.get_active_configuration()
    except usb.core.USBError:
        device.set_configuration()
        configuration = device.get_active_configuration()

    for interface in configuration:
        if interface.bInterfaceClass != 0xFF:
            continue
        endpoints = list(interface)
        endpoint_out = next(
            (
                endpoint
                for endpoint in endpoints
                if usb.util.endpoint_direction(endpoint.bEndpointAddress)
                == usb.util.ENDPOINT_OUT
                and usb.util.endpoint_type(endpoint.bmAttributes)
                == usb.util.ENDPOINT_TYPE_BULK
            ),
            None,
        )
        endpoint_in = next(
            (
                endpoint
                for endpoint in endpoints
                if usb.util.endpoint_direction(endpoint.bEndpointAddress)
                == usb.util.ENDPOINT_IN
                and usb.util.endpoint_type(endpoint.bmAttributes)
                == usb.util.ENDPOINT_TYPE_BULK
            ),
            None,
        )
        if endpoint_out is not None and endpoint_in is not None:
            usb.util.claim_interface(device, interface.bInterfaceNumber)
            return interface.bInterfaceNumber, endpoint_out, endpoint_in
    raise RuntimeError("Prism management interface was not found")


def app_key(app_id: str) -> bytes:
    authored = app_id.encode("ascii")
    return hashlib.sha256(b"prism.app.v1\0" + authored).digest()[:16]


def launch(device, app_id: str, timeout: float) -> None:
    import usb.util

    interface_number, endpoint_out, endpoint_in = management_interface(device)
    request_id = 1
    payload = app_key(app_id)
    packet = struct.pack(
        "<IBBHII",
        MANAGEMENT_MAGIC,
        MANAGEMENT_VERSION,
        MANAGEMENT_LAUNCH,
        0,
        request_id,
        len(payload),
    ) + payload

    try:
        device.write(endpoint_out.bEndpointAddress, packet, timeout=5000)
        incoming = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
            chunk = device.read(
                endpoint_in.bEndpointAddress, 4096, timeout=remaining_ms
            )
            incoming.extend(chunk)
            while len(incoming) >= 16:
                magic, version, message_type, flags, response_id, length = (
                    struct.unpack_from("<IBBHII", incoming)
                )
                if magic != MANAGEMENT_MAGIC or version != MANAGEMENT_VERSION:
                    del incoming[0]
                    continue
                if length > 4096:
                    del incoming[0]
                    continue
                if len(incoming) < 16 + length:
                    break
                response = bytes(incoming[16 : 16 + length])
                del incoming[: 16 + length]
                if not (flags & MANAGEMENT_RESPONSE) or response_id != request_id:
                    continue
                if message_type != MANAGEMENT_LAUNCH:
                    continue
                if flags & MANAGEMENT_ERROR:
                    status = (
                        struct.unpack_from("<H", response)[0]
                        if len(response) >= 2
                        else "unknown"
                    )
                    raise RuntimeError(
                        f"Prism rejected the launch command (status {status})"
                    )
                return
        raise TimeoutError("Prism did not acknowledge the launch command")
    finally:
        try:
            usb.util.release_interface(device, interface_number)
        finally:
            usb.util.dispose_resources(device)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--firmware",
        type=Path,
        default=root / "build" / "prism.elf",
        help="Prism ELF or UF2 to flash (default: build/prism.elf)",
    )
    parser.add_argument("--picotool", help="path to picotool")
    parser.add_argument("--app-id", default=DEFAULT_APP_ID)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()

    firmware = args.firmware.expanduser().resolve()
    if not firmware.is_file():
        parser.error(f"firmware does not exist: {firmware}")

    try:
        picotool = find_picotool(args.picotool)
        backend = pyusb_backend()
        flash(picotool, firmware)
        print("waiting for Prism to reboot...", flush=True)
        device = wait_for_prism(backend, args.timeout)
        launch(device, args.app_id, args.timeout)
    except (
        FileNotFoundError,
        RuntimeError,
        TimeoutError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"launched {args.app_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
