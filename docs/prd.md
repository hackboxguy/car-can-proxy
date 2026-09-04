# car-can-proxy — Product Requirements

**Status:** v0.3, buckets 1-6 delivered (2026-09-04); v1 review at checkpoint 6
**Date:** 2026-09-03
**Companion:** `docs/plan.md` (buckets and checkpoints)

## 1. The one decision this project drives

> Can a single, unmodified `qt-cluster-demo` binary be driven by fundamentally
> different vehicles (combustion, electric, hybrid; emulated or real) with
> **zero vehicle-specific code in the cluster**, because every vehicle detail
> lives behind one published frame contract in a separate proxy with plugins?

Everything in this document exists to answer that question with a yes that
can be demonstrated on a bench. If the boundary leaks (the cluster grows a
special case for a car, or a plugin has to know about a theme) the project has
failed even if the gauges move.

Consequences that follow directly from that decision:

- The cluster consumes the contract and nothing else. Its existing direct
  OBD-II path (`--can=<if>`) stays for compatibility but is frozen.
- The proxy never invents a value. Unknown is transmitted as unknown.
- The emulator becomes the reference vehicle set: it must be able to *be* a
  combustion car, an EV, and a hybrid, so the boundary can be proven with no
  real car in the room.

## 2. Goals and non-goals

### Goals (v1)

| # | Goal | How we know |
|---|---|---|
| G1 | One cluster binary, three drivetrains | Same `qt-cluster-demo` build renders `analog`, `ev` and `adas` themes from emulated ICE, EV and hybrid traffic, switching only proxy plugin and cluster theme flag |
| G2 | Vehicle knowledge lives only in plugins | `grep` of the cluster for PID, DID, `0x7DF`, `0x7E8` in the new reader path returns nothing; plugin ABI carries physical units only |
| G3 | Honest unknowns | With a plugin that cannot supply a signal, the gauge shows blank/dash, not a default; capability bits match |
| G4 | Survives the vehicle | Proxy keeps publishing heartbeat with state `no vehicle` through emulator stop/start; cluster recovers without restart |
| G5 | Bench-reproducible | Whole chain runs on one Linux box with two `vcan` interfaces and no hardware; CI can run the proxy against the emulator |
| G6 | Ready for a real fossil car | `obd2-ice` plugin is written for the standard, not for the emulator: capability discovery, ECU response range, ISO-TP path exercised |

### Non-goals (v1)

- Real EV or hybrid bring-up. The EV/hybrid plugins target the emulator's
  reference profile. A real EV is a v2 plugin.
- Any write towards the vehicle (UDS `0x2E`, Mode 04, session/security).
- DMS / FocusDrive. Untouched.
- JSON-described vehicle profiles. The extension point is the `.so` ABI;
  a profile-driven generic plugin can be added later without changing the ABI.
- 29-bit addressing and 250 kbit/s detection. Design leaves room; v2 work.
- OpenWrt packaging of the proxy. The emulator's OpenWrt script stays.

## 3. Definition of done for v1

On a Pi 4 or a Linux PC, from a fresh clone of the three repos:

1. `car-can-emulator --node=vcan1 --car=ice|ev|hybrid` runs.
2. `can-proxyd --vehicle-if=vcan1 --contract-if=vcan0 --plugin=<obd2-ice|emu-ev|emu-hybrid>` runs.
3. `qt-cluster-demo --source=proxy --contract-if=vcan0 --theme=<analog|ev|adas>` runs.
4. Changing a value on the emulator's netcat port (`speed 90`, `soc 42`) moves the corresponding gauge within one frame cycle plus display smoothing.
5. Killing the emulator turns the cluster to "no vehicle data" within the documented staleness window; restarting it recovers the cluster without restart.
6. Killing the proxy does the same via heartbeat loss.
7. `candump -l vcan1` from a session replays through `canplayer` and drives the proxy identically.
8. Unit tests for contract encode/decode, the OBD/ISO-TP library, and the cluster reader pass in CI.

A first real-car session (fossil) is the first v2 bucket, not a v1 gate.

## 4. Context: the three repositories

```
 vehicle side (vcan1 / can0)          contract side (vcan0)
 ┌──────────────────────┐   OBD-II   ┌──────────────────────┐  0x400..0x450  ┌───────────────────┐
 │ car-can-emulator     │ <────────> │ car-can-proxy        │ ─────────────> │ qt-cluster-demo   │
 │ --car=ice|ev|hybrid  │  UDS/ISO-TP│ can-proxyd + plugin  │   broadcast    │ --source=proxy    │
 └──────────────────────┘            └──────────────────────┘                └───────────────────┘
        or a real car                  owns all vehicle detail                 owns all display detail
```

Theme names used in this document: the cluster has three themes, referred to
here as `analog` (needle dials), `ev` (bar gauges, state of charge, range) and
`adas` (eco coach ring plus a driver-assist road view). The cluster's own
command-line name for the third theme may differ; this repository uses only
the generic name.

| Repo | Today | Role in this project |
|---|---|---|
| `car-can-proxy` | README only | New. Daemon, plugin ABI, plugins, contract header, tests |
| `qt-cluster-demo` | Direct OBD-II poller + `0x420` telltales; draft contract in `docs/can-proxy-interface.md`; EV/ADAS themes unbacked | Gains a contract reader (`--source=proxy`), signal validity in `ClusterModel`, theme auto-select |
| `car-can-emulator` | Single-file J1979 responder for six PIDs, netcat control on 8080, ICE only | Gains `--car=` drivetrain modes, EV/hybrid reference DIDs over ISO-TP, `0x420` telltale broadcast, more netcat knobs |

Rule: **the contract bus is never the vehicle bus.** The proxy reads the
vehicle on one interface and publishes the contract on another. This keeps
OEM frame IDs from colliding with contract IDs and keeps the cluster from ever
seeing raw vehicle traffic.

## 5. The contract: v1 draft adopted, v1.1 revisions proposed

The draft in `qt-cluster-demo/docs/can-proxy-interface.md` is adopted as the
base. Its design rules (broadcast only, physical units, SNA for every signal,
little-endian, conditioning in the proxy, read-only to the vehicle) are
carried over unchanged. This section lists only what changes. Each revision
is a decision in §12; nothing below is final until checkpoint 1.

### 5.1 `0x400` heartbeat — capability bitmap widened to 32 bits (R1)

Bytes 4-7 become a `uint32 LE` bitmap (bytes 6-7 were reserved-zero, so an
old reader sees no change). Minor version becomes `1`.

| Bit | Signal | | Bit | Signal |
|---|---|---|---|---|
| 0 | speed | | 9 | ambient temperature |
| 1 | motor/engine speed | | 10 | coolant temperature |
| 2 | gear | | 11 | fuel level |
| 3 | motor power | | 12 | auxiliary battery voltage |
| 4 | pack voltage / current | | 13 | state of health |
| 5 | state of charge | | 14 | charging state |
| 6 | range | | 15 | cabin temperature |
| 7 | consumption | | 16 | power state |
| 8 | odometer | | 17 | driver-assist frame `0x450` |
| | | | 18-31 | reserved |

Why: the draft had three spare bits and already listed 13 signals; the draft's
own open point (charging state, power state) needs bits, and any ADAS-theme
signal needs one more.

### 5.2 `0x401` vehicle identity, 1000 ms — new (R2)

| Byte | Field | Encoding |
|---|---|---|
| 0 | drivetrain | 0=unknown, 1=ICE, 2=BEV, 3=HEV, 4=PHEV, 5=FCEV |
| 1 | source kind | 0=simulated, 1=emulator, 2=live vehicle, 3=replay |
| 2 | plugin major | |
| 3 | plugin minor | |
| 4-7 | vehicle id | uint32 LE, FNV-1a of `plugin-name` + VIN when known; `0` = unknown |

Why: (a) the cluster can offer `--theme=auto` without knowing anything about
the car beyond a drivetrain enum; (b) the cluster can watermark a display
that is not fed by a live vehicle, which matters for an honest demo; (c) a
log line can say which plugin fed which session. It carries no signal, so it
does not weaken the boundary.

### 5.3 `0x420` telltales — widened to 32 bits (R3)

Bytes 0-3 become `uint32 LE`. Bits 0-11 keep the `DemoSimulator` assignments
(existing `CanReader::decodeTelltales` reads bytes 0-1 and is unaffected).

| Bit | Lamp | | Bit | Lamp |
|---|---|---|---|---|
| 12 | EV ready | | 16 | parking brake |
| 13 | charging | | 17 | low fuel |
| 14 | limited power (turtle) | | 18 | fog lamp |
| 15 | low traction battery | | 19 | hybrid/EV system fault |
| | | | 20-31 | reserved |

Why: the draft flagged 16 bits as tight; an EV needs at least four lamps the
ICE set lacks; a hybrid needs both sets. NCAP/DMS indicators are deliberately
*not* placed here because they do not come from the vehicle.

### 5.4 `0x450` driver assist and eco, 200 ms — new, optional (R4)

Published only when capability bit 17 is set.

| Byte | Field | Encoding | SNA |
|---|---|---|---|
| 0 | eco score | uint8 0-100 | `0xFF` |
| 1 | posted speed limit | uint8 km/h, 0 = none known | `0xFF` |
| 2 | forward collision risk | 0=none, 1=low, 2=medium, 3=high | `0xFF` |
| 3 | lane state | b0 left lane seen, b1 right lane seen, b2 left departure, b3 right departure | `0xFF` |
| 4-5 | lead vehicle gap | uint16 LE, 0.1 m | `0xFFFF` |
| 6-7 | res | | |

Why: the ADAS theme today derives collision risk and fakes a speed limit.
Defining the frame lets the theme be honest: show the badge when the bit is
set, hide it otherwise. **In v1 only the `sim` plugin will populate it**, so
the frame is speculative but confirmed at CP0 (D8). Its plugins are the
only ones that will change when a real source appears.

### 5.5 Draft open points, resolved

| Draft open point | Resolution |
|---|---|
| Is `0x410` power state the cluster's business? | Yes, keep it. The cluster needs "ignition off" to blank gauges rather than show zero, and "ready" for the EV ready lamp. Given capability bit 16. |
| Capability bitmap fixed or runtime-changing? | Runtime-changing but rare. A plugin sets it after discovery and may raise bits as ECUs wake; it must never toggle a bit per sample. The cluster re-evaluates gauge visibility on change, with a 2 s hold-down so a flapping bit cannot flicker the layout. |
| Keep charging state in `0x430`? | Keep. The emulator EV mode will drive it and telltale bit 13 mirrors it. |
| 16 telltale bits tight? | Widened, §5.3. |

### 5.6 Timing table additions

| Frame | Cycle | Cluster ages out after |
|---|---|---|
| `0x401` | 1000 ms | 4000 ms |
| `0x450` | 200 ms | 800 ms |

Total offered load rises from ~65 to ~71 frames/s. Still under 1% of a
500 kbit/s wire.

### 5.7 Single source of truth

`car-can-proxy/contract/can_proxy_contract.h` (plain C, header-only) will hold
every ID, cycle time, staleness window, SNA constant, scale factor, enum and
bit assignment, plus pack/unpack helpers. Both the proxy and the cluster
compile against it; the cluster vendors a copy pinned to a contract version
tag. `docs/can-proxy-interface.md` in the cluster repo is regenerated to match
and remains the human-readable spec.

## 6. Proxy architecture

### 6.1 Components

```
 can-proxyd
 ├── core/
 │   ├── PluginHost        dlopen, ABI check, lifecycle (create/start/stop/destroy)
 │   ├── VehicleState      normalised signals + per-signal validity + capabilities
 │   ├── Publisher         per-frame cycle timers → contract encode → SocketCAN raw TX
 │   ├── StateMachine      starting → no-vehicle → degraded → ok, driven by plugin reports
 │   ├── Config            CLI flags, optional JSON file
 │   └── Log               journald-friendly stderr
 ├── contract/             can_proxy_contract.h + tests
 ├── libobd/               J1979 client, capability discovery, UDS 0x22 client, ISO-TP (kernel can-isotp)
 └── plugins/
     ├── sim.so            scripted drive cycle, any drivetrain, no CAN input
     ├── obd2-ice.so       J1979 poller; works with emulator --car=ice and a real fossil car
     ├── emu-ev.so         J1979 subset + emulator EV reference DIDs over ISO-TP
     └── emu-hybrid.so     union of the two above
```

### 6.2 Plugin ABI (C, versioned)

```c
#define CANPROXY_ABI_VERSION 1

struct canproxy_vehicle_state;          /* physical units, one `valid` bit per signal,
                                           capability bitmap, drivetrain, telltales */
struct canproxy_host {                  /* what a plugin may call */
    void (*publish)(void *host, const struct canproxy_vehicle_state *s);
    void (*set_link)(void *host, int vehicle_present);   /* drives proxy state */
    void (*log)(void *host, int level, const char *fmt, ...);
    void *ctx;
};
struct canproxy_plugin {
    uint32_t abi_version;
    const char *name;
    uint8_t version_major, version_minor;
    void *(*create)(const struct canproxy_plugin_args *args, const struct canproxy_host *host);
    int   (*start)(void *self);
    void  (*stop)(void *self);
    void  (*destroy)(void *self);
};
const struct canproxy_plugin *canproxy_plugin_entry(void);
```

- Plugins **push** state; the core keeps the latest snapshot and publishes on
  the contract schedule. A plugin that goes quiet does not stop the heartbeat.
- Plugins own their I/O thread(s). The core copies the snapshot under a mutex;
  no plugin code runs on the publisher thread.
- The ABI carries **no CAN frames and no contract IDs**. A plugin cannot
  reach the contract bus. This is what makes G2 checkable.
- `sim` is built as a `.so` too, so the ABI is dogfooded from bucket 2.
- One plugin per daemon instance in v1. Composition (hybrid = ICE + EV) is done
  inside `emu-hybrid` by linking both tables against `libobd`, not by loading
  two plugins.

### 6.3 Publisher

Userspace timers on a single thread with a 10 ms tick, each frame on its own
cycle. `can-bcm` was considered and rejected for v1: the payload changes every
cycle anyway (rolling counter, live values), so BCM saves nothing and adds a
second code path. Revisit if the proxy ever runs on a loaded real wire.

### 6.4 State machine

| State | Meaning | Plugin signal |
|---|---|---|
| 0 starting | daemon up, plugin not yet reporting | before first `set_link` |
| 1 no vehicle | plugin reports no ECU answering / bus silent | `set_link(0)` |
| 2 degraded | vehicle present but a capability-bit signal is stale | `set_link(1)` with stale signals |
| 3 ok | vehicle present, all advertised signals fresh | `set_link(1)`, all fresh |

In states 0 and 1 every signal is SNA; capability bits still reflect what the
plugin *could* provide so the cluster layout does not collapse.

### 6.5 Configuration

```
can-proxyd --vehicle-if=<if> --contract-if=<if> --plugin=<name|/path.so>
           [--plugin-arg key=value ...] [--record=<file>] [--log-level=...]
           [--config=<json>]
```

`--plugin=<name>` resolves to `<prefix>/lib/can-proxy/plugins/<name>.so`.

### 6.6 Record and replay

`--record` writes the *vehicle side* in `candump -l` format. Replay is
`canplayer -I <log> vcan1` with the daemon unchanged; no replay plugin.
Captures are the test fixtures for CI and for plugins developed after the car
has gone.

## 7. Changes to `qt-cluster-demo`

1. `ContractReader` (new, alongside `CanReader`): SocketCAN raw on the
   contract interface, kernel filter on the contract IDs, per-frame decode via
   the vendored header, per-frame staleness timers, heartbeat loss →
   `canConnected=false` and all signals invalid.
2. `ClusterModel`: a validity flag per EV/thermal/trip signal (pattern from
   `DmsModel`); `powerFlow` carried in kW with the gauge scaling by a
   configurable maximum, as `ev-can-signals.md` recommends; `drivetrain` and
   `sourceKind` properties from `0x401`; `capabilities` property.
3. QML: gauges bind visibility to capability bits and render dash/blank on
   invalid; optional "SIM"/"REPLAY" watermark from source kind.
4. CLI: `--source=demo|can|proxy` (default `demo`), `--contract-if=<if>`,
   `--theme=auto`. `--demo` and `--can=` remain as aliases.
5. Tests: `contract_reader_tests` feeding hand-built frames and timing.

The cluster keeps no PID, DID, or vehicle-ECU knowledge in the new path.

## 8. Changes to `car-can-emulator`

1. `--car=ice|ev|hybrid` (default `ice`, current behaviour).
2. `0x420` telltale broadcast at 100 ms, driven by the drive script and
   settable via netcat (`tt set 0x0010`), so the emulator matches what
   `DemoSimulator` assumes.
3. EV reference profile: the emulator acts as a BMS/drive ECU on `0x7E4/0x7EC`
   answering UDS `0x22` for a small documented DID set (pack V/I, SoC/SoH,
   range/consumption, gear/power state/charging). Responses are multi-frame
   ISO-TP so the proxy's ISO-TP path is genuinely exercised before a real
   EV appears. DIDs are documented in `docs/emulator-ev-profile.md`.
4. Standard PIDs added where they exist: `0x46` ambient, `0xA6` odometer,
   `0x5B` (EV/hybrid only), `0x2F` fuel (ICE/hybrid), `0x42` module voltage,
   plus honest supported-PID bitmaps for `0x00/0x20/0x40/0x60/0x80/0xA0`.
5. Netcat knobs: `soc`, `soh`, `range`, `power`, `packv`, `packi`, `gear`,
   `pwrstate`, `chg`, `odo`, `ambient`, `tt`.
6. Optional `--drive-cycle` scripted mode mirroring `DemoSimulator` phases,
   so a demo needs no hand input.

Structure: split the single file into `obd/`, `sim/`, `control/` modules but
keep it a single executable and the same CMake/OpenWrt install.

## 9. Test strategy

| Layer | Test | Where |
|---|---|---|
| Contract | pack/unpack round-trips, SNA, bit assignments, version constants | `car-can-proxy/contract/tests` |
| libobd | J1979 decode tables, supported-PID bitmap parsing, UDS 0x22 parsing, ISO-TP against kernel `can-isotp` on vcan | `car-can-proxy/libobd/tests` |
| Daemon | integration: emulator on vcan1, daemon, `candump vcan0` assertions on cycle times and values | `car-can-proxy/tests/integration` (GitHub Actions, `vcan` module) |
| Cluster reader | decode + staleness state machine from synthetic frames | `qt-cluster-demo/tests/contract_reader_tests.cpp` |
| Replay | recorded emulator sessions replayed into the daemon, output compared to golden `candump` | `car-can-proxy/tests/replay` |
| Manual | the eight steps in §3 on a Pi | checklist in `docs/bench-checklist.md` |

## 10. Deployment

- `systemd/can-proxyd.service` on the Pi, ordered before
  `qt-cluster-demo.service`; `systemd/vcan.service` (or a `systemd-networkd`
  unit) to create `vcan0`.
- The cluster's `build-and-deploy.sh` gains `--mode=proxy`.
- Bench topology on one host: `vcan1` vehicle side, `vcan0` contract side.
  Pi with a real car: `can0` (CANable or MCP2515 hat) vehicle side, `vcan0`
  contract side.

## 11. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Contract changes ripple into three repos | Rework | Header is the single source of truth, versioned; bucket 1 freezes it before any code depends on it |
| `can-isotp` module absent on a target | EV plugin blocked | Check at daemon start with a clear error; kernel module is in Raspberry Pi OS and mainline; userspace fallback is v2 |
| The emulator's EV profile is invented | Real EV plugin differs | Profile is explicitly "reference", modelled on common BMS DID shapes; the value of the exercise is the ISO-TP/UDS path, not the DID numbers |
| Cluster model refactor (validity, kW) touches all themes | Regression in demo mode | Do it behind `--source=proxy` first; demo path keeps defaults until validated |
| Timing on Pi 4 with Qt eglfs and a 10 ms publisher | Jitter | Publisher thread at SCHED_OTHER first; measure with `candump -t d`; escalate to SCHED_FIFO only if needed |

## 12. Decision log

Status: **C** confirmed with you in the interview, **P** proposed here, needs
your explicit yes/no at the named checkpoint.

| ID | Decision | Status | Verified at |
|---|---|---|---|
| D1 | Purpose is proving the boundary; bench first, real car second | C | every checkpoint (does the bucket keep the cluster vehicle-agnostic?) |
| D2 | Transport is broadcast classic CAN frames on a dedicated `vcan` contract interface, per the draft | C | CP1 |
| D3 | Extension model is C++ `.so` plugins behind a C ABI; no JSON profiles in v1 | C | CP2 |
| D4 | All three repos change; emulator becomes a multi-drivetrain reference vehicle | C | CP0 |
| D5 | v1 done = bench with ICE, EV, hybrid via emulator; real fossil car is v2 bucket 1 | C | CP0, CP6 |
| D6 | R1 32-bit capability bitmap and R3 32-bit telltales, backward-compatible widening | P | CP1 |
| D7 | R2 vehicle identity frame `0x401` | P | CP1 |
| D8 | R4 driver-assist frame `0x450` stays in the contract; sim-only in v1 | C | CP0 |
| D9 | Power state stays in `0x410`; charging state stays in `0x430`; capability bits may change at runtime with hold-down | P | CP1 |
| D10 | Contract bus is never the vehicle bus; two interfaces always | P | CP1 |
| D11 | Plugins push state, core owns schedule; userspace publisher, no `can-bcm` | P | CP2 |
| D12 | Emulator EV/hybrid signals come via UDS `0x22` + ISO-TP on `0x7E4/0x7EC`, not extra broadcast frames, so the ISO-TP path is exercised | C | CP0 |
| D13 | Cluster `ClusterModel` refactor (validity flags, `powerFlow` in kW) is done in this project, under a no-regression rule: `--demo` and `--can=` behaviour and the existing unit tests stay unchanged | C | CP0, re-checked CP3 |
| D15 | v2 real car is a Kia Picanto (ICE) on OBD-II via a CANable USB dongle on the Pi 4 (`can0`, 500 kbit/s, 11-bit expected) | C | CP7 |
| D14 | One plugin per daemon; hybrid is a composed plugin, not two loaded plugins | P | CP2 |

## 13. Checkpoint 0 outcome (2026-09-04)

| Question | Answer |
|---|---|
| Emulator EV signals via UDS/ISO-TP or broadcast? | UDS `0x22` over ISO-TP, so the proxy is prepared for a real EV (D12) |
| Keep `0x450` driver-assist frame? | Yes, keep it in the contract (D8) |
| Real car for v2? | Kia Picanto, OBD-II port, CANable USB-to-CAN dongle on the Pi 4 (D15) |
| Cluster model refactor in scope? | Yes, adapt the cluster together with the proxy, with no regression to existing modes (D13) |

The no-regression rule for the cluster is made concrete in bucket 3: the
existing `cluster-model-tests`, `dms-*` tests and the `--demo` drive cycle
must behave identically before and after, and `--source=can` is not touched.
