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

Bucket 1 of the plan is complete: the contract is frozen as a tested,
header-only C file. The daemon, plugins and OBD library follow.

| Piece | Where | State |
|---|---|---|
| Requirements | `docs/prd.md` | v0.2 |
| Plan and checkpoints | `docs/plan.md` | bucket 1 done |
| Contract v1.1 | `contract/can_proxy_contract.h` | complete; tag `contract-v1.1` applied at checkpoint 1 approval |
| Versioning rules | `docs/contract-versioning.md` | |
| Daemon `can-proxyd` | `core/` | bucket 2 |
| Plugins | `plugins/` | bucket 2 (`sim`), 4 (`obd2-ice`), 5 (`emu-ev`, `emu-hybrid`) |
| OBD / UDS / ISO-TP library | `libobd/` | bucket 4-5 |

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Only a C99/C++11 compiler and CMake 3.16 are needed for the contract. Later
buckets add SocketCAN (Linux) and the `vcan`, `can-isotp` kernel modules.

## Related repositories

- `car-can-emulator` — bench vehicle: OBD-II responder, gaining EV and hybrid
  reference modes so the proxy can be proven without a real car.
- An instrument-cluster application that reads only this contract.

## License

See `LICENSE`.
