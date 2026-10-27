#!/bin/bash
# The package in Chimera's own window (run headless, no
# display), which the headless runner cannot show:
#  - the test program (tests/make-nds-program.py) runs in the window and
#    leaves the Main RAM the headless runner leaves at the same frame, with
#    the two screens as one picture of 256x384;
#  - the keys the package ships become Chimera's keys for the console;
#  - with a game (NDS_GAME, as waterbox/run-gate.sh takes it): its project
#    runs in the window and leaves the headless runner's Main RAM;
#  - with the console's BIOS and firmware files (NDS_BIOS7, NDS_BIOS9,
#    NDS_FIRMWARE): a project that starts from the console's menu takes them
#    from Chimera's firmware, and without them it is refused and a file is
#    named;
#  - with a DSi game and a DSi's four files (DSI_GAME, DSI_BIOS7, DSI_BIOS9,
#    DSI_FIRMWARE, DSI_NAND): a DSi project takes them from Chimera's
#    firmware, runs in the window and leaves the headless runner's Main RAM.
#
# Usage: tests/run-frontend.sh --chimera-root <chimera checkout> [--frames N] [--keep]
#   --keep   leave the work folder (logs, configurations, projects) and say where it is
# Needs: Chimera built with its window (its tools/ci-chimera.sh), and the
# package installed in that checkout (waterbox/build-package.sh -r <chimera>).
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
wb="$root/waterbox"
frames=300
chimera_root=""
keep=""
while [ $# -gt 0 ]; do
	case "$1" in
		--keep) keep=1 ;;
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "say where Chimera is: --chimera-root <chimera checkout>" >&2; exit 2; }
chimera_root="$(cd "$chimera_root" && pwd)"
game="${NDS_GAME:-$here/games-local/nsmb}"
bios7="${NDS_BIOS7:-$here/roms-local/bios7.bin}"
bios9="${NDS_BIOS9:-$here/roms-local/bios9.bin}"
firmware="${NDS_FIRMWARE:-$here/roms-local/firmware.bin}"
dsigame="${DSI_GAME:-$here/games-local/dsi}"
bios7i="${DSI_BIOS7:-$here/roms-local/bios7i.bin}"
bios9i="${DSI_BIOS9:-$here/roms-local/bios9i.bin}"
dsifirmware="${DSI_FIRMWARE:-$here/roms-local/dsi_firmware.bin}"
dsinand="${DSI_NAND:-$here/roms-local/dsi_nand.bin}"

chimera_exe="$chimera_root/build/meson-linux/source/ui/chimera"
package="$chimera_root/build/Cores/melonds.chimeraCore"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
runner="$chimera_root/build/meson-linux/chimera-run"
[ -x "$chimera_exe" ] || { echo "Chimera's window is not built: $chimera_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "the package is not installed: $package (run waterbox/build-package.sh -r $chimera_root)" >&2; exit 1; }
[ -x "$runwbx" ] && [ -f "$core" ] || { echo "the sandbox build is not there: $runwbx, $core" >&2; exit 1; }
[ -x "$runner" ] || { echo "Chimera's headless runner is not built: $runner" >&2; exit 1; }

work="$(mktemp -d)"
cleanup() {
	if [ -n "$keep" ]; then echo "work folder kept: $work"; else rm -rf "$work"; fi
}
trap cleanup EXIT

# Chimera's window, run headless (it needs no display): the cores installed in
# the checkout, its settings, cache and recovery files the run's own, and
# nothing carried over from a Chimera installed on this machine
# the native test runner (run-wbx) needs miniBox's library, which Chimera's build holds
export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$chimera_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" CHIMERA_QT_RECOVERY="$work/qt-recovery" \
	CHIMERA_QT_HASHES="$work/qt-file-hashes.txt" CHIMERA_QT_CHIMERA_SETTINGS="$work/no-chimera-config.ini"


ok=0
failed=0
skipped=0
report() {
	printf '%-16s %-5s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; SKIP) skipped=$((skipped + 1)) ;; *) failed=$((failed + 1)) ;; esac
}

# make_project <tag> <frames> <game file> [<setting>=<value> | press=... | firmware=<id>=<file>]...
# A project with the game beside it, where Chimera looks, and a movie of
# that many frames over the console's controls.
make_project() {
	local tag="$1" nframes="$2" gamefile="$3"; shift 3
	local dir="$work/$tag"
	mkdir -p "$dir"
	cp "$gamefile" "$dir/"
	python3 "$here/make-project.py" "$package" "$dir/$tag.chimeraProject" "$nframes" "panel=$work/panel.out" "$@" \
		-- "cart=$dir/$(basename "$gamefile")" > "$work/$tag.log" 2>&1
}
# run_frontend <tag> <configuration> <frames> <domain or "">: the project in the window
run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" domain="$4" job="$work/job.$1.txt"
	{
		echo "frames=$nframes"
		echo "out=$work/$tag.ram.bin"
		echo "meta=$work/$tag.meta.txt"
		echo "shot="
		echo "domain=$domain"
	} > "$job"
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout -s KILL 900 "$chimera_exe" --headless \
		"--config=$cfg" "--project=$work/$tag/$tag.chimeraProject" "--lua=$here/${lua_script:-frontend-ram.lua}" ) >> "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}
value() { sed -n "s/^$2=//p" "$work/$1.meta.txt" 2>/dev/null; }

python3 "$here/settings-config.py" "$work/settings.base.json" '{}'

# ---- the test program: needs no file of anyone's
mkdir -p "$work/prog" "$work/probe"
python3 "$here/make-nds-program.py" "$work/prog/chimera-test.nds" > /dev/null
ln -sf "$work/prog/chimera-test.nds" "$work/probe/chimera-test.nds"
printf '{"cart":["chimera-test.nds"],"savedata":[]}' > "$work/probe/slots"; printf '{}' > "$work/probe/settings"
"$runwbx" "$core" "$work/probe" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null
# the movie is longer than the run: a window whose movie ends stops there
make_project ds $((frames + 100)) "$work/prog/chimera-test.nds" "press=A:100:$frames"
"$runner" --project "$work/ds/ds.chimeraProject" "$package" --files "$work/ds" --frames "$frames" \
	--dump "Main RAM=$work/engine.ram.bin" > "$work/engine.log" 2>&1
if ! run_frontend ds "$work/settings.base.json" "$frames" "Main RAM"; then
	report "window:nds" FAIL "Chimera did not finish the run: $(value ds detail) $(tail -2 "$work/ds.log" | tr '\n' ' ' | cut -c1-200)"
elif [ "$(value ds frames)" != "$frames" ] || [ "$(value ds width)" != 256 ] || [ "$(value ds height)" != 384 ]; then
	report "window:nds" FAIL "frames $(value ds frames) of $frames, picture $(value ds width)x$(value ds height) and not 256x384"
elif [ -s "$work/engine.ram.bin" ] && cmp -s "$work/engine.ram.bin" "$work/ds.ram.bin"; then
	report "window:nds" PASS "the test program ran $frames frames in Chimera's window, 256x384, with A held from frame 100; its Main RAM ($(wc -c < "$work/ds.ram.bin") bytes, the program's count at $(od -An -tu4 -j $((0x100004)) -N4 "$work/ds.ram.bin" | tr -d ' ')) is what the headless runner leaves"
else
	report "window:nds" FAIL "the Main RAM differs from the headless runner's (or one of the two is missing)"
fi

mkdir -p "$work/keys"; cp "$work/ds/chimera-test.nds" "$work/keys/"; sed 's/"melonds-test-[^"]*"/"melonds-test-keys"/' "$work/ds/ds.chimeraProject" > "$work/keys/keys.chimeraProject"
if lua_script=dump-controls.lua run_frontend keys "$work/settings.keys.json" 1 ""; then
	if python3 "$here/check-keybinds.py" "$work/keys.ram.bin" "$wb/default_keybinds.json" "Nintendo DS Controls" "A" > "$work/keys.txt" 2>&1; then
		report "window:keys" PASS "$(cat "$work/keys.txt")"
	else
		report "window:keys" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "window:keys" FAIL "Chimera did not finish the run: $(tail -2 "$work/keys.log" | tr '\n' ' ' | cut -c1-200)"
fi

# ---- a game
nds="$(ls "$game"/*.nds 2> /dev/null | head -1)"
if [ -z "$nds" ]; then
	report "window:game" SKIP "needs a Nintendo DS game (NDS_GAME)"
	report "window:refusal" SKIP "needs a Nintendo DS game (NDS_GAME)"
	report "window:bios" SKIP "needs a Nintendo DS game (NDS_GAME)"
else
	make_project gm $((frames + 100)) "$nds"
	"$runner" --project "$work/gm/gm.chimeraProject" "$package" --files "$work/gm" --frames "$frames" \
		--dump "Main RAM=$work/gm-engine.ram.bin" > "$work/gm-engine.log" 2>&1
	if ! run_frontend gm "$work/settings.base.json" "$frames" "Main RAM"; then
		report "window:game" FAIL "Chimera did not finish the run: $(value gm detail) $(tail -2 "$work/gm.log" | tr '\n' ' ' | cut -c1-200)"
	elif [ "$(value gm frames)" = "$frames" ] && [ -s "$work/gm-engine.ram.bin" ] && cmp -s "$work/gm-engine.ram.bin" "$work/gm.ram.bin"; then
		report "window:game" PASS "a project of the game ran $frames frames in the window, $(value gm width)x$(value gm height), and left the Main RAM the headless runner leaves"
	else
		report "window:game" FAIL "frames $(value gm frames) of $frames, or the Main RAM differs from the headless runner's"
	fi
	if [ ! -f "$bios7" ] || [ ! -f "$bios9" ] || [ ! -f "$firmware" ]; then
		report "window:refusal" SKIP "needs the console's BIOS and firmware files (NDS_BIOS7, NDS_BIOS9, NDS_FIRMWARE)"
		report "window:bios" SKIP "needs the console's BIOS and firmware files (NDS_BIOS7, NDS_BIOS9, NDS_FIRMWARE)"
	else
		# the files live in a folder of their own, which Chimera is told of only
		# by the configuration of the run that is meant to find them
		fwdir="$(mktemp -d)"
		cp "$bios7" "$fwdir/bios7.bin"; cp "$bios9" "$fwdir/bios9.bin"; cp "$firmware" "$fwdir/firmware.bin"
		fw=("firmware=bios7.bin=$fwdir/bios7.bin" "firmware=bios9.bin=$fwdir/bios9.bin" "firmware=firmware.bin=$fwdir/firmware.bin")
		make_project menunofw 100 "$nds" bios=original boot=firmware "${fw[@]}"
		if run_frontend menunofw "$work/settings.base.json" 1 ""; then
			report "window:refusal" FAIL "the project opened without the console's files"
		elif grep -q "bios7.bin\|bios9.bin\|firmware.bin" "$work/menunofw.log"; then
			report "window:refusal" PASS "without the firmware the project is not opened, and a file is named: $(grep -o "[^.]*\(bios7\|bios9\|firmware\)\.bin[^.]*" "$work/menunofw.log" | head -1 | cut -c1-120)"
		else
			report "window:refusal" FAIL "refused, but no file is named: $(tail -2 "$work/menunofw.log" | tr '\n' ' ' | cut -c1-200)"
		fi
		python3 "$here/settings-config.py" "$work/settings.fw.json" '{}' "{\"bios7.bin\": \"$fwdir/bios7.bin\", \"bios9.bin\": \"$fwdir/bios9.bin\", \"firmware.bin\": \"$fwdir/firmware.bin\"}"
		make_project menu $((frames + 100)) "$nds" bios=original boot=firmware "${fw[@]}"
		if ! run_frontend menu "$work/settings.fw.json" "$frames" ""; then
			report "window:bios" FAIL "Chimera did not finish the run: $(value menu detail) $(tail -2 "$work/menu.log" | tr '\n' ' ' | cut -c1-200)"
		elif [ "$(value menu frames)" = "$frames" ]; then
			report "window:bios" PASS "a project that starts from the console's menu opened with the console's three files from Chimera's firmware and ran $frames frames in the window"
		else
			report "window:bios" FAIL "frames $(value menu frames) of $frames"
		fi
		rm -rf "$fwdir"
	fi
fi

# ---- the DSi
dsi="$(ls "$dsigame"/*.nds 2> /dev/null | head -1)"
if [ -z "$dsi" ] || [ ! -f "$bios7i" ] || [ ! -f "$bios9i" ] || [ ! -f "$dsifirmware" ] || [ ! -f "$dsinand" ]; then
	report "window:dsi" SKIP "needs a DSi game (DSI_GAME) and a DSi's BIOS files, firmware and NAND image"
else
	# the files are named in place: the NAND image is a quarter of a gigabyte
	fw=("firmware=bios7i.bin=$bios7i" "firmware=bios9i.bin=$bios9i" "firmware=dsi_firmware.bin=$dsifirmware" "firmware=dsi_nand.bin=$dsinand")
	python3 "$here/settings-config.py" "$work/settings.dsi.json" '{}' "{\"bios7i.bin\": \"$bios7i\", \"bios9i.bin\": \"$bios9i\", \"dsi_firmware.bin\": \"$dsifirmware\", \"dsi_nand.bin\": \"$dsinand\"}"
	make_project dsi $((frames + 100)) "$dsi" machine=dsi "${fw[@]}"
	"$runner" --project "$work/dsi/dsi.chimeraProject" "$package" --files "$work/dsi" --frames "$frames" \
		--firmware "bios7i.bin=$bios7i" --firmware "bios9i.bin=$bios9i" --firmware "dsi_firmware.bin=$dsifirmware" --firmware "dsi_nand.bin=$dsinand" \
		--dump "Main RAM=$work/dsi-engine.ram.bin" > "$work/dsi-engine.log" 2>&1
	if ! run_frontend dsi "$work/settings.dsi.json" "$frames" "Main RAM"; then
		report "window:dsi" FAIL "Chimera did not finish the run: $(value dsi detail) $(tail -2 "$work/dsi.log" | tr '\n' ' ' | cut -c1-200)"
	elif [ "$(value dsi frames)" = "$frames" ] && [ -s "$work/dsi-engine.ram.bin" ] && cmp -s "$work/dsi-engine.ram.bin" "$work/dsi.ram.bin"; then
		report "window:dsi" PASS "a DSi project opened with the DSi's four files from Chimera's firmware, ran $frames frames in the window and left the Main RAM ($(wc -c < "$work/dsi.ram.bin") bytes) the headless runner leaves"
	else
		report "window:dsi" FAIL "frames $(value dsi frames) of $frames, or the Main RAM differs from the headless runner's"
	fi
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
