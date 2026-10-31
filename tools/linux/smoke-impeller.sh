#!/usr/bin/env bash
# Native application smoke/captures on a private X server and a fresh, offline profile.
# Usage: smoke-impeller.sh <build-dir> <capture-dir> <0|1> <gl|vulkan> <desktop|tv>
# Run in a network namespace for automated UI verification (see docs/IMPELLER.md).
set -euo pipefail
if [[ "${LM_PRIVATE_DISPLAY:-0}" != 1 ]]; then
    exec xvfb-run -a -s '-screen 0 1280x800x24' env LM_PRIVATE_DISPLAY=1 bash "$0" "$@"
fi
build="$(realpath "$1")"; shots="$(realpath -m "$2")"
engine="$3"; transport="$4"; mode="$5"
mkdir -p "$shots"
# Keep the data path short: the launcher's UNIX-domain single-instance socket has a path limit.
profile="$(mktemp -d /tmp/lm-impeller-XXXXXX)"
export LAUNCHME_CONFIG_DIR="$profile" XDG_DATA_HOME="$profile/data" XDG_CONFIG_HOME="$profile/config" XDG_CACHE_HOME="$profile/cache"
export STEAM_DIR="$profile/steam" SDL_VIDEO_DRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LAUNCHME_RENDERER="$transport"
unset LAUNCHME_IMPELLER WAYLAND_DISPLAY
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/launchme-smoke-no-session-bus
tv=0; [[ "$mode" == tv ]] && tv=1
printf 'impeller=%s\nlaunch_tv_mode=%s\ntour_done=1\naurelia_intro_done=1\nsteam_enabled=0\nshow_emulation=0\npc_platforms=\ncompanion=0\nglance=0\nupdate_check=0\nscreensaver=0\nmusic_track=none\nboot_sound=0\n' "$engine" "$tv" > "$profile/settings.ini"
args=(); [[ "$mode" == tv ]] && args+=(--jump=settings)
"$build/launchme" "${args[@]}" > "$profile/run.log" 2>&1 &
pid=$!
cleanup() { if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid"; wait "$pid" || true; fi; pid=""; }
trap cleanup EXIT
wid=""
for ((i=0; i<60; ++i)); do
    kill -0 "$pid" 2>/dev/null || { cat "$profile/run.log"; exit 1; }
    wid="$(xdotool search --onlyvisible --pid "$pid" 2>/dev/null || true)"
    [[ -n "$wid" ]] && break
    sleep 0.25
done
[[ -n "$wid" ]] || { echo 'No launcher window'; cat "$profile/run.log"; exit 1; }
wid="${wid%%$'\n'*}"
sleep "${LM_SMOKE_WAIT:-6}"
name="app-${engine}-${transport}-${mode}"
xdotool windowfocus "$wid"
if [[ "$mode" == desktop ]]; then
    width="$(xdotool getwindowgeometry --shell "$wid" | sed -n 's/^WIDTH=//p')"
    xdotool mousemove --window "$wid" "$((width - 166))" 24 click 1
    sleep 1
fi
import -window "$wid" "$shots/$name-settings.png"
if [[ -n "${LM_SMOKE_SYSTEM_X:-}" && -n "${LM_SMOKE_SYSTEM_Y:-}" ]]; then
    xdotool mousemove --window "$wid" "$LM_SMOKE_SYSTEM_X" "$LM_SMOKE_SYSTEM_Y" click 1
    sleep 1
    if [[ "${LM_SMOKE_SCROLL:-0}" == 1 ]]; then
        xdotool mousemove --window "$wid" 900 550 click --repeat 12 --delay 50 5
        sleep 1
    fi
    import -window "$wid" "$shots/$name-system.png"
    if [[ -n "${LM_SMOKE_TOGGLE_X:-}" && -n "${LM_SMOKE_TOGGLE_Y:-}" ]]; then
        xdotool mousemove --window "$wid" "$LM_SMOKE_TOGGLE_X" "$LM_SMOKE_TOGGLE_Y" click 1
        sleep 1
        import -window "$wid" "$shots/$name-toggled.png"
        cleanup
        expected="$((1 - engine))"
        grep -qx "impeller=$expected" "$profile/settings.ini" || { echo 'UI toggle did not persist'; exit 1; }
        "$build/launchme" > "$profile/restart.log" 2>&1 &
        pid=$!
        wid=""
        for ((i=0; i<60; ++i)); do
            kill -0 "$pid" 2>/dev/null || { cat "$profile/restart.log"; exit 1; }
            wid="$(xdotool search --onlyvisible --pid "$pid" 2>/dev/null || true)"
            [[ -n "$wid" ]] && break
            sleep 0.25
        done
        [[ -n "$wid" ]] || { echo 'No restarted window'; exit 1; }
        wid="${wid%%$'\n'*}"
        sleep "${LM_SMOKE_WAIT:-6}"
        import -window "$wid" "$shots/$name-restarted.png"
        if [[ "$expected" == 1 ]]; then
            grep -q 'launchme gpu: Impeller ' "$profile/restart.log" || { cat "$profile/restart.log"; exit 1; }
        else
            if grep -q 'launchme gpu: Impeller ' "$profile/restart.log"; then cat "$profile/restart.log"; exit 1; fi
        fi
        cp "$profile/restart.log" "$shots/$name-restarted.log"
        echo "UI switch $engine -> $expected saved and applied after restart"
    fi
fi
xdotool windowfocus "$wid"
xdotool key --window "$wid" Escape
sleep 1
import -window "$wid" "$shots/$name.png"
if grep -q 'Impeller frame recording failed' "$profile/run.log"; then cat "$profile/run.log"; exit 1; fi
if [[ "$engine" == 1 ]]; then
    grep -q 'launchme gpu: Impeller ' "$profile/run.log" || { cat "$profile/run.log"; exit 1; }
else
    if grep -q 'launchme gpu: Impeller ' "$profile/run.log"; then cat "$profile/run.log"; exit 1; fi
fi
echo "Captured $name; profile/log: $profile"
cp "$profile/run.log" "$shots/$name.log"
