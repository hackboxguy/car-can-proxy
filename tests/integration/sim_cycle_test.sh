#!/bin/sh
# Bucket 2 acceptance on a real vcan: can-proxyd + sim must emit every
# contract frame on cycle with no counter skips, and go degraded when the
# plugin stalls while the heartbeat continues.
#
# Skipped (exit 77) when vcan0 is absent or not writable. Run scripts/bench-up.sh first.
set -u
BUILD="${1:-$(dirname "$0")/../../build}"
IF="${CANPROXY_TEST_IF:-vcan0}"
DAEMON="$BUILD/core/can-proxyd"
DUMP="$BUILD/tools/contract-dump"
PLUGIN="$BUILD/plugins/sim.so"

ip link show "$IF" >/dev/null 2>&1 || { echo "SKIP: $IF not present"; exit 77; }
[ -x "$DAEMON" ] && [ -x "$DUMP" ] && [ -f "$PLUGIN" ] || { echo "build first: $DAEMON $DUMP $PLUGIN"; exit 1; }

fail=0
cleanup() { [ -n "${PID:-}" ] && kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; }
trap cleanup EXIT

echo "== 1. steady state: all nine IDs on cycle, no counter skips (6 s)"
"$DAEMON" --contract-if="$IF" --plugin="$PLUGIN" --plugin-arg drivetrain=bev --log-level=warn &
PID=$!
sleep 0.3
"$DUMP" "$IF" --stats=6 --max-jitter-ms=5 --require-counter \
    --expect-ids=0x400,0x401,0x410,0x411,0x420,0x430,0x431,0x440,0x450 || fail=1
kill "$PID"; wait "$PID" 2>/dev/null; PID=""

echo "== 2. ICE without assist: 0x450 absent, EV fields SNA"
"$DAEMON" --contract-if="$IF" --plugin="$PLUGIN" --plugin-arg drivetrain=ice --plugin-arg assist=0 --log-level=warn &
PID=$!
sleep 0.5
OUT=$(timeout 2 "$DUMP" "$IF" | head -60)
kill "$PID"; wait "$PID" 2>/dev/null; PID=""
echo "$OUT" | grep -q "0x450" && { echo "FAIL: 0x450 published without capability"; fail=1; }
echo "$OUT" | grep -q "edrive    power=SNA kW pack=SNA V SNA A" || { echo "FAIL: edrive not SNA for ICE"; fail=1; }
echo "$OUT" | grep -q "thermal   coolant=[0-9]* C fuel=[0-9.]*% aux" || { echo "FAIL: fuel not live for ICE"; fail=1; }
echo "$OUT" | grep -q "drivetrain=ice source=simulated" || { echo "FAIL: identity"; fail=1; }

echo "== 3. plugin stalls: heartbeat continues, state -> degraded, speed -> SNA"
"$DAEMON" --contract-if="$IF" --plugin="$PLUGIN" --plugin-arg stall_after_ms=1000 --plugin-timeout-ms=500 --log-level=warn &
PID=$!
sleep 0.5
EARLY=$(timeout 1 "$DUMP" "$IF" | grep -m1 "^.*0x410")
sleep 1.5
LATE=$(timeout 1 "$DUMP" "$IF" | grep -E "0x400|0x410" | head -4)
kill "$PID"; wait "$PID" 2>/dev/null; PID=""
echo "$EARLY" | grep -q "speed=[0-9.]* km/h" || { echo "FAIL: no live speed before stall: $EARLY"; fail=1; }
echo "$LATE" | grep -q "state=degraded" || { echo "FAIL: not degraded after stall: $LATE"; fail=1; }
echo "$LATE" | grep -q "speed=SNA" || { echo "FAIL: speed not SNA after stall: $LATE"; fail=1; }

echo "== 4. link=0: state no-vehicle, capabilities still advertised"
"$DAEMON" --contract-if="$IF" --plugin="$PLUGIN" --plugin-arg link=0 --log-level=warn &
PID=$!
sleep 0.5
OUT=$(timeout 1 "$DUMP" "$IF" | grep -m1 "0x400")
kill "$PID"; wait "$PID" 2>/dev/null; PID=""
echo "$OUT" | grep -q "state=no-vehicle" || { echo "FAIL: expected no-vehicle: $OUT"; fail=1; }

[ $fail -eq 0 ] && echo "sim_cycle_test: PASS" || echo "sim_cycle_test: FAIL"
exit $fail
