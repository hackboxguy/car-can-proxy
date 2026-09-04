#!/bin/bash
# A recorded real-dongle session (tests/fixtures) replayed as the vehicle
# must drive obd2-ice to the state seen on the Pi, with the device's zero
# fillers for module voltage and ambient treated as unavailable.
set -u
BUILD="${1:-$(dirname "$0")/../../build}"
FIX="$(dirname "$0")/../fixtures/obd2-emulator-canable-pi4.log"
CIF="${CANPROXY_TEST_IF:-vcan0}"; VIF="${CANPROXY_TEST_VEHICLE_IF:-vcan1}"
for IF in "$CIF" "$VIF"; do ip link show "$IF" >/dev/null 2>&1 || { echo "SKIP: $IF not present"; exit 77; }; done
[ -f "$FIX" ] || { echo "SKIP: fixture missing"; exit 77; }
fail=0; RPID=""; DPID=""
cleanup() { for p in "$DPID" "$RPID"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT
"$BUILD/tools/can-replay" --respond "$VIF" "$FIX" 2>/dev/null & RPID=$!
sleep 0.3
"$BUILD/core/can-proxyd" --contract-if="$CIF" --vehicle-if="$VIF" --plugin="$BUILD/plugins/obd2-ice.so" --plugin-arg source=replay --log-level=warn & DPID=$!
sleep 3
OUT=$(timeout 1.2 "$BUILD/tools/contract-dump" "$CIF" | sed -E 's/^ *[0-9.]+ +//; s/ctr= *[0-9]+ //' | sort -u)
expect() { echo "$OUT" | grep -q -- "$1" || { echo "FAIL [$2]: $(echo "$OUT" | grep -E "${1%% *}" | head -1)"; fail=1; }; }
# speed0 rpm1 coolant10 fuel11 power-state16 = 0x10C03: ambient (9) and aux (12)
# were advertised but answered with fillers, so they are dropped
expect "state=ok caps=0x10C03" caps
expect "drivetrain=ice source=replay" identity
expect "speed=92.00 km/h rpm=25" motion
expect "fuel=44.5% aux=SNA V" aux-filler-sna
expect "ambient=SNA C" ambient-filler-sna
expect "coolant=6" coolant
[ $fail -eq 0 ] && echo "replay_fixture_test: PASS" || echo "replay_fixture_test: FAIL"
exit $fail
