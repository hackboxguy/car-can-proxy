# car-can-proxy

A proxy service that offers a unified interface to instrument-cluster apps
irrespective of the type of car's CAN messaging format.

```
 vehicle side (can0 / vcan1)               contract side (vcan0)
 ┌────────────────────────┐   OBD-II /   ┌─────────────────────────┐  0x400..0x450  ┌──────────────────┐
 │ real car, or           │ <──────────> │ can-proxyd + one plugin │ ─────────────> │ instrument       │
 │ car-can-emulator       │  UDS/ISO-TP  │ (ICE / EV / hybrid)     │   broadcast    │ cluster app      │
 └────────────────────────┘              └─────────────────────────┘                └──────────────────┘
```

Every vehicle-specific detail (PIDs, OEM DIDs, scaling, which signals a car
simply does not have) lives in a plugin. The cluster consumes one broadcast
frame set with physical units and an explicit "not available" encoding for
every signal, so the same cluster binary works for combustion, electric and
hybrid vehicles.

## Status

Buckets 1 to 5 of the plan are complete: the contract is frozen as a
tested header, `can-proxyd` publishes it on a CAN interface from a plugin
loaded at runtime, the instrument-cluster application reads it
(`--source=proxy`) with per-signal validity, per-gauge capability and
automatic theme selection, and three vehicle plugins drive it from the
`car-can-emulator` in its `ice`, `ev` and `hybrid` modes: `obd2-ice` over
J1979, `emu-ev` and `emu-hybrid` over J1979 plus UDS/ISO-TP to a battery
ECU. Deployment, CI and the v1 review (bucket 6) remain.

| Piece | Where | State |
|---|---|---|
| Requirements | `docs/prd.md` | v0.2 |
| Plan and checkpoints | `docs/plan.md` | bucket 5 done |
| Contract v1.1 | `contract/can_proxy_contract.h` | frozen, tag `contract-v1.1` |
| Versioning rules | `docs/contract-versioning.md` | |
| Plugin ABI v1 | `include/canproxy/plugin.h` | done |
| Daemon `can-proxyd` | `core/` | done: plugin host, state machine, scheduler |
| Plugins | `plugins/` | `sim`, `obd2-ice`, `emu-ev`, `emu-hybrid` |
| Tools | `tools/contract-dump`, `tools/can-replay` | contract decoder and timing checker; session replay |
| OBD / UDS / ISO-TP library | `libobd/` | J1979 client, UDS `0x22` over kernel ISO-TP |
| Emulator EV profile | `docs/emulator-ev-profile.md` | reference DIDs the emulator's battery ECU serves |

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Needs a C++17 compiler, CMake 3.16 and Linux SocketCAN headers. The
integration test needs a `vcan0` interface and reports SKIP without one:

```bash
./scripts/bench-up.sh          # creates vcan0 (contract) and vcan1 (vehicle), needs sudo
ctest --test-dir build         # includes the vcan integration tests
```

The `obd2_ice_test` integration test also needs the emulator built at
`../car-can-emulator/build/car-can-emulator` (or `-DCANPROXY_EMULATOR=<path>`).

## Run

```bash
./build/core/can-proxyd --contract-if=vcan0 --plugin=build/plugins/sim.so --plugin-arg drivetrain=bev
./build/tools/contract-dump vcan0            # decoded frames, in a second terminal
./build/tools/contract-dump vcan0 --stats=5  # per-ID rate and jitter table
```

`--plugin=<name>` resolves to `<prefix>/lib/can-proxy/plugins/<name>.so`
after `cmake --install`; a path ending in `.so` is used as given. See
`can-proxyd --help` for `--vehicle-if`, `--plugin-arg`, `--plugin-timeout-ms`.

The `sim` plugin takes `drivetrain=ice|bev|hev|phev`, `assist=0`, `link=0`
and `stall_after_ms=<n>` as plugin arguments; the last two exist to exercise
the daemon's no-vehicle and degraded states.

With the emulator as the vehicle (or a real OBD-II car on `can0`):

```bash
../car-can-emulator/build/car-can-emulator --node=vcan1 &
./build/core/can-proxyd --contract-if=vcan0 --vehicle-if=vcan1 --plugin=build/plugins/obd2-ice.so \
    --plugin-arg source=emulator --record=session.log
echo -n "speed 90" | nc 127.0.0.1 8080      # the contract follows within a poll cycle
```

`obd2-ice` discovers the supported-PID bitmaps and polls only what the ECU
advertises: speed, rpm, coolant, fuel, module voltage, ambient and odometer
when present. `emu-ev` and `emu-hybrid` do the same and add the emulator's
battery ECU over UDS/ISO-TP (`--car=ev|hybrid`, `sudo modprobe can_isotp`);
see `docs/emulator-ev-profile.md`. `--record` writes the vehicle side in candump format;
`tools/can-replay --respond vcan1 session.log` then stands in for the
vehicle, answering each request with the recorded reply, so a plugin can be
developed and regression-tested after the car has gone.

## Writing a plugin

A plugin is a shared object exporting `canproxy_plugin_entry()` and built
against `include/canproxy/plugin.h`. It pushes a `canproxy_vehicle_state` in
physical units with a validity bit per signal and tells the host whether a
vehicle is present. It never sees the contract bus. `plugins/sim/sim.cpp` is
the reference; a full authoring guide is planned for bucket 6.

## Related repositories

- `car-can-emulator` — bench vehicle: OBD-II responder, gaining EV and hybrid
  reference modes so the proxy can be proven without a real car.
- An instrument-cluster application that reads only this contract.

## License

See `LICENSE`.
