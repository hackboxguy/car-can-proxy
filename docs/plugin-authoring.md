# Writing a vehicle plugin

A plugin is a shared object that knows one kind of vehicle. It reads the
vehicle however it must and hands the daemon a `canproxy_vehicle_state` in
physical units. It never sees the contract bus. `plugins/sim/sim.cpp` is the
smallest complete example; `plugins/obd2-ice` and `plugins/emu-ev` show a
polling source on top of `libobd`.

## The ABI (`include/canproxy/plugin.h`)

Export one symbol:

```c
const struct canproxy_plugin *canproxy_plugin_entry(void);
```

returning a static descriptor:

| Field | Meaning |
|---|---|
| `abi_version` | `CANPROXY_PLUGIN_ABI_VERSION`; the host refuses anything else |
| `name` | short, stable, used in logs and in `vehicle_id` |
| `version_major/minor` | reported in the contract's identity frame |
| `create(args, host)` | allocate; parse `args` (see below); return NULL to fail |
| `start(self)` | begin reading the vehicle, typically start a thread; 0 = ok |
| `stop(self)` | stop and join; must be safe to call twice |
| `destroy(self)` | free |

`args` carries `vehicle_if` (may be NULL) and the `--plugin-arg key=value`
list; `canproxy_arg(args, "key")` looks one up. `host` carries three
thread-safe callbacks:

- `publish(ctx, state)` replaces the host's copy of the vehicle state.
- `set_link(ctx, present)` says whether a vehicle is answering. Until the
  first call the daemon reports `starting`; `0` means `no vehicle`.
- `log(ctx, level, message)` writes a pre-formatted line.

## Filling the state

- Set a `capable` bit for every signal this vehicle can *ever* provide, and
  keep that set stable for the session. It becomes the contract's capability
  bitmap, which the cluster uses to decide which gauges exist.
- Set a `valid` bit only when the field holds a current measurement. Clear
  it when the source goes stale (you know its rate; the daemon does not).
  A value without its `valid` bit is never sent. A `valid` bit without its
  `capable` bit is ignored.
- Physical units, no display scaling: km/h, rpm, kW (negative = regen), V,
  A (negative = charging), %, km, Wh/km, °C. Enumerations use the `CP_`
  values in the header.
- `lamps` is a bitmap of `CANPROXY_LAMP_BIT(...)`; unlit and unsupported are
  both zero.
- `drivetrain`, `source` and `vehicle_id` identify the vehicle;
  `canproxy_vehicle_id(name, vin)` gives a stable id.
- Never invent a value. If the vehicle does not publish it, do not set
  `capable`. If it is temporarily missing, clear `valid`.

Publishing more often than the fastest contract frame (50 ms) is pointless;
`sim` publishes every 20 ms, the OBD plugins every 50 ms.

## Silence and loss

If the plugin stops calling `publish` for `--plugin-timeout-ms` (default
1000), the daemon marks every signal unknown and reports `degraded` while
keeping the heartbeat. Call `set_link(0)` yourself when you know the
vehicle is gone so the cluster shows `no vehicle` promptly, and keep your
`capable` set so the layout holds; call `set_link(1)` when it is back.

## Building

```cmake
add_library(my-car MODULE my_car.cpp)
target_link_libraries(my-car PRIVATE canproxy::plugin_abi canproxy::obd Threads::Threads)
set_target_properties(my-car PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CANPROXY_PLUGIN_OUTPUT_DIR})
```

Add the directory to `plugins/CMakeLists.txt`. Run it with
`can-proxyd --plugin=/path/my-car.so` or install it and use `--plugin=my-car`.

## What `libobd` gives you

- `obd::J1979Client`: functional requests on `0x7DF`, first-responder ECU
  stickiness, supported-PID discovery, a broadcast-frame sink, and a raw
  channel with candump recording.
- `obd::decode(pid, ...)`: physical values for the PIDs this project uses.
- `obd::Poller`: per-PID intervals, most-overdue-first scheduling,
  three-interval freshness, unanswered streak for link detection.
- `obd::IsoTpChannel` and `obd::readDid`: UDS `0x22` over the kernel
  `can-isotp` socket. Read-only by construction; the library offers no
  write, session or security services, and a plugin must not add them.

A real-vehicle plugin is `obd2-ice` plus a DID table like
`plugins/emu-ev/EmuProfile.h`, with the DIDs of that car.
