#!/usr/bin/env python3
"""Wrap a linked Cortex-M cartridge image in Prism's installable container.

This is intentionally a build-system helper, not a user-facing CLI.  The
cloneable cartridge template invokes it from CMake and emits a .prism file as
an ordinary build artifact.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import uuid


ELF_MAGIC = b"\x7fELF"
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_REL = 9
SHF_ALLOC = 0x2
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
R_ARM_ABS32 = 2

PACKAGE_MAGIC = 0x4B505250
PACKAGE_VERSION = 3
PACKAGE_HEADER_SIZE = 256
CARTRIDGE_ABI = 1
U8G2_ABI_HASH = 0x1F40A6D9
IMPORT_SENTINEL = 0xF0000000


class ElfError(RuntimeError):
    pass


class Elf32:
    def __init__(self, path: pathlib.Path):
        self.data = path.read_bytes()
        if len(self.data) < 52 or self.data[:4] != ELF_MAGIC:
            raise ElfError(f"{path} is not an ELF file")
        if self.data[4] != 1 or self.data[5] != 1:
            raise ElfError("cartridge ELF must be 32-bit little-endian")
        if struct.unpack_from("<H", self.data, 18)[0] != 40:
            raise ElfError("cartridge ELF is not for ARM")

        shoff = struct.unpack_from("<I", self.data, 32)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", self.data, 46)
        if shentsize != 40 or shoff + shnum * shentsize > len(self.data):
            raise ElfError("invalid ELF section table")
        self.sections: list[dict[str, int | str]] = []
        for index in range(shnum):
            values = struct.unpack_from("<IIIIIIIIII", self.data,
                                        shoff + index * shentsize)
            self.sections.append({
                "name_offset": values[0], "type": values[1],
                "flags": values[2], "addr": values[3],
                "offset": values[4], "size": values[5],
                "link": values[6], "info": values[7],
                "align": values[8], "entsize": values[9],
            })
        strings = self.section_bytes(shstrndx)
        for section in self.sections:
            section["name"] = self._cstring(strings, int(section["name_offset"]))

        self.symbols: list[dict[str, int | str]] = []
        self.symbol_table_index = -1
        for index, section in enumerate(self.sections):
            if section["type"] != SHT_SYMTAB:
                continue
            self.symbol_table_index = index
            names = self.section_bytes(int(section["link"]))
            raw = self.section_bytes(index)
            if int(section["entsize"]) != 16:
                raise ElfError("invalid ELF symbol table")
            for offset in range(0, len(raw), 16):
                name, value, size, info, other, shndx = struct.unpack_from(
                    "<IIIBBH", raw, offset)
                self.symbols.append({
                    "name": self._cstring(names, name), "value": value,
                    "size": size, "info": info, "other": other,
                    "shndx": shndx,
                })
            break
        if self.symbol_table_index < 0:
            raise ElfError("linked cartridge has no symbol table")

    @staticmethod
    def _cstring(data: bytes, offset: int) -> str:
        if offset < 0 or offset >= len(data):
            return ""
        end = data.find(b"\0", offset)
        if end < 0:
            end = len(data)
        return data[offset:end].decode("utf-8", errors="strict")

    def section_bytes(self, index: int) -> bytes:
        section = self.sections[index]
        start, size = int(section["offset"]), int(section["size"])
        if start + size > len(self.data):
            raise ElfError("section extends beyond ELF file")
        return self.data[start:start + size]

    def symbol(self, name: str) -> dict[str, int | str]:
        for symbol in self.symbols:
            if symbol["name"] == name:
                return symbol
        raise ElfError(f"missing linker symbol {name}")

    def image(self, size: int) -> bytes:
        result = bytearray(size)
        for index, section in enumerate(self.sections):
            if (int(section["flags"]) & SHF_ALLOC) == 0 or \
                    section["type"] != SHT_PROGBITS:
                continue
            start = int(section["addr"])
            data = self.section_bytes(index)
            if start + len(data) > size:
                raise ElfError(f"allocated section {section['name']} exceeds image")
            result[start:start + len(data)] = data
        return bytes(result)

    def absolute_relocations(self) -> set[int]:
        result: set[int] = set()
        for index, section in enumerate(self.sections):
            if section["type"] != SHT_REL or int(section["link"]) != self.symbol_table_index:
                continue
            target_index = int(section["info"])
            if target_index >= len(self.sections) or \
                    (int(self.sections[target_index]["flags"]) & SHF_ALLOC) == 0:
                # Debug sections also contain R_ARM_ABS32 entries, but their
                # offsets are relative to debug data and must never become
                # runtime package patches.
                continue
            raw = self.section_bytes(index)
            if int(section["entsize"]) != 8:
                raise ElfError("invalid ELF relocation table")
            for offset in range(0, len(raw), 8):
                patch, info = struct.unpack_from("<II", raw, offset)
                relocation_type = info & 0xFF
                symbol_index = info >> 8
                if relocation_type != R_ARM_ABS32:
                    continue
                if symbol_index >= len(self.symbols):
                    raise ElfError("relocation references an invalid symbol")
                shndx = int(self.symbols[symbol_index]["shndx"])
                if shndx not in (SHN_UNDEF, SHN_ABS):
                    result.add(patch)
        return result


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def read_string(image: bytes, offset: int, field: str) -> str:
    if offset >= len(image):
        raise ElfError(f"descriptor {field} pointer is outside the image")
    end = image.find(b"\0", offset, min(len(image), offset + 128))
    if end < 0:
        raise ElfError(f"descriptor {field} is not terminated")
    return image[offset:end].decode("utf-8", errors="strict")


def package(elf_path: pathlib.Path, output: pathlib.Path, package_uuid: uuid.UUID) -> None:
    elf = Elf32(elf_path)
    image_size = int(elf.symbol("__prism_image_end")["value"])
    descriptor = int(elf.symbol("__prism_descriptor_start")["value"])
    descriptor_end = int(elf.symbol("__prism_descriptor_end")["value"])
    got = int(elf.symbol("__prism_got_start")["value"])
    got_end = int(elf.symbol("__prism_got_end")["value"])
    rw_start = int(elf.symbol("__prism_rw_start")["value"])
    rw_data_end = int(elf.symbol("__prism_rw_data_end")["value"])
    rw_end = int(elf.symbol("__prism_rw_end")["value"])
    # With -msingle-pic-base, ARM's R_ARM_GOT_BREL values are offsets from the
    # first word of .got. GNU ld's synthetic _GLOBAL_OFFSET_TABLE_ symbol may
    # point at a trailing .got.plt-style marker and is not the value r9 needs.
    got_base = got
    if image_size <= 0 or descriptor_end - descriptor != 64:
        raise ElfError("cartridge must contain exactly one 64-byte descriptor")
    if not (0 <= got <= got_base <= got_end <= image_size == rw_start <=
            rw_data_end <= rw_end):
        raise ElfError("invalid cartridge GOT layout")

    linked_image = elf.image(rw_data_end)
    image = linked_image[:image_size]
    rw_init = linked_image[rw_start:rw_data_end]
    magic, abi, descriptor_size, flags, app_id = struct.unpack_from(
        "<IHHII", image, descriptor)
    if magic != 0x50524354 or abi != CARTRIDGE_ABI or descriptor_size != 64:
        raise ElfError("invalid prism_cartridge_t descriptor")
    slug_offset, name_offset = struct.unpack_from("<II", image, descriptor + 16)
    slug = read_string(image, slug_offset, "slug")
    name = read_string(image, name_offset, "name")
    if not slug or len(slug.encode()) > 31 or len(name.encode()) > 31:
        raise ElfError("cartridge slug/name must contain 1..31 UTF-8 bytes")
    persistent_size = struct.unpack_from("<I", image, descriptor + 56)[0]
    persistent_schema = struct.unpack_from("<H", image, descriptor + 60)[0]

    # Absolute pointers can live in the copied descriptor, the private GOT,
    # or initialized writable data (for example an array of structs containing
    # pointers to strings and assets).  Every one must be rebased at launch.
    absolute_relocations = elf.absolute_relocations()
    unsupported = sorted(
        offset for offset in absolute_relocations
        if 0 <= offset < image_size and not (
            descriptor <= offset < descriptor_end or
            got <= offset < got_end
        )
    )
    if unsupported:
        preview = ", ".join(f"0x{offset:x}" for offset in unsupported[:8])
        raise ElfError(
            "cartridge contains non-PIC absolute pointers in its read-only "
            f"image ({preview}); use the cartridge compiler-runtime wrappers"
        )

    relocations = {
        offset for offset in absolute_relocations
        if (descriptor <= offset < descriptor_end or
            got <= offset < got_end or
            rw_start <= offset < rw_data_end)
    }
    imports: list[tuple[int, int]] = []
    for offset in range(got, got_end, 4):
        value = struct.unpack_from("<I", image, offset)[0]
        if (value & 0xFFFF0000) == IMPORT_SENTINEL:
            symbol = value & 0xFFFF
            if symbol == 0:
                raise ElfError("invalid zero import symbol")
            imports.append((offset, symbol))
        elif value != 0:
            pointer = value & ~1
            if pointer >= image_size and not (rw_start <= pointer < rw_end):
                raise ElfError(f"GOT word at 0x{offset:x} is neither an image pointer nor an import")
            relocations.add(offset)

    for offset in relocations:
        if offset % 4 != 0 or not (
            descriptor <= offset < descriptor_end or
            got <= offset < got_end or
            rw_start <= offset < rw_data_end
        ):
            raise ElfError(
                f"runtime relocation 0x{offset:x} is outside descriptor/GOT templates"
            )

    relocation_count = len(relocations)
    import_count = len(imports)
    if relocation_count > 1024 or import_count > 256:
        raise ElfError("cartridge exceeds package relocation/import limits")

    relocations_offset = PACKAGE_HEADER_SIZE
    imports_offset = relocations_offset + relocation_count * 4
    image_offset = align(imports_offset + import_count * 8, 16)
    rw_offset = align(image_offset + image_size, 16)
    package_size = rw_offset + len(rw_init)
    header = bytearray(PACKAGE_HEADER_SIZE)
    struct.pack_into("<IHHHHI", header, 0, PACKAGE_MAGIC, PACKAGE_VERSION,
                     PACKAGE_HEADER_SIZE, CARTRIDGE_ABI, flags, package_size)
    header[16:32] = package_uuid.bytes
    struct.pack_into("<IIHH", header, 32, app_id, persistent_size,
                     persistent_schema, 0)
    header[44:44 + len(slug.encode())] = slug.encode()
    header[76:76 + len(name.encode())] = name.encode()
    struct.pack_into("<IIIIIIIIIII", header, 108,
                     image_offset, image_size, image_offset + descriptor,
                     image_offset + got, got_end - got,
                     got_base - got, relocations_offset, relocation_count,
                     imports_offset, import_count, U8G2_ABI_HASH)
    struct.pack_into("<III", header, 152, rw_offset, len(rw_init),
                     rw_end - rw_start)

    result = bytearray(package_size)
    result[:PACKAGE_HEADER_SIZE] = header
    for index, offset in enumerate(sorted(relocations)):
        if rw_start <= offset < rw_data_end:
            patch_offset = rw_offset + offset - rw_start
        else:
            patch_offset = image_offset + offset
        struct.pack_into("<I", result, relocations_offset + index * 4,
                         patch_offset)
    for index, (offset, symbol) in enumerate(imports):
        struct.pack_into("<IHH", result, imports_offset + index * 8,
                         image_offset + offset, symbol, 0)
    result[image_offset:image_offset + image_size] = image
    result[rw_offset:rw_offset + len(rw_init)] = rw_init
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    print(f"Packaged {name} -> {output} ({package_size} bytes, "
          f"{relocation_count} relocations, {import_count} imports)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--uuid", type=uuid.UUID, required=True)
    args = parser.parse_args()
    package(args.elf, args.output, args.uuid)


if __name__ == "__main__":
    main()
