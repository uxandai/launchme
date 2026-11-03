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
    text = text.replace(original, patched)

# libimpeller.so exports only its C API (launchme_impeller.map): without it the statically linked
# libunwind's _Unwind_* replace the system unwinder in the launcher's process.
shared = 'target_type = "shared_library"\n\n  output_name = "impeller"\n'
hidden = shared + ('\n  if (is_linux) {\n    ldflags = [ "-Wl,--version-script=" +\n'
                   '               rebase_path("launchme_impeller.map", root_build_dir) ]\n  }\n')
if hidden not in text:
    if text.count(shared) != 1:
        raise SystemExit("Unexpected Impeller BUILD.gn; use the pinned SDK revision.")
    text = text.replace(shared, hidden)
build.write_text(text, encoding="utf-8", newline="\n")
for name in ("launchme_impeller.h", "launchme_impeller.cc", "launchme_impeller.map"):
    shutil.copyfile(source / name, interop / name)
