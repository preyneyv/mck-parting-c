from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_assets", ROOT / "scripts" / "package_assets.py"
)
PACKAGER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = PACKAGER
SPEC.loader.exec_module(PACKAGER)


class AssetPackTests(unittest.TestCase):
    def test_builds_deterministic_sorted_uncompressed_pack(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            first = directory / "first.bin"
            second = directory / "second.bin"
            output = directory / "tracks.prismpack"
            first.write_bytes(b"golden")
            second.write_bytes(b"never")

            package = PACKAGER.build_pack(
                output,
                "dev.preyneyv.prism.beatline.tracks",
                "Tracks",
                3,
                "dev.preyneyv.prism.beatline",
                1,
                4,
                [("z/never.beatline", second), ("a/golden.beatline", first)],
            )
            self.assertEqual(output.read_bytes(), package)
            header = struct.unpack_from("<IHHII16s16s" + "I" * 13, package)
            self.assertEqual(
                header[:5],
                (PACKAGER.PACK_MAGIC, PACKAGER.PACK_VERSION,
                 PACKAGER.HEADER_BYTES, len(package), 3),
            )
            self.assertEqual(
                header[5],
                hashlib.sha256(
                    b"prism.pack.v1\0dev.preyneyv.prism.beatline.tracks"
                ).digest()[:16],
            )
            self.assertEqual(
                header[6],
                hashlib.sha256(
                    b"prism.app.v1\0dev.preyneyv.prism.beatline"
                ).digest()[:16],
            )
            file_count, directory_offset = header[9:11]
            self.assertEqual(file_count, 2)
            entries = [
                struct.unpack_from("<IHHII", package,
                                   directory_offset + index * 16)
                for index in range(file_count)
            ]
            paths = [
                package[offset:offset + length].decode("ascii")
                for offset, length, _flags, _data_offset, _data_length
                in entries
            ]
            self.assertEqual(paths, ["a/golden.beatline", "z/never.beatline"])
            self.assertEqual(
                [package[data_offset:data_offset + data_length]
                 for _path_offset, _path_length, _flags, data_offset,
                 data_length in entries],
                [b"golden", b"never"],
            )

    def test_rejects_noncanonical_and_duplicate_paths(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            source = directory / "asset.bin"
            source.write_bytes(b"asset")
            for path in ("/asset", "asset/", "asset//file", "asset/../file"):
                with self.assertRaises(PACKAGER.PackError):
                    PACKAGER.build_pack(
                        directory / "bad.prismpack",
                        "dev.example.pack", "Pack", 1, "dev.example.app",
                        0, 0, [(path, source)],
                    )
            with self.assertRaisesRegex(PACKAGER.PackError, "unique"):
                PACKAGER.build_pack(
                    directory / "duplicate.prismpack",
                    "dev.example.pack", "Pack", 1, "dev.example.app",
                    0, 0, [("asset.bin", source), ("asset.bin", source)],
                )


if __name__ == "__main__":
    unittest.main()
