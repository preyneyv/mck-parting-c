import importlib.util
import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_cartridge", ROOT / "scripts" / "package_cartridge.py")
PACKAGER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PACKAGER)


class CartridgeIdTests(unittest.TestCase):
    def test_accepts_canonical_reverse_fqdn(self):
        PACKAGER.validate_id("dev.preyneyv.prism.beatline")
        PACKAGER.validate_id("a." + "b" * 63 + ".c")

    def test_rejects_noncanonical_ids(self):
        invalid = ["", ".dev.app", "dev.app.", "dev..app", "Dev.app",
                   "dev_app", "-dev.app", "dev-.app", "dev.äpp"]
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(PACKAGER.ElfError):
                PACKAGER.validate_id(value)
        with self.assertRaises(PACKAGER.ElfError):
            PACKAGER.validate_id("a" * 64 + ".app")
        with self.assertRaises(PACKAGER.ElfError):
            PACKAGER.validate_id("a" * 254)

    def test_key_derivation_is_deterministic(self):
        self.assertEqual(
            PACKAGER.derive_app_key("dev.preyneyv.prism.beatline").hex(),
            "32b41a03ab53336315fc9e3f33e17e40")


class ImportBoundaryTests(unittest.TestCase):
    def test_allows_got_import_relocations(self):
        PACKAGER.validate_import_relocations([
            (0x10, PACKAGER.R_ARM_GOT_BREL, 0xF0000064),
        ])

    def test_rejects_direct_import_call_relocations(self):
        with self.assertRaisesRegex(PACKAGER.ElfError, "non-GOT import"):
            PACKAGER.validate_import_relocations([
                (0x04, 10, 0xF000005B),  # R_ARM_THM_CALL
            ])


class PackageParsingTests(unittest.TestCase):
    @staticmethod
    def make_package(cartridge_id="dev.preyneyv.prism.beatline",
                     name="beatline", version=1, tick_divider=4):
        image = bytearray(384)
        id_offset = 64
        name_offset = id_offset + len(cartridge_id) + 1
        icon_offset = 160
        struct.pack_into("<IHHIIIII", image, 0, 0x50524354,
                         PACKAGER.CARTRIDGE_ABI, 64, tick_divider,
                         version, id_offset, name_offset, icon_offset)
        image[id_offset:id_offset + len(cartridge_id) + 1] = cartridge_id.encode() + b"\0"
        image[name_offset:name_offset + len(name) + 1] = name.encode() + b"\0"
        image[icon_offset:icon_offset + 180] = bytes(range(180))
        result = bytearray(256 + len(image))
        struct.pack_into("<IHHHHI", result, 0, PACKAGER.PACKAGE_MAGIC,
                         PACKAGER.PACKAGE_VERSION, PACKAGER.PACKAGE_HEADER_SIZE,
                         PACKAGER.CARTRIDGE_ABI, tick_divider, len(result))
        result[16:32] = PACKAGER.derive_app_key(cartridge_id)
        struct.pack_into("<III", result, 40, 256, len(image), 256)
        result[256:] = image
        return result

    def test_reads_identity_only_from_descriptor(self):
        package = self.make_package()
        metadata = PACKAGER.parse_package_metadata(package)
        self.assertEqual(metadata["id"], "dev.preyneyv.prism.beatline")
        self.assertEqual(metadata["name"], "beatline")
        self.assertEqual(metadata["version"], 1)
        self.assertEqual(metadata["tick_divider"], 4)
        self.assertNotIn(b"dev.preyneyv.prism.beatline", package[:256])
        self.assertNotIn(b"beatline", package[:256])

    def test_rejects_key_mismatch(self):
        package = self.make_package()
        package[16] ^= 1
        with self.assertRaises(PACKAGER.ElfError):
            PACKAGER.parse_package_metadata(package)

    def test_rejects_tick_divider_mismatch(self):
        package = self.make_package()
        struct.pack_into("<H", package, 10, 8)
        with self.assertRaisesRegex(PACKAGER.ElfError, "tick divider"):
            PACKAGER.parse_package_metadata(package)

    def test_name_change_keeps_identity_and_same_version(self):
        before = PACKAGER.parse_package_metadata(
            self.make_package(name="beatline", version=1))
        after = PACKAGER.parse_package_metadata(
            self.make_package(name="beatline dev", version=1))
        self.assertEqual(before["id"], after["id"])
        self.assertEqual(before["app_key"], after["app_key"])
        self.assertEqual(before["version"], after["version"])
        self.assertNotEqual(before["name"], after["name"])


if __name__ == "__main__":
    unittest.main()
