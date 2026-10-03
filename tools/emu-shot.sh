#!/usr/bin/env bash
# Screenshot the running emulator window on macOS.
#
#   tools/emu-shot.sh [--out FILE] [--settle SECS] [--timeout SECS] [--keep] [FILE.3dsx]
#
# Azahar has no --screenshot flag, and --dump-video writes nothing on macOS, so
# the only route is to capture the emulator's window from the screen. That needs
# two things this script exists to get right:
#
#   * The .3dsx must be opened through LaunchServices (`open -a`). Run as
#     `MacOS/azahar file.3dsx`, Azahar ignores the argument, never opens a
#     window and sits on its HOME menu -- there is nothing to capture.
#   * The window is found through System Events, so this needs Accessibility
#     permission for the terminal running it, and `screencapture` needs Screen
#     Recording permission. Grant both in System Settings > Privacy & Security.
#
# The capture is Retina: a 1706x752 window yields a 3412x1504 PNG.
#
# Emulator-only. A screenshot here says nothing about real hardware.
set -euo pipefail

EMU="${EMU:-$HOME/Applications/Azahar.app/Contents/MacOS/azahar}"
out=""
settle=6
# Azahar takes ~30s to open its window on a Mac (MoltenVK init), so the default
# wait is generous. Polling for the window is the only reliable signal that it
# is up; there is no output to watch and no log is written.
timeout=120
keep=0
three_dsx=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--emu) EMU="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--settle) settle="$2"; shift 2 ;;
		--timeout) timeout="$2"; shift 2 ;;
		--keep) keep=1; shift ;;
		-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
		-*) echo "emu-shot: unknown option $1" >&2; exit 2 ;;
		*) three_dsx="$1"; shift ;;
	esac
done

top="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
[[ -n "$out" ]] || out="$top/build-host/emu-shots/shot-$(date +%Y%m%d-%H%M%S).png"
mkdir -p "$(dirname "$out")"

# The emulator binary lives at <bundle>/Contents/MacOS/<name>; `open -a` wants
# the bundle. Same guess the Makefile's run-emu target makes.
app="${EMU_APP:-}"
if [[ -z "$app" ]]; then
	d="$(dirname "$EMU")"
	d="$(dirname "$d")"
	d="$(dirname "$d")"
	[[ -f "$d/Contents/Info.plist" ]] && app="$d"
fi

[[ -x "$EMU" ]] || { echo "emu-shot: emulator not found at $EMU" >&2; exit 1; }
[[ -n "$app" ]] || { echo "emu-shot: $EMU is not in an app bundle; cannot use open -a" >&2; exit 1; }

# The process name is the *binary* basename, not the bundle's: Azahar.app holds
# MacOS/azahar, and System Events matches "azahar". Using the bundle name makes
# every lookup miss, and the window poll then spins until it times out.
emu_name="$(basename "$EMU")"

window_geometry() {
	osascript <<-'EOF' 2>/dev/null || true
	tell application "System Events"
		if not (exists process "PROCESS") then return ""
		tell process "PROCESS"
			set ws to every window
			if (count of ws) is 0 then return ""
			set p to position of item 1 of ws
			set s to size of item 1 of ws
			return ((item 1 of p) as string) & " " & ((item 2 of p) as string) & " " & ((item 1 of s) as string) & " " & ((item 2 of s) as string)
		end tell
	end tell
	EOF
}

# A stale emulator from a previous run must be gone before launching: `open -a`
# activates an existing instance instead of starting a new one, and an instance
# that never opened a window would otherwise be captured (or waited on) forever.
#
# Terminated with a signal rather than `tell application ... to quit`: that
# needs an Apple Events grant for the emulator and blocks on the permission
# dialog when it has not been approved.
stop_emulator() {
	pgrep -f "MacOS/$emu_name" >/dev/null 2>&1 || return 0
	pkill -f "MacOS/$emu_name" 2>/dev/null || true
	for ((i = 0; i < 15; i++)); do
		pgrep -f "MacOS/$emu_name" >/dev/null 2>&1 || break
		sleep 1
	done
	pgrep -f "MacOS/$emu_name" >/dev/null 2>&1 && pkill -9 -f "MacOS/$emu_name" 2>/dev/null || true
	# Relaunching immediately after the kill tends to produce an instance that
	# runs but never opens a window. Let the window server let go first.
	sleep 5
}

launch() {
	stop_emulator
	if [[ -n "$three_dsx" ]]; then
		open -a "$app" "$three_dsx"
	else
		open -a "$app"
	fi
}

# With the screen at the login window (session locked, or no GUI session
# attached) the emulator still runs and still renders -- its log shows Vulkan
# coming up and the game executing -- but it never maps a window, so there is
# nothing to capture and the wait below just burns its timeout. Detect that up
# front and say so.
session_locked() {
	local with_windows
	with_windows="$(osascript <<-'EOF' 2>/dev/null || true
	tell application "System Events"
		set out to ""
		repeat with p in processes
			try
				if (count of windows of p) > 0 then set out to out & (name of p) & " "
			end try
		end repeat
		return out
	end tell
	EOF
	)"
	with_windows="${with_windows//loginwindow/}"
	[[ -z "${with_windows// /}" ]]
}

if session_locked; then
	echo "emu-shot: no application has a window (only the login window does), so" >&2
	echo "  the emulator would run and render but never map a window to capture." >&2
	echo "  Unlock the screen, or run this from a GUI session, and try again." >&2
	exit 1
fi

wait_for_window() {
	local i
	geom=""
	for ((i = 0; i < timeout; i++)); do
		geom="$(window_geometry | sed "s/PROCESS/$emu_name/")"
		[[ -n "$geom" ]] && return 0
		sleep 1
	done
	return 1
}

if [[ -n "$three_dsx" ]]; then
	[[ -f "$three_dsx" ]] || { echo "emu-shot: no such file: $three_dsx" >&2; exit 1; }
	# Absolute: a relative path can leave Azahar on its HOME menu with no window.
	three_dsx="$(cd "$(dirname "$three_dsx")" && pwd)/$(basename "$three_dsx")"
fi

# Azahar does not always open its window on the first launch. Retry once from a
# clean state rather than failing the run.
for attempt in 1 2; do
	if pgrep -f "MacOS/$emu_name" >/dev/null 2>&1; then
		echo "emu-shot: stopping the running $emu_name first"
	fi
	launch
	if wait_for_window; then
		break
	fi
	if (( attempt == 1 )); then
		echo "emu-shot: no window after ${timeout}s; restarting $emu_name" >&2
	fi
done

if [[ -z "$geom" ]]; then
	stop_emulator
	echo "emu-shot: no window after ${timeout}s on two attempts." >&2
	echo "  If the .3dsx was passed on the command line instead of via open -a," >&2
	echo "  Azahar sits on its HOME menu with no window. Check Screen Recording" >&2
	echo "  permission for your terminal too." >&2
	exit 1
fi

read -r x y w h <<<"$geom"

# Give the emulator time to render past its first frames.
sleep "$settle"

# -x: no sound. -R: capture just this window's rectangle.
if ! screencapture -x -R"$x,$y,$w,$h" -o "$out"; then
	echo "emu-shot: screencapture failed; Screen Recording permission is likely missing" >&2
	exit 1
fi

[[ -s "$out" ]] || { echo "emu-shot: $out is empty" >&2; exit 1; }

if (( ! keep )); then
	stop_emulator
fi

echo "$out"