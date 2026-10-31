#!/usr/bin/env python3
"""Install the versioned C ABI addition into our pinned, dedicated SDK checkout."""
import pathlib
import shutil
import sys

root = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(__file__).resolve().parent / "impeller"
interop = root / "engine/src/flutter/impeller/toolkit/interop"
build = interop / "BUILD.gn"
text = build.read_text(encoding="utf-8")
original = 'sources = [ "impeller.cc" ]'
patched = 'sources = [ "impeller.cc", "launchme_impeller.cc", "launchme_impeller.h" ]'
if patched not in text:
    if text.count(original) != 1:
        raise SystemExit("Unexpected Impeller BUILD.gn; use the pinned SDK revision.")
    build.write_text(text.replace(original, patched), encoding="utf-8", newline="\n")
for name in ("launchme_impeller.h", "launchme_impeller.cc"):
    shutil.copyfile(source / name, interop / name)
