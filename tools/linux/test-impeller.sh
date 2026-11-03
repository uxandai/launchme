#!/usr/bin/env bash
# Isolated WSL/native-Linux build of the branch, including both rendering backends.
# Usage: test-impeller.sh [build|test|all] [optional build targets...]
set -euo pipefail
mode="${1:-all}"; if [[ $# -gt 0 ]]; then shift; fi
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
root="${LM_IMPELLER_TEST_ROOT:-$HOME/lm-impeller}"
[[ "$root" != / && "$root" != "$HOME" ]] || { echo "Select a dedicated build workspace" >&2; exit 1; }
mkdir -p "$root/src"
if [[ "$mode" != test ]]; then
    rsync -a --exclude '/build/' --exclude '/.git/' --exclude '/_llm_docs/' --exclude '/.opencode/' \
        --exclude '/local/' --exclude '/.openai_token' --exclude '*_api_key.txt' --exclude '*_token.*' \
        "$repo/" "$root/src/"
    cmake -S "$root/src" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DLAUNCHME_IMPELLER_DIR="${IMPELLER_ROOT:-$HOME/launchme-impeller}/sdk"
    if [[ $# -gt 0 ]]; then
        cmake --build "$root/build" --parallel "${LM_JOBS:-8}" --target "$@"
    else
        cmake --build "$root/build" --parallel "${LM_JOBS:-8}"
    fi
fi
if [[ "$mode" != build ]]; then
    ctest --test-dir "$root/build" --output-on-failure -R '^impeller_' -j 1
    if command -v xvfb-run >/dev/null; then
        xvfb-run -a -s '-screen 0 1280x800x24' env LANG=C.UTF-8 SDL_VIDEO_DRIVER=x11 \
            LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe WEBKIT_DISABLE_DMABUF_RENDERER=1 \
            ctest --test-dir "$root/build" --output-on-failure -j 4
    else
        ctest --test-dir "$root/build" --output-on-failure -j 4
    fi
fi
