# Handover: car-can-proxy

Written 2026-09-04 at the end of the session that designed and built v1.
Everything below is done and verified on the Pi 4 unless listed under
"Open items". Read `docs/prd.md` §1 and §12 for the why, `docs/plan.md` for
the how, and this page for where things are and how to drive them.

## Where things stand

| Repo | Head | Role |
|---|---|---|
| `car-can-proxy` (public) | `61397a6` | `can-proxyd`, contract, plugins, tools, services |
| `car-can-emulator` (public) | `e730957` | bench vehicle: OBD-II ECU, UDS/ISO-TP battery ECU, drive cycle |
| `qt-cluster-demo` (private) | `b129a04` | the cluster; `--source=proxy` reads the contract |
| `misc-tools` (private) | `263b4d4` | Pi image builder; hooks install all of the above |

Image **01.13** is on the Pi 4 at `192.168.1.94` (user `pi`): proxy path,
hybrid bench with the demo drive cycle, `--theme=auto`, DMS live. A CANable
(gs_usb, USB `1d50:606f`) with a hardware OBD-II emulator behind it is
attached as `can0`. The Pi's SSH host key changes on every reflash.

v1 is complete: the same cluster binary is driven by ICE, EV and hybrid
bench vehicles, by a hardware OBD-II emulator over a real dongle, and by a
synthetic Forza stream, with zero vehicle knowledge in the cluster. That
was the project's one question (PRD §1) and the answer is yes.

## The pieces

- **Contract** `contract/can_proxy_contract.h`, frozen at tag
  `contract-v1.1`; rules in `docs/contract-versioning.md`; the cluster
  vendors a copy. Human-readable spec in
  `qt-cluster-demo/docs/can-proxy-interface.md`.
- **Daemon** `core/`: plugin host, state machine (starting / no-vehicle /
  degraded / ok), absolute-deadline publisher. `--record` writes the
  vehicle side in candump format.
- **Plugin ABI** `include/canproxy/plugin.h`, guide in
  `docs/plugin-authoring.md`. A plugin pushes physical units with a
  capable/valid bit per signal and never sees the contract bus.
- **Plugins**: `sim` (no bus), `obd2-ice` (J1979), `emu-ev` / `emu-hybrid`
  (J1979 + UDS DIDs to the emulator's battery ECU, `docs/emulator-ev-profile.md`),
  `forza` (game telemetry over UDP, `docs/forza.md`).
- **libobd**: J1979 client with ECU stickiness and bitmap discovery,
  Poller, kernel ISO-TP channel, UDS `0x22` (read-only by construction).
- **Tools**: `contract-dump` (decode, `--stats` timing/jitter/counter
  checks), `can-replay` (`--timed` like canplayer, `--respond` answers
  recorded OBD requests so a poller session replays deterministically).
- **Services**: `systemd/can-proxy-links` (vcan for the contract, bitrate
  or vcan for the vehicle, `can_isotp`), `systemd/can-proxyd`;
  `scripts/deploy.sh` writes `systemd/can-proxyd.env` and enables both.
- **Tests**: 11 unit, 5 integration (label `integration`, need vcan0/vcan1,
  SKIP without). CI runs all but the UDS one (GitHub's kernel lacks
  `can_isotp`). `tests/fixtures/` holds the real-dongle recording.

## Recipes

**Bench on one box** (`scripts/bench-up.sh` creates vcan0/vcan1):

```bash
car-can-emulator --node=vcan1 --car=hybrid --drive-cycle=cycles/demo.cycle &
can-proxyd --contract-if=vcan0 --vehicle-if=vcan1 --plugin=build/plugins/emu-hybrid.so --plugin-arg source=emulator &
contract-dump vcan0 --stats=44 --require-counter
echo -n "risk 3" | nc 127.0.0.1 8080      # knobs; "reset"; "cycle stop|start"
```

**The CANable and the hardware OBD-II emulator, by hand on the Pi.**
Stop the service first: two publishers on `vcan0` would interleave.

```bash
sudo systemctl stop can-proxyd
sudo ip link set can0 down; sudo ip link set can0 type can bitrate 500000; sudo ip link set up can0   # no restart-ms: gs_usb refuses it
cd /home/pi/car-can-proxy
./build/core/can-proxyd --contract-if=vcan0 --vehicle-if=can0 \
    --plugin=build/plugins/obd2-ice.so --plugin-arg source=live --record=/home/pi/session.log
# the cluster follows on its own: drivetrain ice -> analog theme, badge off (source live)
./build/tools/contract-dump vcan0                     # second terminal
sudo systemctl start can-proxyd                       # back to the bench afterwards
```

The same as the service, which survives reboots and brings `can0` up
itself: edit `/home/pi/car-can-proxy/systemd/can-proxyd.env` to
`VEHICLE_IF=can0`, `PLUGIN=obd2-ice`, `PLUGIN_ARGS=--plugin-arg source=live`,
then `sudo systemctl restart can-proxy-links can-proxyd`. From a shell,
`--plugin=<name>` resolves in `/usr/local/lib/can-proxy/plugins` unless
`CANPROXY_PLUGIN_DIR` is set; the service sets it to the build directory,
so outside the service use the `.so` path as above.

Expect on that device: bitmaps that claim every PID and zero answers for
`0x42`/`0x46`, which `obd2-ice` drops with one warning each. A real car
will not do that.

**Forza**: `docs/forza.md`. `PLUGIN=forza`,
`PLUGIN_ARGS=--plugin-arg port=1101 --plugin-arg drivetrain=ice`, game's
Data Out to the Pi's IP and port 1101. Verified with a synthetic stream
across the LAN; not yet with a real game.

**Panel capture on the Pi**: `docs/bench-checklist.md`, section
"Capturing the panel" (stop the service, `--screenshot`,
`QT_FORCE_STDERR_LOGGING=1`).

**Replay a recording**: `can-replay --respond vcan1 session.log &` then
the daemon with `--plugin-arg source=replay`; the cluster shows `REPLAY`.

## Traps

- `pkill -f`/`pgrep -f` with a name that appears in your own command line
  kills your shell. Kill by exact comm (15 chars: `car-can-emulato`,
  `can-proxyd`), see `tests/integration/*.sh` for the pattern.
- A stale emulator on `vcan1` answers alongside a new one (both `0x7E8`);
  check `pgrep -x car-can-emulato` before a bench run.
- The emulator's control port refuses a restart within a minute unless
  `SO_REUSEADDR` is set (it is now); a "Connection refused" on 8080 means
  another emulator holds the port.
- `ip link set canN type can ... restart-ms 100` fails on gs_usb; the
  links unit falls back, hand commands must omit it.
- Qt logs to journald when stderr is not a tty: `QT_FORCE_STDERR_LOGGING=1`.
- The offscreen Qt platform crashes on the dev host; use `xvfb-run` for
  headless cluster screenshots.
- A chroot image build shares the host's network namespace: only
  `ctest -LE integration` may run there (deploy.sh does), and the
  integration tests own vcan0/vcan1 while they run.
- Python's stdout is block-buffered under `timeout`; use `python3 -u`.
- The proxy repo is public: no vendor names in it (the third theme is
  "adas" here).

## What comes next

`docs/extension-plan.md` orders the post-v1 work: transport extension
(contract v1.2) and UI refinement first, gaming and maps second, phone
integration third. `docs/future-extensions.md` holds the reasoning and the
frame sketches behind it.

## Open items (in order of value)

1. **The Kia Picanto.** Same recipe as the CANable above with the car on the
   dongle; `--record` on so the session becomes a fixture. Watch for
   11-bit/500 kbit/s assumptions, the first-responder rule against several
   ECUs, and poll timing on a real bus. This is what turns `obd2-ice` from
   emulator-proven into car-proven.
2. **A real Forza title** against the `forza` plugin (only the synthetic
   stream has been seen). The journal line "telemetry format ..." is the
   first thing to check.
3. **UDS-aware replay**: `--record` captures only the raw channel, so an EV
   session replays its J1979 half only.
4. **Userspace ISO-TP fallback**, which also lets CI run `emu_ev_test`.
5. **JSON-described vehicle profiles** on top of `libobd` (the
   `ev-can-signals.md` tier-2 idea), and a real EV plugin from a community
   DID set.
6. `0x450` on a real vehicle: today only the emulator's assist DID and
   `sim` fill it.
7. Gaming and map ideas, explored but not started: `docs/future-extensions.md`
   (per-car gauge limits, g-ball, tires, race panel, a moving map of the
   game world, a BeamNG plugin for real telltales, GPS + OpenStreetMap for
   a real car).

## Working agreement that produced this

Interview first, then small buckets, each with a plan up front and a
checkpoint after, with decisions re-verified against PRD §1 at every
checkpoint. Three integration rounds with the image-builder session
followed the same shape (notes in the workspace's `tmp-docs/`).
