#!/usr/bin/env python3
"""Build an uncompressed Prism asset pack from path=file inputs."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct


PACK_MAGIC = 0x4B415050
PACK_VERSION = 1
HEADER_BYTES = 256
FILE_ENTRY_BYTES = 16
PACK_KEY_BYTES = 16
APP_KEY_BYTES = 16
MAX_FILES = 4096
MAX_PATH_BYTES = 255


class PackError(ValueError):
    pass


def align(value: int, alignment: int = 4) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def validate_id(value: str, field: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise PackError(f"{field} must be ASCII") from error
    if not 1 <= len(encoded) <= 253:
        raise PackError(f"{field} must contain 1..253 ASCII bytes")
    labels = value.split(".")
    if any(not 1 <= len(label) <= 63 for label in labels) or any(
        label.startswith("-")
        or label.endswith("-")
        or re.fullmatch(r"[a-z0-9-]+", label) is None
        for label in labels
    ):
        raise PackError(f"{field} must be a canonical lowercase reverse-FQDN")
    return encoded


def validate_path(value: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise PackError("asset paths must be printable ASCII") from error
    if not 1 <= len(encoded) <= MAX_PATH_BYTES:
        raise PackError(f"asset paths must contain 1..{MAX_PATH_BYTES} bytes")
    if value.startswith("/") or value.endswith("/") or "\\" in value:
        raise PackError(f"asset path is not canonical: {value}")
    components = value.split("/")
    if any(
        not component
        or component in (".", "..")
        or any(ord(character) < 0x21 or ord(character) > 0x7E for character in component)
        for component in components
    ):
        raise PackError(f"asset path is not canonical: {value}")
    return encoded


def derive_key(domain: bytes, identifier: bytes) -> bytes:
    return hashlib.sha256(domain + b"\0" + identifier).digest()[:16]


def parse_file_argument(raw: str) -> tuple[str, Path]:
    if "=" not in raw:
        raise PackError(f"asset input must be path=file: {raw}")
    logical, source = raw.split("=", 1)
    validate_path(logical)
    source_path = Path(source).resolve()
    if not source_path.is_file():
        raise PackError(f"asset source does not exist: {source_path}")
    return logical, source_path


def build_pack(
    output: Path,
    pack_id: str,
    name: str,
    version: int,
    target_id: str,
    target_min_version: int,
    target_max_version: int,
    inputs: list[tuple[str, Path]],
) -> bytes:
    pack_id_bytes = validate_id(pack_id, "pack id")
    target_id_bytes = validate_id(target_id, "target id")
    name_bytes = name.encode("utf-8")
    if not 1 <= len(name_bytes) <= 31:
        raise PackError("pack name must contain 1..31 UTF-8 bytes")
    if not 1 <= version <= 0xFFFFFFFF:
        raise PackError("pack version must fit uint32 and be nonzero")
    if not 0 <= target_min_version <= 0xFFFFFFFF or not 0 <= target_max_version <= 0xFFFFFFFF:
        raise PackError("target version bounds must fit uint32")
    if target_max_version and target_max_version < target_min_version:
        raise PackError("target maximum version is below its minimum")
    if not 1 <= len(inputs) <= MAX_FILES:
        raise PackError(f"asset packs must contain 1..{MAX_FILES} files")

    normalized: list[tuple[str, bytes, bytes]] = []
    for logical, source in inputs:
        normalized.append((logical, validate_path(logical), source.read_bytes()))
    normalized.sort(key=lambda item: item[1])
    if any(normalized[index - 1][1] == normalized[index][1] for index in range(1, len(normalized))):
        raise PackError("asset paths must be unique")

    directory_offset = HEADER_BYTES
    directory_bytes = len(normalized) * FILE_ENTRY_BYTES
    string_table_offset = directory_offset + directory_bytes
    strings = bytearray()

    def add_string(value: bytes) -> tuple[int, int]:
        offset = string_table_offset + len(strings)
        strings.extend(value)
        strings.append(0)
        return offset, len(value)

    id_offset, id_length = add_string(pack_id_bytes)
    name_offset, name_length = add_string(name_bytes)
    target_id_offset, target_id_length = add_string(target_id_bytes)
    path_locations: list[tuple[int, int]] = []
    for _logical, encoded, _contents in normalized:
        path_locations.append(add_string(encoded))

    data_offset = align(string_table_offset + len(strings))
    data = bytearray()
    entries = bytearray()
    for (logical, _encoded, contents), (path_offset, path_length) in zip(normalized, path_locations):
        del logical
        while (data_offset + len(data)) & 3:
            data.append(0)
        file_offset = data_offset + len(data)
        entries.extend(
            struct.pack(
                "<IHHII",
                path_offset,
                path_length,
                0,
                file_offset,
                len(contents),
            )
        )
        data.extend(contents)

    package_size = data_offset + len(data)
    header = bytearray(HEADER_BYTES)
    struct.pack_into(
        "<IHHII16s16s" + "I" * 13,
        header,
        0,
        PACK_MAGIC,
        PACK_VERSION,
        HEADER_BYTES,
        package_size,
        version,
        derive_key(b"prism.pack.v1", pack_id_bytes),
        derive_key(b"prism.app.v1", target_id_bytes),
        target_min_version,
        target_max_version,
        len(normalized),
        directory_offset,
        string_table_offset,
        len(strings),
        data_offset,
        id_offset,
        id_length,
        name_offset,
        name_length,
        target_id_offset,
        target_id_length,
    )
    result = bytes(header) + bytes(entries) + bytes(strings)
    result += bytes(data_offset - len(result)) + bytes(data)
    if len(result) != package_size:
        raise AssertionError("asset pack size calculation disagrees with output")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True, type=int)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--target-min-version", type=int, default=0)
    parser.add_argument("--target-max-version", type=int, default=0)
    parser.add_argument("--file", action="append", required=True, dest="files")
    args = parser.parse_args()
    try:
        inputs = [parse_file_argument(raw) for raw in args.files]
        result = build_pack(
            args.output.resolve(),
            args.id,
            args.name,
            args.version,
            args.target_id,
            args.target_min_version,
            args.target_max_version,
            inputs,
        )
    except (OSError, PackError) as error:
        parser.error(str(error))
    print(f"Packaged {len(inputs)} assets, {len(result)} bytes: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
