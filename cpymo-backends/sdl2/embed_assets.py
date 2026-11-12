#!/usr/bin/env python3
"""把 WAV 资源生成成 C 数组，让单文件分发（.exe / 单二进制 / .app）不必外带资源。

用法: embed_assets.py <输出.c> [名字=路径 ...]
"""
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: embed_assets.py <out.c> [name=path ...]")

    out = sys.argv[1]
    items = []
    for spec in sys.argv[2:]:
        name, path = spec.split("=", 1)
        with open(path, "rb") as f:
            items.append((name, f.read()))

    lines = [
        "/* 由 embed_assets.py 生成，请勿手工修改。 */",
        '#include "cpymo_embedded_assets.h"',
        "",
    ]
    for i, (_name, data) in enumerate(items):
        lines.append("static const unsigned char cpymo_asset_%d[] = {" % i)
        row = []
        for b in data:
            row.append(str(b))
            if len(row) == 24:
                lines.append("  " + ",".join(row) + ",")
                row = []
        if row:
            lines.append("  " + ",".join(row) + ",")
        lines.append("};")
        lines.append("")

    lines.append("const struct cpymo_embedded_asset_entry cpymo_embedded_asset_table[] = {")
    for i, (name, data) in enumerate(items):
        lines.append('  { "%s", cpymo_asset_%d, %du },' % (name, i, len(data)))
    if not items:
        lines.append("  { 0, 0, 0 },")
    lines.append("};")
    lines.append("")
    lines.append("const unsigned cpymo_embedded_asset_count = %du;" % len(items))
    lines.append("")

    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
