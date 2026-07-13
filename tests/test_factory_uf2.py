from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_factory_uf2", ROOT / "scripts" / "build_factory_uf2.py"
)
factory = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = factory
SPEC.loader.exec_module(factory)


FAMILY_ID = 0xE48BFF56


def firmware_uf2() -> bytes:
    block = bytearray(512)
    struct.pack_into(
        "<8I",
        block,
        0,
        factory.UF2_MAGIC_START0,
        factory.UF2_MAGIC_START1,
        factory.UF2_FLAG_FAMILY_ID_PRESENT,
        factory.XIP_BASE,
        factory.UF2_PAYLOAD_BYTES,
        0,
        1,
        FAMILY_ID,
    )
    block[32 : 32 + factory.UF2_PAYLOAD_BYTES] = bytes(
        range(factory.UF2_PAYLOAD_BYTES)
    )
    struct.pack_into("<I", block, 508, factory.UF2_MAGIC_END)
    return bytes(block)


def package(app_key: bytes, size: int) -> bytes:
    assert len(app_key) == factory.APP_KEY_BYTES
    assert size >= factory.PACKAGE_HEADER_BYTES
    contents = bytearray(size)
    struct.pack_into(
        "<IHH",
        contents,
        0,
        factory.PACKAGE_MAGIC,
        factory.PACKAGE_FORMAT_VERSION,
        factory.PACKAGE_HEADER_BYTES,
    )
    struct.pack_into("<I", contents, 12, size)
    contents[
        factory.PACKAGE_APP_KEY_OFFSET :
        factory.PACKAGE_APP_KEY_OFFSET + factory.APP_KEY_BYTES
    ] = app_key
    for index in range(factory.PACKAGE_HEADER_BYTES, size):
        contents[index] = index & 0xFF
    return bytes(contents)


def parse_uf2(contents: bytes):
    blocks = []
    assert len(contents) % factory.UF2_BLOCK_BYTES == 0
    for offset in range(0, len(contents), factory.UF2_BLOCK_BYTES):
        block = contents[offset : offset + factory.UF2_BLOCK_BYTES]
        header = struct.unpack_from("<8I", block)
        blocks.append((header, block[32 : 32 + header[4]]))
    return blocks


def read_region(blocks, target: int, size: int) -> bytes:
    by_target = {header[3]: payload for header, payload in blocks}
    output = bytearray()
    while len(output) < size:
        cursor = target + len(output)
        block_target = cursor - (cursor % factory.UF2_PAYLOAD_BYTES)
        payload = by_target[block_target]
        start = cursor - block_target
        output.extend(payload[start:])
    return bytes(output[:size])


class FactoryUf2Tests(unittest.TestCase):
    def test_packages_are_normal_catalog_entries_in_final_blocks(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            firmware_path = directory / "prism.uf2"
            first_path = directory / "first.prism"
            second_path = directory / "second.prism"
            output_path = directory / "prism_factory.uf2"
            firmware_path.write_bytes(firmware_uf2())
            first = package(bytes(range(16)), 300)
            second = package(
                bytes(range(16, 32)), factory.CARTRIDGE_BLOCK_BYTES + 1
            )
            first_path.write_bytes(first)
            second_path.write_bytes(second)

            packages = factory.build_factory_uf2(
                firmware_path, output_path, [first_path, second_path]
            )

            self.assertEqual([value.start_block for value in packages], [0, 1])
            self.assertEqual([value.block_count for value in packages], [1, 2])
            self.assertEqual(firmware_path.read_bytes(), firmware_uf2())

            blocks = parse_uf2(output_path.read_bytes())
            self.assertTrue(blocks)
            self.assertTrue(
                all(header[5] == index for index, (header, _data) in enumerate(blocks))
            )
            self.assertTrue(all(header[6] == len(blocks) for header, _ in blocks))
            self.assertEqual(
                read_region(blocks, factory.XIP_BASE, 256), bytes(range(256))
            )

            first_target = factory.XIP_BASE + factory.CARTRIDGE_OFFSET
            second_target = first_target + factory.CARTRIDGE_BLOCK_BYTES
            self.assertEqual(read_region(blocks, first_target, len(first)), first)
            self.assertEqual(read_region(blocks, second_target, len(second)), second)
            first_padding = read_region(
                blocks,
                first_target + len(first),
                factory.CARTRIDGE_BLOCK_BYTES - len(first),
            )
            self.assertEqual(set(first_padding), {0xFF})

            for slot_offset, generation in (
                (factory.CATALOG0_OFFSET, 1),
                (factory.CATALOG1_OFFSET, 2),
            ):
                catalog = read_region(
                    blocks,
                    factory.XIP_BASE + slot_offset,
                    factory.FLASH_SECTOR_BYTES,
                )
                magic, version, count, actual_generation, flags, entries_crc = (
                    struct.unpack_from("<IHHIII", catalog)
                )
                self.assertEqual(magic, factory.CATALOG_MAGIC)
                self.assertEqual(version, factory.CATALOG_VERSION)
                self.assertEqual(count, 2)
                self.assertEqual(actual_generation, generation)
                self.assertEqual(flags, 0)
                live_entries = catalog[
                    factory.CATALOG_HEADER_BYTES :
                    factory.CATALOG_HEADER_BYTES
                    + count * factory.CATALOG_ENTRY_BYTES
                ]
                self.assertEqual(zlib.crc32(live_entries), entries_crc)

                entry0 = struct.unpack_from("<16sHHIIB3s", live_entries, 0)
                entry1 = struct.unpack_from(
                    "<16sHHIIB3s", live_entries, factory.CATALOG_ENTRY_BYTES
                )
                self.assertEqual(entry0[:3], (bytes(range(16)), 0, 1))
                self.assertEqual(entry1[:3], (bytes(range(16, 32)), 1, 2))
                self.assertEqual(entry0[3:6], (len(first), zlib.crc32(first), 1))
                self.assertEqual(entry1[3:6], (len(second), zlib.crc32(second), 1))

            self.assertEqual(
                read_region(
                    blocks,
                    factory.XIP_BASE + factory.MOVE_JOURNAL_OFFSET,
                    factory.MOVE_JOURNAL_BYTES,
                ),
                bytes(factory.MOVE_JOURNAL_BYTES),
            )
            for arena_offset in (
                factory.CARTRIDGE_DATA_ARENA0_OFFSET,
                factory.CARTRIDGE_DATA_ARENA1_OFFSET,
            ):
                self.assertEqual(
                    read_region(
                        blocks,
                        factory.XIP_BASE + arena_offset,
                        factory.PERSISTENCE_MARKER_BYTES,
                    ),
                    bytes(factory.PERSISTENCE_MARKER_BYTES),
                )
            self.assertEqual(
                read_region(
                    blocks,
                    factory.XIP_BASE + factory.SETTINGS_OFFSET,
                    factory.SETTINGS_BYTES,
                ),
                bytes(factory.SETTINGS_BYTES),
            )

    def test_duplicate_app_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            firmware_path = directory / "prism.uf2"
            first_path = directory / "first.prism"
            second_path = directory / "second.prism"
            firmware_path.write_bytes(firmware_uf2())
            duplicate_key = b"same-app-key-123"
            first_path.write_bytes(package(duplicate_key, 256))
            second_path.write_bytes(package(duplicate_key, 512))

            with self.assertRaisesRegex(
                factory.FactoryImageError, "duplicate factory cartridge"
            ):
                factory.build_factory_uf2(
                    firmware_path,
                    directory / "factory.uf2",
                    [first_path, second_path],
                )


if __name__ == "__main__":
    unittest.main()
