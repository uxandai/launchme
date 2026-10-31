#!/usr/bin/env bash
# Build the pinned standalone SDK and Launchme's small C ABI extension on Linux.
# IMPELLER_ROOT defaults to ~/launchme-impeller; --fetch-only prepares dependencies.
set -euo pipefail
revision=f6965468e5902c28fb719793f5da3e7e812870fb
root="${IMPELLER_ROOT:-$HOME/launchme-impeller}"
tools="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$root"
if [[ ! -d "$root/depot_tools/.git" ]]; then
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$root/depot_tools"
fi
export PATH="$root/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0
if [[ ! -d "$root/flutter/.git" ]]; then
    git clone --depth 1 https://github.com/flutter/flutter.git "$root/flutter"
    git -C "$root/flutter" fetch --depth 1 origin "$revision"
    git -C "$root/flutter" checkout --detach "$revision"
fi
if [[ "$(git -C "$root/flutter" rev-parse HEAD)" != "$revision" ]]; then
    echo "Expected Flutter $revision at $root/flutter; use a separate IMPELLER_ROOT." >&2
    exit 1
fi
cp "$tools/impeller.gclient" "$root/flutter/.gclient"
cd "$root/flutter"
gclient sync --no-history -j "${IMPELLER_JOBS:-8}"
if [[ "${1:-}" == --fetch-only ]]; then exit 0; fi
engine="$root/flutter/engine/src"
python3 "$tools/prepare-impeller.py" "$root/flutter"
python3 "$engine/flutter/tools/gn" --runtime-mode release --no-lto --no-rbe \
    --disable-desktop-embeddings --no-build-engine-artifacts --no-enable-unittests \
    --target-dir launchme_impeller
"${NINJA:-/usr/bin/ninja}" -C "$engine/out/launchme_impeller" -j "${IMPELLER_JOBS:-8}" \
    flutter/impeller/toolkit/interop:shared_library
mkdir -p "$root/sdk/include" "$root/sdk/lib"
cp "$engine/flutter/impeller/toolkit/interop/impeller.h" "$root/sdk/include/"
cp "$tools/impeller/launchme_impeller.h" "$root/sdk/include/"
cp "$engine/out/launchme_impeller/libimpeller.so" "$root/sdk/lib/"
cp "$engine/flutter/sky/packages/sky_engine/LICENSE" "$root/sdk/LICENSE.txt"
printf '%s\n' "$revision" > "$root/sdk/revision.txt"
echo "Impeller SDK: $root/sdk"
