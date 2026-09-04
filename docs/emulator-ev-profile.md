# Emulator EV profile (reference)

What `car-can-emulator --car=ev|hybrid` exposes beyond J1979, and what the
`emu-ev` and `emu-hybrid` plugins read. It is a *reference* profile: the
DID numbers and layouts are invented, modelled on common battery-management
ECU shapes, so the proxy's UDS and ISO-TP path is exercised for real before
a real EV appears. A real EV plugin swaps this table for the vehicle's own.

## Transport

- Tester request id `0x7E4`, ECU response id `0x7EC` (physical addressing,
  the usual pair for a battery ECU on several platforms).
- ISO 15765-2 (ISO-TP), normal addressing, 8-byte frames. Every DID reply
  is longer than 7 bytes, so every one is a multi-frame transfer with a
  first frame, a flow-control frame from the tester and consecutive frames.
  Both sides use the Linux `can-isotp` socket; nothing is hand-rolled.
- Service: `0x22` ReadDataByIdentifier only. Anything else gets
  `7F <service> 11` (serviceNotSupported); an unknown DID gets `7F 22 31`
  (requestOutOfRange).

## OBD-II side (`0x7DF` / `0x7E8`) per car type

| Car | PIDs served |
|---|---|
| `ice` | `04 05 0B 0C 0D 10 2F 42 46 A6` |
| `ev` | `0D 42 46 5B A6` (no engine, no fuel) |
| `hybrid` | `04 05 0B 0C 0D 10 2F 42 46 5B A6` |

The supported-PID bitmaps advertise exactly this set, and unserved PIDs get
no answer. `0x5B` carries state of charge as a percentage on the emulator;
the plugins deliberately do not use it (its meaning varies between
vehicles) and take SoC from DID `0x0101` instead.

## DIDs

All multi-byte fields big-endian, as is usual for UDS records.

### `0x0101` pack status, 8 bytes

| Offset | Field | Encoding |
|---|---|---|
| 0-1 | pack voltage | uint16, 0.1 V |
| 2-3 | pack current | int16, 0.1 A, negative = charging |
| 4 | state of charge | uint8, 0.5 % |
| 5 | state of health | uint8, 0.5 % |
| 6 | charging state | 0 none, 1 AC, 2 DC, 3 complete |
| 7 | reserved | 0 |

### `0x0102` range and trip, 8 bytes

| Offset | Field | Encoding |
|---|---|---|
| 0-1 | remaining range | uint16, km |
| 2-3 | consumption | int16, Wh/km, negative = net regeneration |
| 4-7 | odometer | uint32, 0.1 km |

### `0x0103` drive, 8 bytes

| Offset | Field | Encoding |
|---|---|---|
| 0 | gear | 0 P, 1 R, 2 N, 3 D, 4 L |
| 1 | power state | 0 off, 1 acc, 2 on, 3 ready |
| 2-3 | motor speed | uint16, rpm |
| 4-5 | motor power | int16, 0.1 kW, negative = regeneration |
| 6-7 | reserved | 0 |

## Control-port knobs (emulator)

`soc`, `soh`, `packv`, `packi`, `chg`, `range`, `cons`, `gear`, `pwr`,
`mrpm`, `power`, in the units above; `car` reads the current type.

## What the plugins publish

| Contract signal | `emu-ev` | `emu-hybrid` |
|---|---|---|
| speed | PID `0D` | PID `0D` |
| rpm | DID `0103` motor speed | PID `0C` engine speed |
| gear, power state | DID `0103` | DID `0103` |
| motor power, pack V/I | DID `0101`/`0103` | same |
| SoC, SoH, charging | DID `0101` | same |
| range, consumption | DID `0102` | same |
| odometer | PID `A6` | PID `A6` |
| ambient | PID `46` | PID `46` |
| coolant | not capable | PID `05` |
| fuel | not capable | PID `2F` |
| aux battery | PID `42` | PID `42` |
| lamps | `0x420` broadcast, plus EV ready / charging mirrored from the DIDs | same |

Poll periods: DID `0103` and `0101` every 100 ms, `0102` every 500 ms;
PIDs as in `obd2-ice`. A DID is valid while its last answer is younger
than three periods. If the battery ECU never answers at start, the EV
signals are not advertised at all (the plugin degrades to what the OBD
port offers); if it stops answering later they go unknown while the
capability bits hold.
