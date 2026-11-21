#!/usr/bin/env python3
"""d5_pcm_table.h without the ROM, for the build on GitHub (issue #36).

    python3 tools/ci/make_pcm_table.py <outdir>

PicoFaceD5's tools/d5_extract/d5_make_blob.py writes the table beside the decoded PCM, but the table itself comes from the committed
d5_sample_table.json, not from the ROM: every entry there carries its own root_hz, so the ROM is only read for the PCM. This writes the
same header from the JSON alone (the same rows and header text as d5_make_blob.py), so the firmware builds without any Roland data.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXTRACT = os.path.join(HERE, "..", "..", "third_party", "picofaced5", "tools", "d5_extract")
sys.path.insert(0, EXTRACT)
from d5_make_blob import HEADER  # noqa: E402
from d5_rom import SAMPLE_RATE  # noqa: E402

PCM_WORDS = 262144  # the D-50's 512 KiB of PCM, in 16-bit words (d5_make_blob.py: len(rs.audio))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    table = json.load(open(os.path.join(EXTRACT, "d5_sample_table.json")))["samples"]
    rows = []
    for e in table:
        if e["start"] is None:
            rows.append(f'    {{ 0, 0, true, 0.f, "{e["name"][:6]}" }},'
                        f'  // {e["pcm"]:3} {e["basis"]}')
        else:
            f0 = e.get("root_hz")
            if f0 is None:
                sys.exit(f'PCM {e["pcm"]} has no root_hz in d5_sample_table.json: d5_make_blob.py would read it from the ROM')
            rows.append(f'    {{ {e["start"]:6}, {e["end"] - e["start"]:6}, '
                        f'{"true " if e["looped"] else "false"}, {f0:8.2f}f, '
                        f'"{e["name"][:6]}" }},'
                        f'  // {e["pcm"]:3} {e["basis"]}')
    with open(os.path.join(outdir, "d5_pcm_table.h"), "w") as f:
        f.write(HEADER.format(rate=SAMPLE_RATE, words=PCM_WORDS, count=len(table), rows="\n".join(rows)))
    print(f"wrote {os.path.join(outdir, 'd5_pcm_table.h')} ({len(table)} samples, from the JSON)")


if __name__ == "__main__":
    main()
