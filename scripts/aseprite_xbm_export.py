import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Tuple


def sanitize_c_ident(name: str) -> str:
    out = []
    for ch in name:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        else:
            out.append("_")
    ident = "".join(out)
    if not ident:
        ident = "asset"
    if ident[0].isdigit():
        ident = f"_{ident}"
    return ident


def run_cmd(cmd: list[str], cwd: Path | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if proc.returncode != 0:
        quoted = " ".join(cmd)
        raise RuntimeError(f"Command failed ({proc.returncode}): {quoted}")


def _read_u16_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "little", signed=False)


def _read_u32_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little", signed=False)


def _read_i32_le(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little", signed=True)


def _srgb_luma_u8(r: int, g: int, b: int) -> int:
    return (299 * r + 587 * g + 114 * b) // 1000


def decode_bmp_to_mono_bits(
    bmp_data: bytes, negate: bool = True
) -> Tuple[int, int, List[int]]:
    if len(bmp_data) < 54:
        raise RuntimeError("BMP too small")
    if bmp_data[0:2] != b"BM":
        raise RuntimeError("Not a BMP file")

    pixel_offset = _read_u32_le(bmp_data, 10)
    dib_size = _read_u32_le(bmp_data, 14)
    if dib_size < 40:
        raise RuntimeError(f"Unsupported BMP DIB header size: {dib_size}")

    width = _read_i32_le(bmp_data, 18)
    height_signed = _read_i32_le(bmp_data, 22)
    planes = _read_u16_le(bmp_data, 26)
    bpp = _read_u16_le(bmp_data, 28)
    compression = _read_u32_le(bmp_data, 30)
    colors_used = _read_u32_le(bmp_data, 46) if dib_size >= 40 else 0

    if planes != 1:
        raise RuntimeError(f"Unsupported BMP planes: {planes}")
    if compression != 0:
        raise RuntimeError(f"Unsupported BMP compression: {compression}")
    if width <= 0 or height_signed == 0:
        raise RuntimeError(f"Invalid BMP dimensions: {width}x{height_signed}")
    if bpp not in (1, 4, 8, 24, 32):
        raise RuntimeError(f"Unsupported BMP bit depth: {bpp}")

    height = abs(height_signed)
    top_down = height_signed < 0

    row_stride = ((bpp * width + 31) // 32) * 4
    if pixel_offset + row_stride * height > len(bmp_data):
        raise RuntimeError("BMP pixel data is truncated")

    palette: List[Tuple[int, int, int]] = []
    if bpp <= 8:
        palette_count = colors_used if colors_used else (1 << bpp)
        palette_off = 14 + dib_size
        palette_end = palette_off + palette_count * 4
        if palette_end > len(bmp_data):
            raise RuntimeError("BMP palette is truncated")
        for i in range(palette_count):
            base = palette_off + i * 4
            b = bmp_data[base + 0]
            g = bmp_data[base + 1]
            r = bmp_data[base + 2]
            palette.append((r, g, b))

    def pixel_is_on(x: int, y: int) -> int:
        src_y = y if top_down else (height - 1 - y)
        row_base = pixel_offset + src_y * row_stride

        if bpp == 32:
            base = row_base + x * 4
            b = bmp_data[base + 0]
            g = bmp_data[base + 1]
            r = bmp_data[base + 2]
            a = bmp_data[base + 3]
            if a == 0:
                mono = 0
            else:
                mono = 1 if _srgb_luma_u8(r, g, b) < 128 else 0
        elif bpp == 24:
            base = row_base + x * 3
            b = bmp_data[base + 0]
            g = bmp_data[base + 1]
            r = bmp_data[base + 2]
            mono = 1 if _srgb_luma_u8(r, g, b) < 128 else 0
        elif bpp == 8:
            idx = bmp_data[row_base + x]
            r, g, b = palette[idx]
            mono = 1 if _srgb_luma_u8(r, g, b) < 128 else 0
        elif bpp == 4:
            byte = bmp_data[row_base + (x // 2)]
            idx = (byte >> 4) & 0x0F if (x % 2) == 0 else byte & 0x0F
            r, g, b = palette[idx]
            mono = 1 if _srgb_luma_u8(r, g, b) < 128 else 0
        else:  # bpp == 1
            byte = bmp_data[row_base + (x // 8)]
            bit = 7 - (x % 8)
            idx = (byte >> bit) & 0x01
            r, g, b = (
                palette[idx] if palette else ((0, 0, 0) if idx else (255, 255, 255))
            )
            mono = 1 if _srgb_luma_u8(r, g, b) < 128 else 0

        if negate:
            mono = 1 - mono
        return mono

    packed: List[int] = []
    bytes_per_row = (width + 7) // 8
    for y in range(height):
        row = [0] * bytes_per_row
        for x in range(width):
            if pixel_is_on(x, y):
                row[x // 8] |= 1 << (x % 8)
        packed.extend(row)

    return width, height, packed


def emit_xbm_c_block(var_name: str, width: int, height: int, bits: List[int]) -> str:
    lines = [
        f"#define {var_name}_width {width}",
        f"#define {var_name}_height {height}",
        f"static const uint8_t {var_name}_bits[] = {{",
    ]

    row: List[str] = []
    for i, b in enumerate(bits):
        row.append(f"0x{b:02X}")
        if len(row) >= 12 or i == len(bits) - 1:
            lines.append("    " + ", ".join(row) + ",")
            row = []

    lines.append("};")
    return "\n".join(lines)


def collect_generated_bmps(tmp_dir: Path, stem: str) -> list[Path]:
    # Aseprite will emit files like: <stem>_<tag>_<frame>.bmp
    # Keep a deterministic order for stable header output.
    return sorted(tmp_dir.glob(f"{stem}_*.bmp"), key=lambda p: p.name)


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    if path.exists() and path.read_bytes() == encoded:
        return
    path.write_bytes(encoded)


def generate_header(
    aseprite_file: Path,
    header_out: Path,
    aseprite_cli: str,
    typescript_out: Path | None = None,
    typescript_name: str | None = None,
) -> None:
    stem = sanitize_c_ident(aseprite_file.stem)

    tmp_dir = Path(tempfile.mkdtemp(prefix="aseprite_xbm_"))

    try:
        bmp_pattern = tmp_dir / f"{stem}_{{tag}}_{{frame}}.bmp"

        run_cmd(
            [
                aseprite_cli,
                "-b",
                str(aseprite_file),
                "--save-as",
                str(bmp_pattern),
            ]
        )

        bmp_files = collect_generated_bmps(tmp_dir, stem)
        if not bmp_files:
            raise RuntimeError(f"No BMP frames were generated for {aseprite_file}")

        rel_path = str(aseprite_file.relative_to(Path(__file__).parent.parent)).replace(
            "\\", "/"
        )

        chunks: list[str] = [
            "// Auto-generated by scripts/aseprite_xbm_export.py. Do not edit.",
            f"// Source: {rel_path}",
            "",
            "#pragma once",
            "#include <stdint.h>",
            "",
        ]

        frames: list[tuple[str, int, int, list[int]]] = []
        for bmp in bmp_files:
            var_name = sanitize_c_ident(bmp.stem)
            bmp_data = bmp.read_bytes()
            width, height, bits = decode_bmp_to_mono_bits(bmp_data)
            frames.append((var_name, width, height, bits))
            chunks.append(emit_xbm_c_block(var_name, width, height, bits))
            chunks.append("")

        header_text = "\n".join(chunks).rstrip() + "\n"
        write_if_changed(header_out, header_text)

        if typescript_out is not None:
            if len(frames) != 1:
                raise RuntimeError(
                    "TypeScript icon export requires exactly one frame"
                )
            if typescript_name is None or not re.fullmatch(
                r"[A-Za-z_$][A-Za-z0-9_$]*", typescript_name
            ):
                raise RuntimeError("TypeScript icon export requires a valid symbol name")
            _var_name, width, height, bits = frames[0]
            rows = []
            for offset in range(0, len(bits), 12):
                rows.append(
                    "  "
                    + ", ".join(
                        f"0x{byte:02x}" for byte in bits[offset : offset + 12]
                    )
                    + ","
                )
            typescript = (
                "// Auto-generated by scripts/aseprite_xbm_export.py. Do not edit.\n"
                f"// Source: {rel_path}\n\n"
                f"export const {typescript_name}Width = {width};\n"
                f"export const {typescript_name}Height = {height};\n"
                f"export const {typescript_name} = new Uint8Array([\n"
                + "\n".join(rows)
                + "\n]);\n"
            )
            write_if_changed(typescript_out, typescript)
    finally:
        if tmp_dir.exists():
            shutil.rmtree(tmp_dir, ignore_errors=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export one .aseprite file to one .h file containing XBM frame arrays"
    )
    parser.add_argument("--aseprite", required=True, help="Input .aseprite file")
    parser.add_argument(
        "--header-out", required=True, help="Output header file path (.h)"
    )
    parser.add_argument(
        "--aseprite-cli", required=True, help="Path or command name for Aseprite CLI"
    )
    parser.add_argument(
        "--typescript-out", help="Optional generated TypeScript icon file"
    )
    parser.add_argument("--typescript-name", help="Exported TypeScript icon symbol")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    aseprite_file = Path(args.aseprite).resolve()
    if not aseprite_file.is_file():
        print(f"Error: invalid .aseprite file: {aseprite_file}", file=sys.stderr)
        return 2

    header_out = Path(args.header_out).resolve()

    try:
        generate_header(
            aseprite_file=aseprite_file,
            header_out=header_out,
            aseprite_cli=args.aseprite_cli,
            typescript_out=(
                Path(args.typescript_out).resolve()
                if args.typescript_out
                else None
            ),
            typescript_name=args.typescript_name,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
