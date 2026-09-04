# Recorded vehicle sessions

Candump-format logs of the vehicle side, recorded with `can-proxyd --record`
and replayed with `tools/can-replay --respond <if> <log>` (answers each
OBD-II request with the last recorded reply, re-broadcasts non-OBD frames).
The interface name in the log is cosmetic and has been rewritten to `vcan1`.

| File | Source | Notes |
|---|---|---|
| `obd2-emulator-canable-pi4.log` | A hardware OBD-II emulator on a CANable (gs_usb) dongle, `can0` at 500 kbit/s on a Pi 4, 5 s of `obd2-ice` polling, 2026-09-04 | The device advertises PIDs `0x01`-`0x50` wholesale (bitmaps `FF FF FF FF`) and answers `0x42` and `0x46` with zero payloads; speed 92 km/h, rpm about 2515 with jitter, coolant 67 °C, fuel 44.5 %. The first real gs_usb session; the reason `obd2-ice` gained its plausibility gates. |
