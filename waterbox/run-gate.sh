#!/bin/sh
# The core's tests. Each line says what was compared. The script ends with a
# count of tests that passed, failed and were skipped, and with an error if
# any failed. A skipped test is not a pass.
#
#   waterbox/run-gate.sh
#
# CHIMERA_ROOT=<a Chimera checkout> adds the tests that run the package in
# Chimera's own headless runner (waterbox/build-package.sh -r <it> first).
# NDS_GAME=<folder> holds a Nintendo DS game, a .nds file, for the tests that
# need one (tests/games-local/nsmb when not given). NDS_GAME_PEN=<folder>
# holds a game that is played with the pen (tests/games-local/kirby).
# NDS_BIOS7, NDS_BIOS9 and NDS_FIRMWARE are the console's own BIOS and
# firmware files (tests/roms-local/bios7.bin, bios9.bin and firmware.bin).
#
# Needs the native build (meson setup build/meson-native ... && ninja) and
# the sandboxed core (waterbox/setup-guest.sh && ninja -C build/meson-guest):
# docs/BUILDING.md.
#
# WHAT RUNS WITH NO FILES OF YOURS. No game and no BIOS of Nintendo's may be
# put in this repository or on a CI runner. The DS needs no BIOS here (melonDS
# carries a free one), and tests/make-nds-program.py assembles a small
# program, so the "nds:" tests run everywhere: both processors run, every
# control reaches the program (the buttons, the pen and where it is, the lid,
# the microphone), its reads of the buttons are seen, both screens and the
# sound come out, the screen layouts change the picture and nothing else, and
# the sandbox gives the native build's results with savestates. The "game:",
# "pen:" and "bios:" tests need files of yours and say SKIP without them.
#
# The ways a test passes on a broken thing (Chimera's docs/gates.md) that
# these are written against: a machine that did nothing agrees with itself,
# so a run is first checked to have got somewhere; the end of a run hides a
# run that went another way and came back, so runs are compared as a stream
# of digests; and a comparison that cannot fail proves nothing, so each
# "the same" has a run beside it that must come out different.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
native="$root/build/meson-native/run-native"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
chimera_root="${CHIMERA_ROOT:-}"
game="${NDS_GAME:-$root/tests/games-local/nsmb}"
game_pen="${NDS_GAME_PEN:-$root/tests/games-local/kirby}"
bios7="${NDS_BIOS7:-$root/tests/roms-local/bios7.bin}"
bios9="${NDS_BIOS9:-$root/tests/roms-local/bios9.bin}"
firmware="${NDS_FIRMWARE:-$root/tests/roms-local/firmware.bin}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ok=0; failed=0; skipped=0
report() { # <test> <PASS|FAIL|SKIP> <what>
	printf '%-22s %-6s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; FAIL) failed=$((failed + 1)) ;; *) skipped=$((skipped + 1)) ;; esac
}
# check <test> <what> <condition...>
check() {
	ck_test="$1"; ck_what="$2"; shift 2
	if "$@"; then report "$ck_test" PASS "$ck_what"; else report "$ck_test" FAIL "$ck_what"; fi
}
stream() { grep '^frame' "$1"; }
same() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2"; }
differ() { [ -s "$1" ] && [ -s "$2" ] && ! cmp -s "$1" "$2"; }
# field <file> <name>: the value after a name in the file's last report line
field() { stream "$1" | tail -1 | awk -v f="$2" '{for (i = 1; i <= NF; i++) if ($i == f) { print $(i + 1); exit }}'; }
# word <RAM dump> <n>: the program's n-th number (tests/make-nds-program.py
# keeps them at 0x02100000, which is 0x100000 into Main RAM)
word() { od -An -tu4 -j $((0x100000 + 4 * $2)) -N4 "$1" 2> /dev/null | tr -d ' '; }
# folder <name> <settings json> <game file> [<save data file>...]: a work
# folder as the sandbox mounts a project - every file under its name
folder() {
	fd_dir="$work/$1"; fd_settings="$2"; fd_game="$3"; shift 3
	mkdir -p "$fd_dir"
	ln -sf "$fd_game" "$fd_dir/$(basename "$fd_game")"
	fd_saves=""
	for fd_f in "$@"; do
		cp "$fd_f" "$fd_dir/"
		fd_saves="$fd_saves${fd_saves:+,}\"$(basename "$fd_f")\""
	done
	printf '{"cart":["%s"],"savedata":[%s]}' "$(basename "$fd_game")" "$fd_saves" > "$fd_dir/slots"
	printf '%s' "$fd_settings" > "$fd_dir/settings"
}
# the console's own BIOS and firmware, under the names the package gives them
console_files() { ln -sf "$bios7" "$work/$1/bios7.bin"; ln -sf "$bios9" "$work/$1/bios9.bin"; ln -sf "$firmware" "$work/$1/firmware.bin"; }
nat() { nt_out="$1"; nt_dir="$2"; shift 2; "$native" "$work/$nt_dir" "$@" > "$work/$nt_out.out" 2> "$work/$nt_out.err"; }
box() { bx_out="$1"; bx_dir="$2"; shift 2; "$runwbx" "$core" "$work/$bx_dir" "$@" > "$work/$bx_out.out" 2> "$work/$bx_out.err"; }
# repeats <file> <first> <second> <count> <step>: do the report lines from
# frame <second> on repeat those from frame <first> on (run-wbx --rewind).
# The count of lag frames is the runner's own and goes on counting, so it is
# left out.
repeats() {
	awk -v a="$2" -v b="$3" -v n="$4" -v step="$5" '
		/^frame/ { f = $2; $1 = ""; $2 = ""; sub(/ lag [0-9]+$/, ""); line[f] = $0 }
		END {
			for (i = step; i < n; i += step) {
				if (!((a + i) in line) || !((b + i) in line) || line[a + i] != line[b + i]) exit 1
				seen++
			}
			exit seen > 0 ? 0 : 1
		}' "$1"
}

if [ ! -x "$native" ] || [ ! -x "$runwbx" ] || [ ! -f "$core" ]; then
	echo "not built: this needs build/meson-native/run-native and run-wbx, and build/meson-guest/core.wbx (docs/BUILDING.md)" >&2
	exit 1
fi

# ------------------------------------------------- the source and the package
# melonDS is the pinned commit with exactly the patches of patches/
patches="$(ls "$root"/patches/*.patch 2> /dev/null | wc -l)"
check "source:patched" "extern/melonDS is the pinned commit with the $patches patches of patches/" \
	sh -c "sh '$here/apply-patches.sh' > '$work/patches.log' 2>&1 && [ \"\$(git -C '$root' rev-parse HEAD:extern/melonDS)\" = \"\$(git -C '$root/extern/melonDS' rev-parse HEAD)\" ]"

# a sandbox build that failed must not be tested as the previous one
stale=""
for src in "$here"/cinterface.cpp "$here"/platform.cpp "$root"/patches/*.patch; do
	[ -e "$src" ] || continue
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
done
check "package:fresh" "core.wbx is not older than its sources${stale:+ (older than:$stale)}" [ -z "$stale" ]

# the buttons and axes the package declares are the ones the core has, in order
check "package:controls" "the package declares the core's own buttons and axes, in the core's order" \
	python3 - "$here/waterbox.config" "$here/cinterface.cpp" <<'PY'
import json, re, sys
cfg = json.load(open(sys.argv[1]))
src = open(sys.argv[2]).read()
table = src[src.index("kButtonTable[kButtons] = {"):]
buttons = re.findall(r'\{ "([^"]+)", -?\d+ \}', table[:table.index("};")])
axes = re.findall(r'"([^"]+)"', re.search(r"kAxisNames\[kAxes\] = \{([^}]*)\}", src).group(1))
ok = buttons == cfg["input"]["buttons"] and axes == [a["name"] for a in cfg["input"]["axes"]]
sys.exit(0 if ok and len(buttons) == 14 and len(axes) == 3 else 1)
PY

# ------------------------------------------------- the DS, everywhere
mkdir -p "$work/prog" "$work/quiet"
python3 "$root/tests/make-nds-program.py" "$work/prog/chimera-test.nds" > /dev/null
python3 "$root/tests/make-nds-program.py" "$work/quiet/chimera-test.nds" nopad > /dev/null
prog="$work/prog/chimera-test.nds"
folder ds '{}' "$prog"
folder dsq '{}' "$work/quiet/chimera-test.nds"
# A held for a second, then the pad, then the pen at three quarters across
# and a quarter down with the lid shut and X held, to the end
dsrun="--frames 300 --report 10 --press 7:60:60 --press 0:130:20 --press 4:160:10 --axis 0:49152:1:400 --axis 1:16384:1:400 --press 12:200:200 --press 13:240:200 --press 9:240:200"
nat ds-idle ds --frames 300 --report 10 --dump-domain "Main RAM" "$work/ds-idle.ram"
nat ds ds $dsrun --list-panel --dump-domain "Main RAM" "$work/ds.ram"
box ds-box ds $dsrun
for n in ds ds-idle ds-box; do stream "$work/$n.out" > "$work/$n.stream"; done

# the program counts in RAM: a processor that does not run leaves it at 0
count="$(word "$work/ds.ram" 1)"
pictures="$(awk '{print $7}' "$work/ds.stream" | sort -u | wc -l)"
sounds="$(awk '{print $10}' "$work/ds.stream" | sort -u | wc -l)"
check "nds:runs" "a program this script assembles runs with the free BIOS: its count in RAM reached ${count:-nothing} in 300 frames, with $pictures different pictures and $sounds different stretches of sound in 30 reports" \
	[ "${count:-0}" -gt 100000 -a "$pictures" -gt 10 -a "$sounds" -gt 10 ]
check "nds:pad" "A, Up and Start change the run (the idle run's digests differ)" differ "$work/ds.stream" "$work/ds-idle.stream"
check "nds:sandbox" "native and sandbox print the same $(wc -l < "$work/ds.stream") digests of RAM, picture and sound, with every control used" \
	same "$work/ds.stream" "$work/ds-box.stream"
check "nds:controls" "the console's 14 buttons and 3 axes are live" \
	[ "$(grep -c "^panel .* active\$" "$work/ds.out")" = 14 -a "$(grep -c "^axis .* active\$" "$work/ds.out")" = 3 ]

# each button on its own, held to the end: the program's copy of the two
# button registers has that button's bit at 0 and every other at 1
wrong=""
i=0
for want in "959 127" "895 127" "991 127" "1007 127" "1015 127" "1019 127" "1021 127" "1022 127" "1023 125" "1023 126" "511 127" "767 127"; do
	nat ds-b$i ds --frames 20 --report 20 --press $i:5:100 --dump-domain "Main RAM" "$work/ds-b$i.ram"
	[ "$(word "$work/ds-b$i.ram" 0) $(word "$work/ds-b$i.ram" 2)" = "$want" ] || wrong="$wrong $i"
	i=$((i + 1))
done
check "nds:buttons" "each of the twelve buttons, held on its own, clears its own bit of the console's button registers and no other${wrong:+ (wrong: buttons$wrong)}" [ -z "$wrong" ]

# what the program read at the end, against what it reads with nothing held:
# the second button register is 0x7F idle (X, Y and the pen up, the lid
# open); the pen's place is sixteen times the pixel
check "nds:pen-and-lid" "the pen held at three quarters across and a quarter down reads as pixel $(( $(word "$work/ds.ram" 3) / 16 )),$(( $(word "$work/ds.ram" 4) / 16 )); with it, the lid shut and X held the second button register reads $(word "$work/ds.ram" 2) (idle: $(word "$work/ds-idle.ram" 2), pen up at $(word "$work/ds-idle.ram" 3),$(word "$work/ds-idle.ram" 4))" \
	[ "$(word "$work/ds.ram" 3)" = 3072 -a "$(word "$work/ds.ram" 4)" = 768 -a "$(word "$work/ds.ram" 2)" = 190 -a "$(word "$work/ds-idle.ram" 2)" = 127 -a "$(word "$work/ds-idle.ram" 3)" = 0 -a "$(word "$work/ds-idle.ram" 4)" = 4095 ]

# the microphone: silent at volume 0, noise at volume 100
nat ds-mic ds --frames 120 --report 120 --axis 2:100:30:200 --dump-domain "Main RAM" "$work/ds-mic.ram"
check "nds:microphone" "the microphone hears nothing with Mic Volume at 0 (level bits gathered: $(word "$work/ds-idle.ram" 6)) and noise with it at 100 ($(word "$work/ds-mic.ram" 6))" \
	[ "$(word "$work/ds-idle.ram" 6)" = 0 -a "$(word "$work/ds-mic.ram" 6)" -gt 1000 ]

# lag: the program reads the buttons every frame; its twin never does
nat dsq dsq --frames 100 --report 100
check "nds:lag" "no lag frame in 300 of a program that reads the buttons ($(field "$work/ds.out" lag)), 100 of 100 in one that never does ($(field "$work/dsq.out" lag))" \
	[ "$(field "$work/ds.out" lag)" = 0 -a "$(field "$work/dsq.out" lag)" = 100 ]

box ds-rerecord ds $dsrun --rerecord
box ds-session ds $dsrun --session
box ds-rewind ds --frames 300 --report 5 --press 7:60:60 --rewind
stream "$work/ds-rerecord.out" > "$work/ds-rerecord.stream"
stream "$work/ds-session.out" > "$work/ds-session.stream"
check "nds:state-every-frame" "a state saved and loaded every frame changes nothing" same "$work/ds-box.stream" "$work/ds-rerecord.stream"
check "nds:state-other-box" "a state moved to a new sandbox half way changes nothing" same "$work/ds-box.stream" "$work/ds-session.stream"
check "nds:state-rewind" "a state kept at frame 150 and loaded at 225: the frames after the load repeat those after the save" \
	repeats "$work/ds-rewind.out" 150 225 75 5

# the screen layouts: another picture, the same machine
folder ds-side '{"layout":"side-by-side","screen_gap":"32"}' "$prog"
folder ds-turn '{"rotation":"90","swap_screens":"true"}' "$prog"
folder ds-top '{"layout":"top"}' "$prog"
for n in side turn top; do nat ds-$n ds-$n $dsrun; stream "$work/ds-$n.out" | awk '{print $2, $4, $9, $10}' > "$work/ds-$n.machine"; done
stream "$work/ds.out" | awk '{print $2, $4, $9, $10}' > "$work/ds.machine"
check "nds:layouts" "side by side with a gap of 32 the picture is $(field "$work/ds-side.out" vid), turned a quarter $(field "$work/ds-turn.out" vid), the upper screen alone $(field "$work/ds-top.out" vid) (stacked: $(field "$work/ds.out" vid)); the RAM and the sound are the stacked run's in all three" \
	sh -c "[ '$(field "$work/ds-side.out" vid)' = 544x192 ] && [ '$(field "$work/ds-turn.out" vid)' = 384x256 ] && [ '$(field "$work/ds-top.out" vid)' = 256x192 ] && [ '$(field "$work/ds.out" vid)' = 256x384 ] && cmp -s '$work/ds.machine' '$work/ds-side.machine' && cmp -s '$work/ds.machine' '$work/ds-turn.machine' && cmp -s '$work/ds.machine' '$work/ds-top.machine'"

# ------------------------------------------------- what is refused
mkdir -p "$work/none"
printf '{"cart":[],"savedata":[]}' > "$work/none/slots"; printf '{}' > "$work/none/settings"
nat none none --frames 1
check "refused:no-game" "a console with no game is refused, and says so" grep -q "Init failed.*no game was given" "$work/none.out" "$work/none.err"
folder nomenu '{"boot":"firmware"}' "$prog"
nat nomenu nomenu --frames 1
check "refused:menu-needs-bios" "starting from the console's menu with the free BIOS is refused, and the message says which setting to change" \
	grep -q "Init failed.*needs the console's own BIOS" "$work/nomenu.out" "$work/nomenu.err"
folder nobios '{"bios":"original"}' "$prog"
nat nobios nobios --frames 1
check "refused:bios-missing" "with the BIOS setting on 'original' and no BIOS files the console is refused, by the files' names" \
	grep -q "Init failed.*bios7\.bin" "$work/nobios.out" "$work/nobios.err"

# ------------------------------------------------- a game
nds="$(ls "$game"/*.nds 2> /dev/null | head -1)"
if [ -n "$nds" ]; then
	folder gm '{}' "$nds"
	mkdir -p "$work/gm-save"
	# A twice, then Start, then the pad
	gmrun="--frames 1500 --report 50 --press 7:700:5 --press 7:900:5 --press 4:1000:5 --press 3:1200:60 --press 7:1300:10"
	nat gm-idle gm --frames 1500 --report 50
	nat gm gm $gmrun --savedata-out "$work/gm-save"
	box gm-box gm $gmrun
	for n in gm gm-idle gm-box; do stream "$work/$n.out" > "$work/$n.stream"; done
	pictures="$(awk '{print $7}' "$work/gm.stream" | sort -u | wc -l)"
	check "game:runs" "the game runs with the free BIOS: $pictures different pictures in 30 reports" [ "$pictures" -gt 5 ]
	check "game:pad" "A, Start and the pad change the run (the idle run's digests differ)" differ "$work/gm.stream" "$work/gm-idle.stream"
	check "game:sandbox" "native and sandbox print the same $(wc -l < "$work/gm.stream") digests of RAM, picture and sound, with presses" \
		same "$work/gm.stream" "$work/gm-box.stream"
	lag="$(field "$work/gm.out" lag)"
	check "game:lag" "lag frames are counted ($lag of 1500), and not on every frame" [ "${lag:-0}" -gt 0 -a "${lag:-0}" -lt 1500 ]
	box gm-rerecord gm $gmrun --rerecord
	box gm-session gm $gmrun --session
	box gm-rewind gm --frames 1600 --report 5 --press 7:500:5 --press 7:700:5 --rewind
	stream "$work/gm-rerecord.out" > "$work/gm-rerecord.stream"
	stream "$work/gm-session.out" > "$work/gm-session.stream"
	check "game:state-every-frame" "a state saved and loaded every frame changes nothing" same "$work/gm-box.stream" "$work/gm-rerecord.stream"
	check "game:state-other-box" "a state moved to a new sandbox half way changes nothing" same "$work/gm-box.stream" "$work/gm-session.stream"
	check "game:state-rewind" "a state kept at frame 800 and loaded at 1200: the frames after the load repeat those after the save" \
		repeats "$work/gm-rewind.out" 800 1200 400 5
	# the cartridge's save memory: out as a file; a changed copy given to a
	# project is what the cartridge holds at the start (read back after one
	# frame: a game may mend a save it does not recognise)
	sav="$work/gm-save/nds.sav"
	if [ -s "$sav" ]; then
		mkdir -p "$work/gm-mine" "$work/gm-out"
		python3 - "$sav" "$work/gm-mine/mine.sav" <<'PY'
import sys
b = bytearray(open(sys.argv[1], "rb").read())
at = len(b) // 2
b[at:at + 4] = b"\xa5\x5a\xc3\x3c"
open(sys.argv[2], "wb").write(b)
PY
		folder gm-in '{}' "$nds" "$work/gm-mine/mine.sav"
		box gm-in gm-in --frames 1 --report 1 --savedata-out "$work/gm-out"
		check "game:save-data" "the cartridge's save memory goes out as a file of $(wc -c < "$sav") bytes; a changed copy given to a project is what the cartridge starts with" \
			sh -c "cmp -s '$work/gm-mine/mine.sav' '$work/gm-out/nds.sav' && ! cmp -s '$sav' '$work/gm-out/nds.sav'"
	else
		report "game:save-data" FAIL "no save memory was exported"
	fi
	# the language is the firmware's: another one changes the run
	folder gm-fr '{"language":"french"}' "$nds"
	nat gm-fr gm-fr --frames 1500 --report 50
	stream "$work/gm-fr.out" > "$work/gm-fr.stream"
	check "game:language" "with the Language setting on French the game's idle run differs from the English one" differ "$work/gm-fr.stream" "$work/gm-idle.stream"
	if [ -f "$bios7" ] && [ -f "$bios9" ] && [ -f "$firmware" ]; then
		folder gm-bios '{"bios":"original"}' "$nds"; console_files gm-bios
		folder gm-menu '{"bios":"original","boot":"firmware"}' "$nds"; console_files gm-menu
		nat gm-bios gm-bios --frames 1500 --report 50
		nat gm-menu gm-menu --frames 900 --report 50
		box gm-menu-box gm-menu --frames 900 --report 50
		for n in gm-bios gm-menu gm-menu-box; do stream "$work/$n.out" > "$work/$n.stream"; done
		check "bios:original" "with the console's own BIOS and firmware the game runs ($(awk '{print $7}' "$work/gm-bios.stream" | sort -u | wc -l) different pictures in 30 reports)" \
			[ "$(awk '{print $7}' "$work/gm-bios.stream" | sort -u | wc -l)" -gt 5 ]
		check "bios:menu" "started from the console's menu the run is another one than the game started at once, and the sandbox gives the native build's $(wc -l < "$work/gm-menu.stream") digests" \
			sh -c "! cmp -s '$work/gm-menu.stream' '$work/gm-idle.stream' && [ \"\$(awk '{print \$7}' '$work/gm-menu.stream' | sort -u | wc -l)\" -gt 3 ] && cmp -s '$work/gm-menu.stream' '$work/gm-menu-box.stream'"
		# the console's menu shows the date: another Starting Clock, another picture
		folder gm-clock '{"bios":"original","boot":"firmware","initialTime":"1262304000"}' "$nds"; console_files gm-clock
		nat gm-clock gm-clock --frames 900 --report 50
		stream "$work/gm-clock.out" > "$work/gm-clock.stream"
		check "bios:clock" "with the Starting Clock set to the first of January 2010 the console's menu is another picture than with the default" \
			sh -c "[ \"\$(tail -1 '$work/gm-clock.stream' | awk '{print \$7}')\" != \"\$(tail -1 '$work/gm-menu.stream' | awk '{print \$7}')\" ] && [ -s '$work/gm-clock.stream' ]"
	else
		report "bios:original" SKIP "no BIOS and firmware files of the console (NDS_BIOS7, NDS_BIOS9, NDS_FIRMWARE)"
		report "bios:menu" SKIP "no BIOS and firmware files of the console (NDS_BIOS7, NDS_BIOS9, NDS_FIRMWARE)"
		report "bios:clock" SKIP "no BIOS and firmware files of the console (NDS_BIOS7, NDS_BIOS9, NDS_FIRMWARE)"
	fi
else
	for t in game:runs game:pad game:sandbox game:lag game:state-every-frame game:state-other-box game:state-rewind game:save-data game:language bios:original bios:menu bios:clock; do
		report "$t" SKIP "no Nintendo DS game (NDS_GAME)"
	done
fi

# ------------------------------------------------- a game played with the pen
pen="$(ls "$game_pen"/*.nds 2> /dev/null | head -1)"
if [ -n "$pen" ]; then
	folder pn '{}' "$pen"
	# the pen put down in the middle of the lower screen, four times
	pnrun="--frames 2400 --report 50 --axis 0:32768:1:3000 --axis 1:32768:1:3000 --press 12:900:10 --press 12:1200:10 --press 12:1500:10 --press 12:1800:10"
	nat pn-idle pn --frames 2400 --report 50
	nat pn pn $pnrun
	nat pn-corner pn --frames 2400 --report 50 --axis 0:2000:1:3000 --axis 1:2000:1:3000 --press 12:900:10 --press 12:1200:10 --press 12:1500:10 --press 12:1800:10
	box pn-box pn $pnrun
	for n in pn pn-idle pn-corner pn-box; do stream "$work/$n.out" > "$work/$n.stream"; done
	check "pen:game" "in a game played with the pen, the pen changes the run, the pen in another place gives another run again, and the sandbox gives the native build's $(wc -l < "$work/pn.stream") digests" \
		sh -c "! cmp -s '$work/pn.stream' '$work/pn-idle.stream' && ! cmp -s '$work/pn.stream' '$work/pn-corner.stream' && cmp -s '$work/pn.stream' '$work/pn-box.stream'"
else
	report "pen:game" SKIP "no Nintendo DS game played with the pen (NDS_GAME_PEN)"
fi

# ------------------------------------------------- in Chimera's engine
engine="$chimera_root/build/meson-linux/chimera-run"
pkg="$chimera_root/build/Cores/melonds.chimeraCore"
if [ -n "$chimera_root" ] && [ -x "$engine" ] && [ -f "$pkg" ]; then
	mk="$root/tests/make-project.py"
	# a movie's row N is the test runner's frame N + 1
	python3 "$mk" "$pkg" "$work/ds.chimeraProject" 300 "panel=$work/ds.out" "press=A:59:60" "press=Up:129:20" "press=Start:159:300" -- "cart=$prog" > "$work/ds.project.log" 2>&1
	python3 "$mk" "$pkg" "$work/ds-idle.chimeraProject" 300 "panel=$work/ds.out" -- "cart=$prog" >> "$work/ds.project.log" 2>&1
	"$engine" --project "$work/ds.chimeraProject" "$pkg" --files "$work/prog" --dump "Main RAM=$work/ds-engine.ram" > "$work/ds.engine" 2>&1
	"$engine" --project "$work/ds-idle.chimeraProject" "$pkg" --files "$work/prog" --dump "Main RAM=$work/ds-idle-engine.ram" > "$work/ds-idle.engine" 2>&1
	# (Start is held to the end: the program keeps no trace of a press that is over)
	box ds-same ds --frames 300 --report 300 --press 7:60:60 --press 0:130:20 --press 4:160:300 --dump-domain "Main RAM" "$work/ds-same.ram"
	check "engine:nds" "a project of the program plays a 300-frame movie in Chimera's engine and leaves the Main RAM the sandbox runner leaves with the same presses, which is not the idle movie's" \
		sh -c "grep -q '^frames=300' '$work/ds.engine' && cmp -s '$work/ds-engine.ram' '$work/ds-same.ram' && ! cmp -s '$work/ds-engine.ram' '$work/ds-idle-engine.ram'"
	if [ -n "$nds" ]; then
		python3 "$mk" "$pkg" "$work/gm.chimeraProject" 1200 "panel=$work/ds.out" "press=A:699:5" "press=A:899:5" "press=Start:999:5" -- "cart=$nds" > "$work/gm.project.log" 2>&1
		"$engine" --project "$work/gm.chimeraProject" "$pkg" --files "$game" --dump "Main RAM=$work/gm-engine.ram" > "$work/gm.engine" 2>&1
		box gm-same gm --frames 1200 --report 1200 --press 7:700:5 --press 7:900:5 --press 4:1000:5 --dump-domain "Main RAM" "$work/gm-same.ram"
		check "engine:game" "a project of the game plays a 1,200-frame movie in Chimera's engine and leaves the Main RAM the sandbox runner leaves with the same presses" \
			sh -c "grep -q '^frames=1200' '$work/gm.engine' && cmp -s '$work/gm-engine.ram' '$work/gm-same.ram'"
		if [ -f "$bios7" ] && [ -f "$bios9" ] && [ -f "$firmware" ]; then
			python3 "$mk" "$pkg" "$work/gm-menu.chimeraProject" 900 bios=original boot=firmware "panel=$work/ds.out" \
				"firmware=bios7.bin=$bios7" "firmware=bios9.bin=$bios9" "firmware=firmware.bin=$firmware" -- "cart=$nds" > "$work/gm-menu.project.log" 2>&1
			"$engine" --project "$work/gm-menu.chimeraProject" "$pkg" --files "$game" --firmware "bios7.bin=$bios7" --firmware "bios9.bin=$bios9" --firmware "firmware.bin=$firmware" \
				--dump "Main RAM=$work/gm-menu-engine.ram" > "$work/gm-menu.engine" 2>&1
			box gm-menu-same gm-menu --frames 900 --report 900 --dump-domain "Main RAM" "$work/gm-menu-same.ram"
			check "engine:bios" "a project with the console's BIOS and firmware as its firmware starts from the console's menu in Chimera's engine and leaves the Main RAM the sandbox runner leaves" \
				sh -c "grep -q '^frames=900' '$work/gm-menu.engine' && cmp -s '$work/gm-menu-engine.ram' '$work/gm-menu-same.ram'"
		else
			report "engine:bios" SKIP "no BIOS and firmware files of the console"
		fi
	else
		report "engine:game" SKIP "no Nintendo DS game (NDS_GAME)"
		report "engine:bios" SKIP "no Nintendo DS game (NDS_GAME)"
	fi
else
	for t in nds game bios; do
		report "engine:$t" SKIP "set CHIMERA_ROOT to a Chimera checkout with build/meson-linux/chimera-run and this core's package (waterbox/build-package.sh -r <it>)"
	done
fi

printf '\n%d passed, %d failed, %d skipped\n' "$ok" "$failed" "$skipped"
[ "$ok" -gt 0 ] || { echo "NOTHING RAN"; exit 1; }
[ "$failed" -eq 0 ]
