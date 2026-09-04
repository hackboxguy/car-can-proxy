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

Buckets 1 and 2 of the plan are complete: the contract is frozen as a
tested header, and `can-proxyd` publishes it on a CAN interface from a
plugin loaded at runtime, with the `sim` plugin as the first vehicle. The
OBD library and the emulator-backed plugins follow.

| Piece | Where | State |
|---|---|---|
| Requirements | `docs/prd.md` | v0.2 |
| Plan and checkpoints | `docs/plan.md` | bucket 2 done |
| Contract v1.1 | `contract/can_proxy_contract.h` | frozen, tag `contract-v1.1` |
| Versioning rules | `docs/contract-versioning.md` | |
| Plugin ABI v1 | `include/canproxy/plugin.h` | done |
| Daemon `can-proxyd` | `core/` | done: plugin host, state machine, scheduler |
| Plugins | `plugins/` | `sim` done; `obd2-ice` (bucket 4), `emu-ev`, `emu-hybrid` (bucket 5) |
| Tools | `tools/contract-dump` | decoder and cycle-timing checker |
| OBD / UDS / ISO-TP library | `libobd/` | bucket 4-5 |

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
ctest --test-dir build         # now includes tests/integration/sim_cycle_test.sh
```

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
