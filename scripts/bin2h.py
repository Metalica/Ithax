#!/usr/bin/env python3
"""Convert a binary file into a C++ header with an embedded byte array.

Usage: bin2h.py <input> <output> <symbol-name>
"""

import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: bin2h.py <input> <output> <symbol-name>", file=sys.stderr)
        return 2

    input_path, output_path, symbol = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(input_path, "rb") as handle:
        contents = handle.read()

    lines = [f"static const unsigned char {symbol}[] = {{"]
    for index in range(0, len(contents), 16):
        chunk = contents[index : index + 16]
        line = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append("    " + line + ",")
    lines.append("};")
    lines.append(f"static const unsigned int {symbol}_size = {len(contents)}u;")
    lines.append("")

    with open(output_path, "w", encoding="ascii") as handle:
        handle.write("\n".join(lines))

    return 0


if __name__ == "__main__":
    sys.exit(main())
