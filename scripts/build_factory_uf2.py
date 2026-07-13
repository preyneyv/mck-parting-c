#!/usr/bin/env python3
"""Provision Prism cartridges and asset packs into a factory UF2 image."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import zlib


UF2_BLOCK_BYTES = 512
UF2_DATA_BYTES = 476
UF2_PAYLOAD_BYTES = 256
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000

XIP_BASE = 0x10000000
FLASH_BYTES = 16 * 1024 * 1024
FIRMWARE_BYTES = 2 * 1024 * 1024
CARTRIDGE_OFFSET = FIRMWARE_BYTES
STORAGE_BLOCK_BYTES = 64 * 1024
STORAGE_BLOCK_COUNT = 192
CARTRIDGE_BLOCK_BYTES = STORAGE_BLOCK_BYTES
CARTRIDGE_BLOCK_COUNT = STORAGE_BLOCK_COUNT
CARTRIDGE_END = CARTRIDGE_OFFSET + (
    STORAGE_BLOCK_BYTES * STORAGE_BLOCK_COUNT
)
FLASH_SECTOR_BYTES = 4096
CATALOG_SLOT_BYTES = 2 * FLASH_SECTOR_BYTES
CATALOG0_OFFSET = CARTRIDGE_END + STORAGE_BLOCK_BYTES
CATALOG1_OFFSET = CATALOG0_OFFSET + CATALOG_SLOT_BYTES
MOVE_JOURNAL_OFFSET = CATALOG1_OFFSET + CATALOG_SLOT_BYTES
MOVE_JOURNAL_SECTORS = 8
MOVE_JOURNAL_BYTES = MOVE_JOURNAL_SECTORS * FLASH_SECTOR_BYTES
CARTRIDGE_DATA_OFFSET = 15 * 1024 * 1024
SETTINGS_SLOT_COUNT = 2
SETTINGS_BYTES = SETTINGS_SLOT_COUNT * FLASH_SECTOR_BYTES
SETTINGS_OFFSET = FLASH_BYTES - SETTINGS_BYTES
CARTRIDGE_DATA_BYTES = SETTINGS_OFFSET - CARTRIDGE_DATA_OFFSET
CARTRIDGE_DATA_ARENA_BYTES = CARTRIDGE_DATA_BYTES // 2
CARTRIDGE_DATA_ARENA0_OFFSET = CARTRIDGE_DATA_OFFSET
CARTRIDGE_DATA_ARENA1_OFFSET = (
    CARTRIDGE_DATA_ARENA0_OFFSET + CARTRIDGE_DATA_ARENA_BYTES
)
PERSISTENCE_MARKER_BYTES = 2 * UF2_PAYLOAD_BYTES

PACKAGE_MAGIC = 0x4B505250
PACKAGE_FORMAT_VERSION = 1
PACKAGE_HEADER_BYTES = 256
PACKAGE_APP_KEY_OFFSET = 16
APP_KEY_BYTES = 16
ASSET_PACK_MAGIC = 0x4B415050
ASSET_PACK_FORMAT_VERSION = 1
ASSET_PACK_HEADER_BYTES = 256
OBJECT_CARTRIDGE = 1
OBJECT_ASSET_PACK = 2

CATALOG_MAGIC = 0x54414350
CATALOG_VERSION = 1
CATALOG_MAX_ENTRIES = STORAGE_BLOCK_COUNT
CATALOG_ENTRY_BYTES = 32
CATALOG_HEADER_BYTES = 20
CATALOG_ENTRY_LIVE = 1


class FactoryImageError(ValueError):
    """The requested factory image cannot be represented safely."""


@dataclass(frozen=True)
class Uf2Block:
    raw: bytes
    flags: int
    target: int
    payload_size: int
    block_number: int
    block_count: int
    family_or_size: int


@dataclass(frozen=True)
class FactoryPackage:
    path: Path
    data: bytes
    object_key: bytes
    kind: int
    start_block: int
    block_count: int
    crc32: int


def _parse_uf2(path: Path) -> list[Uf2Block]:
    contents = path.read_bytes()
    if not contents or len(contents) % UF2_BLOCK_BYTES != 0:
        raise FactoryImageError(f"invalid UF2 length: {path}")

    blocks: list[Uf2Block] = []
    for offset in range(0, len(contents), UF2_BLOCK_BYTES):
        raw = contents[offset : offset + UF2_BLOCK_BYTES]
        header = struct.unpack_from("<8I", raw)
        if (
            header[0] != UF2_MAGIC_START0
            or header[1] != UF2_MAGIC_START1
            or struct.unpack_from("<I", raw, 508)[0] != UF2_MAGIC_END
        ):
            raise FactoryImageError(
                f"invalid UF2 block at byte offset {offset}: {path}"
            )
        if header[4] == 0 or header[4] > UF2_DATA_BYTES:
            raise FactoryImageError(
                f"invalid UF2 payload size {header[4]}: {path}"
            )
        blocks.append(
            Uf2Block(
                raw=raw,
                flags=header[2],
                target=header[3],
                payload_size=header[4],
                block_number=header[5],
                block_count=header[6],
                family_or_size=header[7],
            )
        )

    expected_count = len(blocks)
    if any(block.block_count != expected_count for block in blocks):
        raise FactoryImageError(f"inconsistent UF2 block count: {path}")
    if sorted(block.block_number for block in blocks) != list(
        range(expected_count)
    ):
        raise FactoryImageError(f"invalid UF2 block numbering: {path}")
    blocks.sort(key=lambda block: block.block_number)
    return blocks


def _read_package(path: Path, start_block: int) -> FactoryPackage:
    data = path.read_bytes()
    if len(data) < PACKAGE_HEADER_BYTES:
        raise FactoryImageError(f"package is smaller than its header: {path}")
    magic, format_version, header_size = struct.unpack_from("<IHH", data)
    if magic == PACKAGE_MAGIC:
        expected_format = PACKAGE_FORMAT_VERSION
        expected_header = PACKAGE_HEADER_BYTES
        package_size_offset = 12
        kind = OBJECT_CARTRIDGE
    elif magic == ASSET_PACK_MAGIC:
        expected_format = ASSET_PACK_FORMAT_VERSION
        expected_header = ASSET_PACK_HEADER_BYTES
        package_size_offset = 8
        kind = OBJECT_ASSET_PACK
    else:
        raise FactoryImageError(f"invalid stored-object magic: {path}")
    package_size = struct.unpack_from("<I", data, package_size_offset)[0]
    if format_version != expected_format:
        raise FactoryImageError(
            f"unsupported stored-object format {format_version}: {path}"
        )
    if header_size != expected_header:
        raise FactoryImageError(f"invalid stored-object header size: {path}")
    if package_size != len(data):
        raise FactoryImageError(
            f"package header says {package_size} bytes but file has "
            f"{len(data)}: {path}"
        )

    block_count = (
        len(data) + STORAGE_BLOCK_BYTES - 1
    ) // STORAGE_BLOCK_BYTES
    object_key = data[
        PACKAGE_APP_KEY_OFFSET : PACKAGE_APP_KEY_OFFSET + APP_KEY_BYTES
    ]
    if len(object_key) != APP_KEY_BYTES:
        raise FactoryImageError(f"stored object is missing its key: {path}")
    return FactoryPackage(
        path=path,
        data=data,
        object_key=object_key,
        kind=kind,
        start_block=start_block,
        block_count=block_count,
        crc32=zlib.crc32(data),
    )


def _catalog(packages: list[FactoryPackage], generation: int) -> bytes:
    entries = bytearray(CATALOG_MAX_ENTRIES * CATALOG_ENTRY_BYTES)
    for index, package in enumerate(packages):
        struct.pack_into(
            "<16sHHIIBBH",
            entries,
            index * CATALOG_ENTRY_BYTES,
            package.object_key,
            package.start_block,
            package.block_count,
            len(package.data),
            package.crc32,
            CATALOG_ENTRY_LIVE,
            package.kind,
            0,
        )

    live_entries = bytes(entries[: len(packages) * CATALOG_ENTRY_BYTES])
    header = struct.pack(
        "<IHHIII",
        CATALOG_MAGIC,
        CATALOG_VERSION,
        len(packages),
        generation,
        0,
        zlib.crc32(live_entries),
    )
    catalog = header + entries
    if len(catalog) > CATALOG_SLOT_BYTES:
        raise FactoryImageError("factory catalog does not fit in its slot")
    return catalog + bytes([0xFF]) * (CATALOG_SLOT_BYTES - len(catalog))


def _data_blocks(target: int, contents: bytes, fill: int = 0xFF):
    if target % UF2_PAYLOAD_BYTES != 0:
        raise FactoryImageError(f"unaligned UF2 target 0x{target:08x}")
    for offset in range(0, len(contents), UF2_PAYLOAD_BYTES):
        payload = contents[offset : offset + UF2_PAYLOAD_BYTES]
        if len(payload) < UF2_PAYLOAD_BYTES:
            payload += bytes([fill]) * (UF2_PAYLOAD_BYTES - len(payload))
        yield target + offset, payload


def _new_uf2_block(
    target: int,
    payload: bytes,
    block_number: int,
    block_count: int,
    family_id: int,
) -> bytes:
    if len(payload) != UF2_PAYLOAD_BYTES:
        raise FactoryImageError("factory UF2 payloads must be 256 bytes")
    block = bytearray(UF2_BLOCK_BYTES)
    struct.pack_into(
        "<8I",
        block,
        0,
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FLAG_FAMILY_ID_PRESENT,
        target,
        len(payload),
        block_number,
        block_count,
        family_id,
    )
    block[32 : 32 + len(payload)] = payload
    struct.pack_into("<I", block, 508, UF2_MAGIC_END)
    return bytes(block)


def _renumber_uf2_block(raw: bytes, block_number: int, block_count: int) -> bytes:
    block = bytearray(raw)
    struct.pack_into("<II", block, 20, block_number, block_count)
    return bytes(block)


def build_factory_uf2(
    firmware_path: Path,
    output_path: Path,
    package_paths: list[Path],
) -> list[FactoryPackage]:
    firmware_blocks = _parse_uf2(firmware_path)
    if not package_paths:
        raise FactoryImageError("at least one factory package is required")
    if len(package_paths) > CATALOG_MAX_ENTRIES:
        raise FactoryImageError(
            f"factory image has {len(package_paths)} packages; maximum is "
            f"{CATALOG_MAX_ENTRIES}"
        )

    family_ids = {
        block.family_or_size
        for block in firmware_blocks
        if block.flags & UF2_FLAG_FAMILY_ID_PRESENT
    }
    if len(family_ids) != 1:
        raise FactoryImageError(
            "firmware UF2 must contain exactly one declared family ID"
        )
    family_id = next(iter(family_ids))
    firmware_end = XIP_BASE + FIRMWARE_BYTES
    for block in firmware_blocks:
        if (
            block.target < XIP_BASE
            or block.target + block.payload_size > firmware_end
        ):
            raise FactoryImageError(
                "firmware UF2 already writes outside the firmware region: "
                f"0x{block.target:08x}"
            )

    packages: list[FactoryPackage] = []
    next_block = 0
    object_keys: set[tuple[int, bytes]] = set()
    for raw_path in package_paths:
        package = _read_package(raw_path.resolve(), next_block)
        identity = (package.kind, package.object_key)
        if identity in object_keys:
            raise FactoryImageError(
                f"duplicate factory stored-object key: {package.path}"
            )
        object_keys.add(identity)
        packages.append(package)
        next_block += package.block_count
    if next_block > STORAGE_BLOCK_COUNT:
        raise FactoryImageError(
            f"factory packages require {next_block} blocks; maximum is "
            f"{STORAGE_BLOCK_COUNT}"
        )

    additions: list[tuple[int, bytes]] = []
    for package in packages:
        allocated = package.block_count * STORAGE_BLOCK_BYTES
        padded = package.data + bytes([0xFF]) * (allocated - len(package.data))
        target = XIP_BASE + CARTRIDGE_OFFSET + (
            package.start_block * STORAGE_BLOCK_BYTES
        )
        additions.extend(_data_blocks(target, padded))
    additions.extend(
        _data_blocks(XIP_BASE + CATALOG0_OFFSET, _catalog(packages, 1))
    )
    additions.extend(
        _data_blocks(XIP_BASE + CATALOG1_OFFSET, _catalog(packages, 2))
    )
    # A factory image is safe to apply directly over an existing installation.
    # Invalidate every move-journal page so an interrupted old compaction cannot
    # be resumed against the new catalog. The journal writer erases a sector
    # before it next records a move.
    additions.extend(
        _data_blocks(
            XIP_BASE + MOVE_JOURNAL_OFFSET,
            bytes(MOVE_JOURNAL_BYTES),
        )
    )
    # Invalidating both persistence arena markers makes old cartridge data
    # unreachable. The persistence layer erases and initializes an empty arena
    # on first use; the remainder can be reclaimed lazily.
    for arena_offset in (
        CARTRIDGE_DATA_ARENA0_OFFSET,
        CARTRIDGE_DATA_ARENA1_OFFSET,
    ):
        additions.extend(
            _data_blocks(
                XIP_BASE + arena_offset,
                bytes(PERSISTENCE_MARKER_BYTES),
            )
        )
    # Settings records may occupy any page in either slot, so invalidate both
    # complete sectors to restore factory defaults deterministically.
    additions.extend(
        _data_blocks(XIP_BASE + SETTINGS_OFFSET, bytes(SETTINGS_BYTES))
    )

    occupied = {
        block.target + offset
        for block in firmware_blocks
        for offset in range(0, block.payload_size, UF2_PAYLOAD_BYTES)
    }
    addition_targets = [target for target, _payload in additions]
    if len(addition_targets) != len(set(addition_targets)):
        raise FactoryImageError("factory data regions overlap each other")
    flash_end = XIP_BASE + FLASH_BYTES
    if any(
        target < XIP_BASE or target + UF2_PAYLOAD_BYTES > flash_end
        for target in addition_targets
    ):
        raise FactoryImageError("factory data falls outside physical flash")
    if any(target in occupied for target, _payload in additions):
        raise FactoryImageError("factory data overlaps the firmware UF2")

    total_blocks = len(firmware_blocks) + len(additions)
    output = bytearray()
    for index, block in enumerate(firmware_blocks):
        output.extend(_renumber_uf2_block(block.raw, index, total_blocks))
    for addition_index, (target, payload) in enumerate(additions):
        output.extend(
            _new_uf2_block(
                target,
                payload,
                len(firmware_blocks) + addition_index,
                total_blocks,
                family_id,
            )
        )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    return packages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("packages", nargs="+", type=Path)
    args = parser.parse_args()

    try:
        packages = build_factory_uf2(
            args.firmware.resolve(), args.output.resolve(), args.packages
        )
    except (FactoryImageError, OSError) as error:
        parser.error(str(error))

    for package in packages:
        last_block = package.start_block + package.block_count - 1
        print(
            f"Factory package {package.path.name}: blocks "
            f"{package.start_block}-{last_block}, {len(package.data)} bytes"
        )
    print(f"Generated factory UF2: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
