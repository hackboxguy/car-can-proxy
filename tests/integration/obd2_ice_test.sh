#!/bin/bash
# Bucket 4 acceptance: car-can-emulator on vcan1 -> can-proxyd + obd2-ice ->
# contract on vcan0. Capabilities must match what the emulator advertises,
# netcat changes must reach the contract, losing the emulator must give
# no-vehicle and recovery, and a recorded session must replay identically.
#
# Skipped (exit 77) without vcan0/vcan1 or the emulator binary.
set -u
BUILD="${1:-$(dirname "$0")/../../build}"
EMU="${CANPROXY_EMULATOR:-$(dirname "$0")/../../../car-can-emulator/build/car-can-emulator}"
CIF="${CANPROXY_TEST_IF:-vcan0}"
VIF="${CANPROXY_TEST_VEHICLE_IF:-vcan1}"
DAEMON="$BUILD/core/can-proxyd"
DUMP="$BUILD/tools/contract-dump"
REPLAY="$BUILD/tools/can-replay"
PLUGIN="$BUILD/plugins/obd2-ice.so"
TMP="${TMPDIR:-/tmp}/obd2_ice_test.$$"

for IF in "$CIF" "$VIF"; do ip link show "$IF" >/dev/null 2>&1 || { echo "SKIP: $IF not present"; exit 77; }; done
[ -x "$EMU" ] || { echo "SKIP: emulator not built at $EMU (set CANPROXY_EMULATOR)"; exit 77; }
[ -x "$DAEMON" ] && [ -x "$DUMP" ] && [ -x "$REPLAY" ] && [ -f "$PLUGIN" ] || { echo "build first"; exit 1; }
mkdir -p "$TMP"

fail=0
EPID=""; DPID=""; RPID=""
cleanup() { for p in "$DPID" "$EPID" "$RPID"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; wait 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
knob() { printf '%s' "$1" > /dev/tcp/127.0.0.1/8080; }   # bash/dash-compatible? needs bash
decoded() { timeout "$1" "$DUMP" "$CIF" | sed -E 's/^ *[0-9.]+ +//; s/ctr= *[0-9]+ //' | sort -u; }

start_emu() { "$EMU" --node="$VIF" >/dev/null 2>&1 & EPID=$!; sleep 0.5; }
start_daemon() { "$DAEMON" --contract-if="$CIF" --vehicle-if="$VIF" --plugin="$PLUGIN" --plugin-arg source=emulator --log-level=warn "$@" & DPID=$!; }

echo "== 1. discovery: capabilities equal what the emulator advertises, state ok"
start_emu
start_daemon --record="$TMP/session.log"
sleep 2
OUT=$(decoded 1.2)
# speed(0) rpm(1) odometer(8) ambient(9) coolant(10) fuel(11) aux(12) power-state(16) = 0x11F03
echo "$OUT" | grep -q "state=ok caps=0x11F03" || { echo "FAIL: caps/state: $(echo "$OUT" | grep status)"; fail=1; }
echo "$OUT" | grep -q "identity  drivetrain=ice source=emulator" || { echo "FAIL: identity"; fail=1; }
echo "$OUT" | grep -q "motion    speed=88.00 km/h rpm=768 gear=- power=ready" || { echo "FAIL: motion: $(echo "$OUT" | grep motion)"; fail=1; }
echo "$OUT" | grep -q "thermal   coolant=-5 C fuel=75.0% aux=12.60 V" || { echo "FAIL: thermal: $(echo "$OUT" | grep thermal)"; fail=1; }
echo "$OUT" | grep -q "trip      odo=10568.70 km ambient=23 C cabin=SNA" || { echo "FAIL: trip: $(echo "$OUT" | grep trip)"; fail=1; }
echo "$OUT" | grep -q "edrive    power=SNA kW pack=SNA V SNA A" || { echo "FAIL: edrive not SNA"; fail=1; }
echo "$OUT" | grep -q "0x450" && { echo "FAIL: assist frame published"; fail=1; }

echo "== 2. netcat knobs reach the contract"
knob "speed 90"; knob "tt 0x110"; knob "fuel 20"; knob "ambient -3"
sleep 2.5
OUT=$(decoded 1.2)
echo "$OUT" | grep -q "speed=90.00 km/h" || { echo "FAIL: speed knob: $(echo "$OUT" | grep motion)"; fail=1; }
echo "$OUT" | grep -q "telltales 0x00000110" || { echo "FAIL: telltale knob: $(echo "$OUT" | grep telltales)"; fail=1; }
echo "$OUT" | grep -q "fuel=20.0%" || { echo "FAIL: fuel knob: $(echo "$OUT" | grep thermal)"; fail=1; }
echo "$OUT" | grep -q "ambient=-3 C" || { echo "FAIL: ambient knob: $(echo "$OUT" | grep trip)"; fail=1; }
GOLDEN=$(decoded 2 | grep -v "^status")
# close the recording here so the replay reproduces exactly this state
kill "$DPID"; wait "$DPID" 2>/dev/null; DPID=""
start_daemon
sleep 2

echo "== 3. emulator stops: no-vehicle, everything SNA, capabilities held; restart recovers"
kill "$EPID"; wait "$EPID" 2>/dev/null; EPID=""
sleep 2.5
OUT=$(decoded 1.2)
echo "$OUT" | grep -q "state=no-vehicle caps=0x11F03" || { echo "FAIL: after emulator stop: $(echo "$OUT" | grep status)"; fail=1; }
echo "$OUT" | grep -q "speed=SNA" || { echo "FAIL: speed not SNA after stop"; fail=1; }
start_emu
sleep 3
OUT=$(decoded 1.2)
echo "$OUT" | grep -q "state=ok" || { echo "FAIL: no recovery: $(echo "$OUT" | grep status)"; fail=1; }
echo "$OUT" | grep -q "speed=88.00 km/h" || { echo "FAIL: values after recovery: $(echo "$OUT" | grep motion)"; fail=1; }
kill "$DPID"; wait "$DPID" 2>/dev/null; DPID=""
kill "$EPID"; wait "$EPID" 2>/dev/null; EPID=""

echo "== 4. recorded session replays to the same contract output"
LINES=$(wc -l < "$TMP/session.log")
[ "$LINES" -gt 50 ] || { echo "FAIL: record too short ($LINES lines)"; fail=1; }
grep -q "7DF#02012F" "$TMP/session.log" || { echo "FAIL: recording has no fuel request"; fail=1; }
"$REPLAY" --respond "$VIF" "$TMP/session.log" 2>/dev/null & RPID=$!
sleep 0.3
start_daemon --plugin-arg source=replay
sleep 2.5
REPLAYED=$(decoded 2 | grep -v "^status" | sed 's/source=replay/source=emulator/')
if [ "$GOLDEN" != "$REPLAYED" ]; then
    echo "FAIL: replay differs from live"; echo "--- live"; echo "$GOLDEN"; echo "--- replay"; echo "$REPLAYED"; fail=1
fi
kill "$DPID"; wait "$DPID" 2>/dev/null; DPID=""
kill "$RPID"; wait "$RPID" 2>/dev/null; RPID=""

[ $fail -eq 0 ] && echo "obd2_ice_test: PASS" || echo "obd2_ice_test: FAIL"
exit $fail
