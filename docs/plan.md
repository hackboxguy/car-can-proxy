# car-can-proxy — Implementation Plan

**Status:** v0.2, checkpoint 0 passed (2026-09-04)
**Companion:** `docs/prd.md` (requirements, contract revisions, decision log)

## Working agreement

- One bucket at a time. Each bucket is small enough to review in one sitting
  and leaves every repo buildable and every test green.
- Before a bucket starts: a short plan for that bucket (scope, files,
  acceptance test, which decisions it verifies). After it ends: a checkpoint
  where you review the output before the next bucket is planned.
- Every checkpoint re-asks the boundary question from PRD §1: *did this bucket
  add any vehicle knowledge to the cluster, or any display knowledge to a
  plugin?* If yes, the bucket is not done.
- Decisions marked **P** in the PRD decision log are confirmed at the
  checkpoint named there; a bucket does not start on an unconfirmed decision
  it depends on.
- Work lands per repo on a feature branch; commits only when you ask.

## Bucket map

| Bucket | Repo(s) | Outcome | Checkpoint |
|---|---|---|---|
| 0 | proxy (docs) | PRD + plan, decisions D1-D5 confirmed, D12 and D8 answered | CP0 passed |
| 1 | proxy, cluster (docs) | Contract v1.1 frozen: header + tests + regenerated spec | CP1 passed, tagged `contract-v1.1` |
| 2 | proxy | Daemon core + plugin ABI + `sim.so`; frames on `vcan0` | **CP2 — in review** |
| 3 | cluster | `ContractReader`, `--source=proxy`, validity, staleness; all themes from `sim` | CP3 |
| 4 | proxy, emulator | `libobd` + `obd2-ice.so` against `--car=ice`; analog theme end-to-end | CP4 |
| 5 | emulator, proxy | Emulator EV/hybrid + ISO-TP; `emu-ev.so`, `emu-hybrid.so`; EV and ADAS themes end-to-end | CP5 |
| 6 | all | systemd, deploy script, record/replay, CI, READMEs; v1 done review | CP6 |
| v2-1 | proxy | First real-car session: Kia Picanto via CANable on Pi 4, with capture | CP7 |

Dependency: 1 → 2 → 3, and 2 → 4 → 5. Bucket 3 and bucket 4 are independent
after 2, so we may choose the order at CP2.

---

## Bucket 0 — Definition (this document)

**Goal.** Agree on the purpose, the contract revisions to propose, the plugin
model, and the shape of v1.

**Deliverables.** `docs/prd.md`, `docs/plan.md`.

**Decisions verified.** D1-D5 confirmed in the interview. D12 and D8 need an
answer here because they change the shape of buckets 5 and 1 respectively.

**CP0 acceptance.** You have read the PRD, answered the four open questions
in PRD §13, and approved starting bucket 1.

---

## Bucket 1 — Freeze contract v1.1

**Goal.** One header that both sides compile against, with tests, before any
runtime code exists.

**Scope.**
- `car-can-proxy/contract/can_proxy_contract.h`: IDs, cycles, staleness
  windows, SNA constants, scales, enums, capability and telltale bits, and
  `pack_*` / `unpack_*` helpers per frame. Plain C99, header-only, no
  allocation, no dependencies, so the Qt side can vendor it as-is.
- `car-can-proxy/contract/tests/`: round-trip every field, every SNA, every
  enum boundary; assert `0x420` bits 0-11 equal `DemoSimulator` values;
  assert reserved bytes pack as zero.
- `qt-cluster-demo/docs/can-proxy-interface.md` rewritten to v1.1 from the
  header, with a change log section. Draft "open points" section replaced by
  PRD §5.5 resolutions.
- `car-can-proxy/docs/contract-versioning.md`: how minor (additive) and major
  (breaking) bumps are handled; how the cluster pins a copy.
- CMake for the proxy repo (top level, `contract/` target, ctest).

**Not in scope.** Any daemon code, any cluster code.

**Decisions verified.** D2, D6, D7, D8, D9, D10.

**CP1 acceptance.**
- `ctest` green in `car-can-proxy`.
- You have read the regenerated spec and confirmed each R1-R4 revision
  individually (any may be dropped; only R1 is needed by later buckets).
- Header tagged `contract-v1.1`.

---

## Bucket 2 — Daemon core, plugin ABI, `sim` plugin

**Goal.** `can-proxyd --plugin=sim --contract-if=vcan0` produces the full
frame set on schedule, with a heartbeat that survives the plugin doing
nothing.

**Scope.**
- `core/`: `PluginHost` (dlopen, ABI version check, lifecycle),
  `VehicleState` (physical units + validity + capabilities + drivetrain),
  `Publisher` (10 ms tick, per-frame cycles, rolling counter, encode via
  contract header, SocketCAN raw TX), `StateMachine` (starting / no vehicle /
  degraded / ok), `Config` (CLI), `Log`.
- `include/canproxy/plugin.h`: the C ABI from PRD §6.2, versioned.
- `plugins/sim/`: scripted drive cycle mirroring `DemoSimulator` phases,
  `--plugin-arg drivetrain=ice|bev|hev`, populates every frame including
  `0x401` (source kind = simulated) and `0x450` if D8 is kept.
- Tests: `PluginHost` with a stub `.so`; `Publisher` cycle-time test on vcan
  (`candump -t d` parsed, ±2 ms over 10 s); state machine transitions;
  "plugin stops publishing" keeps heartbeat and flips to degraded/no vehicle
  per rules.
- `scripts/bench-up.sh`: creates `vcan0`/`vcan1` idempotently.

**Not in scope.** Any vehicle-side CAN I/O. `libobd` starts in bucket 4.

**Decisions verified.** D3, D11, D14.

**Outcome (2026-09-04).** Delivered as planned plus `tools/contract-dump`
(a decoder and cycle-timing checker, so acceptance does not depend on
`can-utils`) and `tests/integration/sim_cycle_test.sh`, which runs the four
acceptance scenarios below on a real `vcan0` and reports SKIP where there is
none. Measured on the dev host: every frame on cycle with worst deviation
0.07 ms over 6 s, no counter skips. The ABI header carries no contract
symbol (`grep -c 0x4 include/canproxy/plugin.h` = 0). The ABI's enumerated
values use a `CP_` prefix so a host translation unit can include it next to
the contract header. Plugin timeout defaults to 1000 ms (`--plugin-timeout-ms`).

**CP2 acceptance.**
- `candump vcan0` (or `contract-dump vcan0 --stats`) shows `0x400`, `0x401`, `0x410`, `0x411`, `0x420`, `0x430`,
  `0x431`, `0x440` (and `0x450` if kept) at documented cycles for 60 s with no
  gaps; rolling counter has no skips.
- `kill -STOP` the sim plugin thread (test hook) → `0x400` state goes to
  degraded within 3 cycles, signals go SNA per staleness table, heartbeat
  never stops.
- Boundary question: the plugin ABI header contains no CAN ID and no contract
  symbol. `grep -c 0x4 include/canproxy/plugin.h` is `0`.

---

## Bucket 3 — Cluster contract reader

**Goal.** The unmodified cluster binary renders all three themes from the
`sim` plugin through the proxy, and reports "no vehicle data" correctly on
heartbeat loss.

**Scope (in `qt-cluster-demo`).**
- `src/ContractReader.{h,cpp}`: raw socket, kernel filter on contract IDs,
  decode via vendored `contract/can_proxy_contract.h`, per-frame staleness
  timers per the table, heartbeat loss → everything invalid + `canConnected`
  false, recovery without restart.
- `ClusterModel`: validity flags for each signal that can be SNA; `powerFlow`
  in kW plus `powerFlowMaxKw` (D13); `capabilities`, `drivetrain`,
  `sourceKind`, `proxyState` properties.
- QML: gauges hide on missing capability bit, render dash on invalid; SIM /
  REPLAY watermark from source kind; `--theme=auto` maps drivetrain → theme
  (ICE→analog, BEV→ev, HEV/PHEV→adas by default, overridable).
- CLI: `--source=demo|can|proxy`, `--contract-if`, keep `--demo`/`--can=`
  as aliases. `--source=can` path untouched.
- `tests/contract_reader_tests.cpp`: decode, SNA → invalid, staleness timing,
  heartbeat loss and recovery, capability hold-down.
- `docs/can-proxy-interface.md` gains a "cluster behaviour" section
  (what it does on each state).

**Not in scope.** Emulator, real CAN, any plugin work.

**Decisions verified.** D1 (boundary), D13.

**CP3 acceptance.**
- `qt-cluster-demo --source=proxy --contract-if=vcan0 --theme=analog|ev|adas`
  each renders a full drive cycle from `can-proxyd --plugin=sim` with the
  matching `drivetrain=` argument.
- `kill can-proxyd` → cluster shows no-vehicle within 500 ms; restart → cluster
  recovers with no restart.
- `sim` with `drivetrain=ice` under `--theme=ev`: SoC/range/power gauges are
  hidden or dashed, not showing defaults.
- Boundary grep on `src/ContractReader.cpp` and the new model code: no PID,
  DID, `0x7DF`, `0x7E8`, or vehicle name.

---

## Bucket 4 — `libobd` + `obd2-ice` plugin against the emulator

**Goal.** The analog theme runs end-to-end from `car-can-emulator --car=ice`
on `vcan1`, through the proxy, to the cluster on `vcan0`, and the same plugin
is written to the J1979 standard rather than to the emulator.

**Scope.**
- `libobd/`: SocketCAN raw client; J1979 Service 01 request/response with
  ECU response range `0x7E8-0x7EF` and a deliberate "first responder wins,
  then stick" rule; supported-PID discovery (`0x00/0x20/.../0xC0`); decode
  table for the PIDs we use (`0x04 0x05 0x0B 0x0C 0x0D 0x10 0x2F 0x42 0x46
  0x5B 0xA6`); poll tiers (fast / slow / rare) with a budget; link detection
  (N consecutive timeouts → link down).
- `plugins/obd2-ice/`: uses `libobd`, polls only discovered PIDs, fills
  `VehicleState` with validity, sets capability bits from discovery, never
  derives power from speed (PRD rule), sets drivetrain=ICE, source kind from
  `--plugin-arg source=emulator|live`.
- Emulator: minimal changes only — honest supported-PID bitmaps for the PIDs
  it actually serves, `0x420` telltale broadcast at 100 ms with a `tt`
  netcat knob, `0x46` and `0xA6` and `0x42` added. (`--car=` lands in
  bucket 5; ICE stays the default so nothing changes for current users.)
- `--record=<file>` in the daemon (vehicle side, candump format).
- Tests: `libobd` decode tables and bitmap parsing; integration test
  (emulator on vcan1 → daemon → `candump vcan0` assertions) runnable in CI.

**Not in scope.** ISO-TP / UDS, EV signals.

**Decisions verified.** D6 (capability bits from real discovery), D10.

**CP4 acceptance.**
- `echo -n "speed 90" | nc 127.0.0.1 8080` moves the analog speedometer.
- `candump vcan0` shows `0x400` capability bits exactly matching the PIDs the
  emulator advertises; EV bits are clear; `0x411`/`0x430` fields are SNA.
- Stop the emulator: proxy state → no vehicle within the link-detection
  window; cluster follows; restart recovers both.
- A recorded session replays via `canplayer` and yields the same `vcan0`
  output (golden compare with counters masked).

---

## Bucket 5 — Emulator EV/hybrid, ISO-TP, `emu-ev` and `emu-hybrid`

**Goal.** The EV and ADAS themes run end-to-end from emulated EV and hybrid
traffic, and the proxy's UDS/ISO-TP path is exercised for real.

**Scope.**
- Emulator: `--car=ice|ev|hybrid`; split into `obd/`, `sim/`, `control/`
  modules; EV reference ECU on `0x7E4/0x7EC` answering UDS `0x22` for the DIDs
  in `docs/emulator-ev-profile.md` (pack V/I, SoC/SoH, range/consumption,
  gear/power state/charging) as multi-frame ISO-TP; `0x5B` for ev/hybrid;
  fuel PIDs absent in `ev`; netcat knobs `soc soh range power packv packi
  gear pwrstate chg odo ambient`; optional `--drive-cycle`.
- `libobd`: UDS `0x22` client over kernel `can-isotp` sockets; graceful error
  when the module is absent; DID decode descriptors.
- `plugins/emu-ev/`: J1979 subset (speed, ambient, odometer) + EV DIDs;
  power from pack V×I filtered in the plugin; consumption derived from power
  and speed with documented filter; drivetrain=BEV; EV telltale bits.
- `plugins/emu-hybrid/`: union; both fuel and SoC valid; drivetrain=HEV.
- Tests: ISO-TP multi-frame round-trip against the emulator on vcan; DID
  decode; plugin capability bitmaps per drivetrain; integration for all
  three `--car=` modes.

**Not in scope.** Any real EV DID set.

**Decisions verified.** D12. D8 is confirmed; note at CP5 which plugins fill `0x450`

**CP5 acceptance.**
- `--car=ev` → `--theme=ev`: SoC, range, power flow, consumption, gear all
  live; `soc 42` and `power -30` via netcat move the gauges; fuel gauge absent.
- `--car=hybrid` → `--theme=adas`: fuel and SoC both live; capability bits
  are the union.
- `--car=ice` → `--theme=ev`: right-hand gauges hidden/dashed (G3).
- Same cluster binary hash for all three runs (G1), recorded in the checkpoint
  note.

---

## Bucket 6 — Deployment, CI, docs; v1 review

**Goal.** A stranger can reproduce PRD §3 from the READMEs.

**Scope.**
- `systemd/can-proxyd.service`, `systemd/vcan0.netdev`/service, env file;
  cluster `build-and-deploy.sh --mode=proxy`; ordering between units.
- GitHub Actions in `car-can-proxy`: build, unit tests, integration test with
  `vcan` + emulator built from a pinned commit, replay golden test.
- `docs/bench-checklist.md` (the eight §3 steps as a manual runbook),
  `docs/plugin-authoring.md` (ABI walkthrough with `sim` as the example),
  READMEs in all three repos updated.
- Measure publisher jitter on the Pi and record it.

**Decisions verified.** D5 (v1 done), D1 (final boundary audit across the
three repos).

**CP6 acceptance.** All eight steps in PRD §3 pass on a Pi 4 and on a PC,
from the checklist, by you. Decision on v2 investment.

---

## v2 candidates (not planned in detail)

1. Kia Picanto session over CANable: bit-rate/addressing detection, ECU range rule
   validated, capture archived, plugin fixes. First v2 bucket.
2. `obd2-profile.so`: a generic plugin driven by a JSON profile (the
   `ev-can-signals.md` tier-2 idea), built on `libobd`.
3. Real EV plugin from a community DID set (OBDb) plus a capture.
4. Userspace ISO-TP fallback for kernels without `can-isotp`.
5. `0x450` fed by something real, or removed.
6. OpenWrt package for the proxy.
