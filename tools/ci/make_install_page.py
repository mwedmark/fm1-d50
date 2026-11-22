#!/usr/bin/env python3
"""The D-50's web installer (issue #45): fm1-base's installer page (web/installer.html) with this build's firmware package.

  make_install_page.py FM1_BASE_DIR APP_DIR PACKAGE.fwsc VERSION OUT_DIR

Writes OUT_DIR/install/index.html and OUT_DIR/firmware/d50-VERSION.fwsc (what tools/make_site.py in fm1-base does for its own
sites, without the front page, the manual and the emulator: the emulator is the site's root here). The package carries no
Roland data (the workflow builds it without the resource region), so the page installs the firmware only; the D-50's data
is put in by the person who has it (INSTALL.md). Needs fm1-base at the commit tools/ci/fetch_platform.sh checked out.
"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

base, app, pkg, version, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4], Path(sys.argv[5])
sys.path.insert(0, str(base / "tools"))
import make_site as M  # noqa: E402  (fm1-base's: strip_module, product_of, the version of the installer's inlined libraries)
from build import read_conf  # noqa: E402

conf = read_conf(app / "fm1.toml")
raw = pkg.read_bytes()
product = M.product_of(raw)
prefix = conf["id_prefix"]
if not re.fullmatch(re.escape(prefix) + r"(\d{2}|\d{6})", product):   # a dev build is the prefix and 00, a release six digits
    raise SystemExit(f"{pkg}: identity {product!r}, expected {prefix} and two or six digits")
if b"FELUCCA-LOADER-1" not in raw:
    raise SystemExit(f"{pkg}: no update loader in it")
name = f"d50-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"

for d in ("install", "firmware"):
    (out / d).mkdir(parents=True, exist_ok=True)
shutil.copy(pkg, out / "firmware" / name)
for lic in sorted((base / "LICENSES").glob("*.txt")):     # the licences of the package's parts under other licences, beside it
    shutil.copy(lic, out / "firmware" / lic.name)

html = (base / "web" / "installer.html").read_text(encoding="utf-8")
lib = "\n".join(M.strip_module((base / "web" / f).read_text(encoding="utf-8")) for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name, "backup": bool(conf.get("backup")),
                   "res": None, "boot": None})
for mark in ("/*LIB*/", "/*META*/", "/*APP*/"):
    if html.count(mark) != 1:
        raise SystemExit(f"web/installer.html must contain {mark} once")
repo = conf.get("repo", "")
for key, val in (("RES", conf.get("resources_what", "the D-50's data")), ("RES_JA", "D-50 のデータ"), ("NAME", conf["name"]),
                 ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)), ("LICENSE", conf.get("licence", "GPL-3.0-or-later")),
                 ("CREDITS", " The D-50 engine is PicoFaceD5 by Michi71. The D-50's data is Roland's and is not part of this page.")):
    html = html.replace("{{" + key + "}}", val)
(out / "install" / "index.html").write_text(html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*APP*/", "null"),
                                            encoding="utf-8")
print(f"install page: {out / 'install' / 'index.html'} ({product}, {name}, sha256 {hashlib.sha256(raw).hexdigest()[:12]})")
