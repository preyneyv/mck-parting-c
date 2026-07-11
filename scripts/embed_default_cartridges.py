#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("cartridges", nargs="+")
    args = parser.parse_args()

    lines = [".section .rodata.prism_defaults,\"a\",%progbits", ".balign 4"]
    for value in args.cartridges:
        name, raw_path = value.split("=", 1)
        path = Path(raw_path).resolve().as_posix()
        lines += [
            f".global prism_default_{name}_start",
            f".global prism_default_{name}_end",
            f"prism_default_{name}_start:",
            f'.incbin "{path}"',
            f"prism_default_{name}_end:",
            ".balign 4",
        ]
    Path(args.output).write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
