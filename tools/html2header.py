#!/usr/bin/env python3
"""Convert web/index.html into src/index_html.h (PROGMEM byte array)."""
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
html = (root / "web" / "index.html").read_bytes()

lines = []
for i in range(0, len(html), 20):
    chunk = html[i:i + 20]
    lines.append(", ".join(f"0x{b:02X}" for b in chunk))

out = pathlib.Path(root / "src" / "index_html.h")
out.write_text(
    "// Auto-generated from web/index.html by tools/html2header.py - do not edit by hand.\n"
    "#pragma once\n\n#include <pgmspace.h>\n\n"
    f"const unsigned int INDEX_HTML_LEN = {len(html)};\n\n"
    "const unsigned char INDEX_HTML[] PROGMEM = {\n"
    + ",\n".join(lines)
    + ", 0x00\n};\n"
)

print(f"index_html.h written: {len(html)} bytes")
