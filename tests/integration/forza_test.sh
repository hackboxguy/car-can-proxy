#!/bin/bash
# Forza Data Out telemetry (synthesised here with python) through the forza
# plugin onto the contract. Needs vcan0 and python3; SKIP otherwise.
set -u
BUILD="${1:-$(dirname "$0")/../../build}"
CIF="${CANPROXY_TEST_IF:-vcan0}"
PORT=21101
ip link show "$CIF" >/dev/null 2>&1 || { echo "SKIP: $CIF not present"; exit 77; }
command -v python3 >/dev/null || { echo "SKIP: python3 missing"; exit 77; }
fail=0; DPID=""; SPID=""
cleanup() { for p in "$DPID" "$SPID"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT
"$BUILD/core/can-proxyd" --contract-if="$CIF" --plugin="$BUILD/plugins/forza.so" --plugin-arg port=$PORT --plugin-arg drivetrain=bev --log-level=warn & DPID=$!
sleep 0.5
# FH4/FH5 layout, 60 Hz: 27.5 m/s, 4321 rpm, 155 kW, 42 % fuel, gear 4, handbrake on for the first second.
python3 - "$PORT" <<'PY' & SPID=$!
import socket, struct, sys, time
port = int(sys.argv[1]); s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
t0 = time.time()
while time.time() - t0 < 6:
    p = bytearray(324)
    struct.pack_into('<iIfff', p, 0, 1, 1000, 7500.0, 800.0, 4321.0)
    struct.pack_into('<fff', p, 32, 16.5, 0.0, 22.0)
    b = 244
    struct.pack_into('<ffff', p, b + 12, 27.5, 155000.0, 300.0, 0.0)
    struct.pack_into('<ff', p, b + 44, 0.42, 12345.0)
    p[b + 74] = 1 if time.time() - t0 < 1.0 else 0
    p[b + 75] = 4
    s.sendto(p, ('127.0.0.1', port)); time.sleep(1 / 60)
PY
sleep 2.5
OUT=$(timeout 1.2 "$BUILD/tools/contract-dump" "$CIF" | sed -E 's/^ *[0-9.]+ +//; s/ctr= *[0-9]+ //' | sort -u)
expect() { echo "$OUT" | grep -q -- "$1" || { echo "FAIL [$2]: $(echo "$OUT" | grep -E "${1%% *}" | head -1)"; fail=1; }; }
# speed0 rpm1 gear2 motor3 odometer8 fuel11 power-state16 = 0x1090F
expect "state=ok caps=0x1090F" caps
expect "drivetrain=bev source=simulated" identity
expect "speed=99.00 km/h rpm=4321 gear=D power=ready" motion
expect "power=155.00 kW pack=SNA V SNA A" power
expect "fuel=42.0%" fuel
expect "odo=12.30" odometer
expect "telltales 0x00000000" handbrake-released
wait "$SPID" 2>/dev/null; SPID=""
sleep 2
OUT=$(timeout 1.2 "$BUILD/tools/contract-dump" "$CIF" | sed -E 's/^ *[0-9.]+ +//; s/ctr= *[0-9]+ //' | sort -u)
expect "state=no-vehicle" silence
[ $fail -eq 0 ] && echo "forza_test: PASS" || echo "forza_test: FAIL"
exit $fail
