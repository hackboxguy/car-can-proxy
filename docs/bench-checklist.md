# Bench checklist (v1 definition of done)

The eight steps from `docs/prd.md` §3 as a runbook. Run on a Linux PC or a
Pi 4. Tick every step before calling v1 done.

## Prerequisites

```bash
# three sibling checkouts
git clone https://github.com/hackboxguy/car-can-emulator.git
git clone https://github.com/hackboxguy/car-can-proxy.git
git clone https://github.com/hackboxguy/qt-cluster-demo.git      # private

sudo modprobe vcan can_isotp
./car-can-proxy/scripts/bench-up.sh                               # vcan0 (contract), vcan1 (vehicle)

cmake -S car-can-emulator -B car-can-emulator/build && cmake --build car-can-emulator/build -j
cmake -S car-can-proxy -B car-can-proxy/build && cmake --build car-can-proxy/build -j
cmake -S qt-cluster-demo -B qt-cluster-demo/build -DENABLE_DMS=OFF && cmake --build qt-cluster-demo/build -j
```

Shell helpers used below (`nc` from `can-utils`/`netcat`, or bash's `/dev/tcp`):

```bash
knob() { printf '%s' "$1" > /dev/tcp/127.0.0.1/8080; }
```

## Steps

| # | Step | Command | Pass when |
|---|---|---|---|
| 1 | Emulator runs | `car-can-emulator/build/car-can-emulator --node=vcan1 --car=ev` | log shows OBD ECU and battery ECU started |
| 2 | Proxy runs | `car-can-proxy/build/core/can-proxyd --contract-if=vcan0 --vehicle-if=vcan1 --plugin=car-can-proxy/build/plugins/emu-ev.so --plugin-arg source=emulator` | `contract-dump vcan0` shows `state=ok`, `drivetrain=bev` |
| 3 | Cluster runs | `QT_QPA_PLATFORM=eglfs qt-cluster-demo/build/src/qt-cluster-demo --source=proxy --contract-if=vcan0 --theme=auto` (on a PC without eglfs, omit the platform) | EV theme, live values, no `NO VEHICLE DATA` badge |
| 4 | Knob to gauge | `knob "speed 90"; knob "soc 42"` | speed reads 90 and the battery 42 % within about a second |
| 5 | Emulator loss and recovery | Ctrl-C the emulator; wait 3 s; start it again | badge `NO VEHICLE DATA`, needles to zero, dashes; then values return without restarting the cluster |
| 6 | Proxy loss and recovery | Ctrl-C the proxy; wait 1 s; start it again | same as 5, within 500 ms of the stop |
| 7 | Record and replay | restart the proxy with `--record=session.log`; drive a few knobs; stop everything; `car-can-proxy/build/tools/can-replay --respond vcan1 session.log &`; start the proxy with `--plugin-arg source=replay` | `contract-dump vcan0` shows the same values as before the stop and a `REPLAY` badge on the cluster |
| 8 | Tests | `ctest --test-dir car-can-proxy/build`; `ctest --test-dir qt-cluster-demo/build` (configure the cluster with `-DENABLE_DMS_PROTOCOL_TESTS=ON` for its tests) | all pass; the proxy's integration tests run (not SKIP) because vcan0/vcan1 exist |

Repeat steps 2-4 with `--car=ice` + `obd2-ice.so` (analog theme, fuel and
coolant live, EV gauges hidden) and `--car=hybrid` + `emu-hybrid.so` (third
theme, fuel and SoC both live). One cluster binary throughout: note its
`sha256sum` in the checkpoint.

## Pi 4 service variant

```bash
cd /home/pi/car-can-proxy && ./scripts/deploy.sh --plugin=emu-ev --vehicle-if=vcan1
cd /home/pi/car-can-emulator && sudo systemctl enable --now systemd/car-can-emulator.service
cd /home/pi/qt-cluster-demo && ./scripts/build-and-deploy.sh --mode=proxy
journalctl -u can-proxyd -u car-can-emulator -u qt-cluster-demo -f
```

For the real car: `./scripts/deploy.sh --plugin=obd2-ice --vehicle-if=can0`
with the CANable plugged in; `can-proxy-links.service` sets the bitrate.

## Publisher timing

```bash
car-can-proxy/build/tools/contract-dump vcan0 --stats=10 --max-jitter-ms=5 --require-counter
```

Record the worst deviation per frame in the checkpoint note. Dev host
(2026-09-04): 0.07 ms. Pi 4: to be measured.
