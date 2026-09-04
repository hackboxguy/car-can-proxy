#!/bin/bash
# Bucket 5 acceptance: car-can-emulator --car=ev|hybrid|ice on vcan1 through
# emu-ev / emu-hybrid, with the EV signals read over UDS/ISO-TP.
# Skipped (exit 77) without vcan0/vcan1, the emulator, or the can-isotp module.
set -u
BUILD="${1:-$(dirname "$0")/../../build}"
EMU="${CANPROXY_EMULATOR:-$(dirname "$0")/../../../car-can-emulator/build/car-can-emulator}"
CIF="${CANPROXY_TEST_IF:-vcan0}"
VIF="${CANPROXY_TEST_VEHICLE_IF:-vcan1}"
DAEMON="$BUILD/core/can-proxyd"
DUMP="$BUILD/tools/contract-dump"

for IF in "$CIF" "$VIF"; do ip link show "$IF" >/dev/null 2>&1 || { echo "SKIP: $IF not present"; exit 77; }; done
[ -x "$EMU" ] || { echo "SKIP: emulator not built at $EMU"; exit 77; }
grep -q can_isotp /proc/modules 2>/dev/null || { echo "SKIP: can_isotp module not loaded"; exit 77; }
[ -x "$DAEMON" ] && [ -x "$DUMP" ] && [ -f "$BUILD/plugins/emu-ev.so" ] && [ -f "$BUILD/plugins/emu-hybrid.so" ] || { echo "build first"; exit 1; }

fail=0; EPID=""; DPID=""
cleanup() { for p in "$DPID" "$EPID"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; wait 2>/dev/null; }
trap cleanup EXIT
knob() { printf '%s' "$1" > /dev/tcp/127.0.0.1/8080; }
decoded() { timeout "$1" "$DUMP" "$CIF" | sed -E 's/^ *[0-9.]+ +//; s/ctr= *[0-9]+ //' | sort -u; }
start_emu() { "$EMU" --node="$VIF" --car="$1" >/dev/null 2>&1 & EPID=$!; sleep 0.5; }
start_daemon() { "$DAEMON" --contract-if="$CIF" --vehicle-if="$VIF" --plugin="$BUILD/plugins/$1.so" --plugin-arg source=emulator --log-level=warn & DPID=$!; }
stop_all() { kill "$DPID" 2>/dev/null; wait "$DPID" 2>/dev/null; DPID=""; kill "$EPID" 2>/dev/null; wait "$EPID" 2>/dev/null; EPID=""; }
expect() { echo "$OUT" | grep -q -- "$1" || { echo "FAIL [$2]: wanted '$1' got: $(echo "$OUT" | grep -E "${1%% *}" | head -1)"; fail=1; }; }

echo "== 1. --car=ev through emu-ev: EV signals from UDS, no fuel/coolant, bev identity"
start_emu ev; start_daemon emu-ev; sleep 2.5
OUT=$(decoded 1.2)
# caps: speed0 rpm1 gear2 motor3 packVI4 soc5 range6 cons7 odo8 ambient9 aux12 soh13 charging14 power-state16 assist17 = 0x373FF
expect "status    v1.1 state=ok caps=0x373FF" ev-caps
expect "identity  drivetrain=bev source=emulator" ev-identity
expect "motion    speed=88.00 km/h rpm=6600 gear=D power=ready" ev-motion
expect "edrive    power=21.300 kW pack=388.0 V 55.000 A" ev-edrive
expect "energy    soc=80.0% soh=97.0% range=290 km cons=165 Wh/km chg=0" ev-energy
expect "thermal   coolant=SNA C fuel=SNA% aux=12.60 V" ev-thermal
expect "telltales 0x00001000" ev-ready-lamp
expect "assist    eco=78 limit=50 risk=0 lane=3 gap=42.00 m" ev-assist

echo "== 2. battery knobs over UDS reach the contract"
knob "soc 42.5"; knob "power -30"; knob "chg 2"; knob "packi -80"; knob "range 150"; knob "gear N"; knob "limit 80"; knob "risk 2"; knob "gap 12.5"
sleep 2
OUT=$(decoded 1.2)
expect "soc=42.5%" soc-knob
expect "power=-30.00 kW" power-knob
expect "chg=2" charging-knob
expect "-80.00 A" current-knob
expect "range=150 km" range-knob
expect "gear=N" gear-knob
expect "telltales 0x00003000" charging-lamp
expect "limit=80 risk=2 lane=3 gap=12.50 m" assist-knobs
stop_all

echo "== 3. --car=hybrid through emu-hybrid: fuel and SoC both live, hev identity"
start_emu hybrid; start_daemon emu-hybrid; sleep 2.5
OUT=$(decoded 1.2)
# ev caps + coolant10 fuel11 = 0x373FF | 0xC00 = 0x37FFF
expect "state=ok caps=0x37FFF" hybrid-caps
expect "drivetrain=hev" hybrid-identity
expect "motion    speed=88.00 km/h rpm=768 gear=D power=ready" hybrid-motion-engine-rpm
expect "thermal   coolant=-5 C fuel=75.0% aux=12.60 V" hybrid-thermal
expect "energy    soc=80.0%" hybrid-soc
expect "telltales 0x00000000" hybrid-no-ev-ready
stop_all

echo "== 4. --car=ice through emu-ev: honest degradation, no EV capability advertised"
start_emu ice; start_daemon emu-ev; sleep 2.5
OUT=$(decoded 1.2)
# speed0 odo8 ambient9 aux12 = 0x1301
expect "state=ok caps=0x01301" ice-under-ev-caps
expect "edrive    power=SNA kW pack=SNA V SNA A" ice-under-ev-sna
expect "energy    soc=SNA%" ice-under-ev-soc-sna
stop_all

echo "== 5. battery ECU vanishes: EV signals unknown, capabilities held, no-vehicle on full loss"
start_emu ev; start_daemon emu-ev; sleep 2.5
kill "$EPID"; wait "$EPID" 2>/dev/null; EPID=""
sleep 2.5
OUT=$(decoded 1.2)
expect "state=no-vehicle caps=0x373FF" loss-caps-held
expect "soc=SNA%" loss-soc
start_emu ev; sleep 3
OUT=$(decoded 1.2)
expect "state=ok" recovery
expect "soc=80.0%" recovery-soc
stop_all

[ $fail -eq 0 ] && echo "emu_ev_test: PASS" || echo "emu_ev_test: FAIL"
exit $fail
