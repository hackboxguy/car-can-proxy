# Extension plan (post-v1)

For the next session. v1 is complete (`docs/handover-next-session.md`);
this orders what comes next. Same working agreement as v1: a short plan
per bucket, a checkpoint after it, the boundary question at every
checkpoint (no vehicle knowledge in the cluster, no display knowledge in a
plugin). Details and frame sketches live in `docs/future-extensions.md`.

## Priority 1: transport extension (contract v1.2)

Why first: every gauge in the reference set that is not on the contract
today waits on it, and it is the only part with a compatibility rule to
get right.

| Bucket | Scope | Checkpoint |
|---|---|---|
| T1 contract v1.2 | the new frames from `future-extensions.md` (limits, dynamics, tires, engine detail, cruise/mode, occupancy, trip, charging, position/attitude, navigation) in the header with pack/unpack and golden tests; capability bits 18-27; versioning doc change log; tag `contract-v1.2` | a v1.1 cluster still runs unchanged against a v1.2 proxy (the regression check for "additive") |
| T2 ABI v2 and daemon | vehicle state struct grows; mapping tests pinned; publisher schedules the new frames; `sim` fills all of them from the cycle | `contract-dump` shows every new frame on cycle from `sim` |
| T3 producers | emulator DIDs `0x0105`-`0x0109` and knobs; `emu-ev`/`emu-hybrid` read them; `obd2-ice` adds oil, intake, barometric PIDs; `forza` fills limits, dynamics, tires, engine detail, position | integration tests extended; the bench hybrid carries every new frame |
| T4 cluster reader | decoder, snapshot and model gain the new signals with validity and capability, logged in the journal like the rest; vendored header updated | reader tests; no visual change yet |

## Priority 1: beautification and UI/UX refinement

Runs in parallel with T-buckets where it does not depend on them.

| Bucket | Scope | Depends on | Checkpoint |
|---|---|---|---|
| U1 units | `--units=metric|imperial` in the cluster: numerals, unit labels and gauge scales (mph ring, °F, miles, mpg or mi/kWh); default metric; demo and proxy paths | nothing | screenshots of every theme in both settings |
| U2 gauge language | a ring-gauge component in the reference set's idiom (arc, large numeral, unit, small readout at the foot, LIMIT badge, set-speed marker) reusable across themes; the analog theme keeps its needles | nothing for the ring; T4 for set-speed | the EV theme's speed ring and the third theme's speed panel rebuilt on it, pixel-compared before and after in demo mode |
| U3 lamp layout | two short lamp rows grouped by meaning and colour instead of one strip, per the real-cluster photos; clock, badge and DMS group placement re-solved together | nothing | all three themes at 1920×720 and 1280×480, with and without DMS |
| U4 tile theme | a fourth theme: a grid of tiles that appear only for capabilities the vehicle has (tires, trip, charging, cruise, g-meter, compass, belts/doors), each a small QML component | T4 | the bench hybrid shows all tiles, `obd2-ice` on the dongle shows the few it can, nothing invented |
| U5 dark-panel default and motion | `minDarkLevelEnabled` default off on the proxy path; needle and ring easing tuned against the demo cycle; bulb-check choreography (staged, like a real cluster) | nothing | side-by-side captures |

## Priority 2: gaming

| Bucket | Scope | Depends on |
|---|---|---|
| G1 Forza features | traction/ABS lamps from slip, lane bits from rumble strips, line coach; then the T-frames the plugin can fill | T3 |
| G2 moving map of the game world | breadcrumb trail first, then the calibrated map raster panned and rotated by position and heading (`0x460`/`0x461`) | T1, T4 |
| G3 BeamNG plugin | OutGauge stream: real dash-light flags for the lamp row, temperature, turbo | nothing |

## Priority 2: maps for a real car

| Bucket | Scope | Depends on |
|---|---|---|
| M1 `gps` plugin | NMEA from a USB receiver or a phone GPS-sharing app into `0x460`/`0x461` | T1 |
| M2 OpenStreetMap backdrop | Qt Location with an offline tile cache; measure CPU on the Pi before committing; Google Maps is out (offline embedded licensing) | M1 |

## Priority 3: phone integration

| Bucket | Scope |
|---|---|
| P1 Android Auto window | an `aasdk`-based bridge on the Pi feeding the cluster's frame renderer; first milestone is the handshake and decoded frames with no cluster change |
| P2 navigation card | the bridge's navigation-status channel into `0x480` via a `navigation` plugin |

## Still open from v1 (any time, independent)

- The Kia Picanto session with `--record` (the first real car).
- UDS-aware replay; userspace ISO-TP fallback (also unblocks CI's UDS test).
- The two ADAS lamp symbols once the design-system library key exists.
