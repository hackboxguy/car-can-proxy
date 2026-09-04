# Driving the cluster from Forza

Forza Horizon 4 / 5 and Forza Motorsport 7 / 2023 can stream telemetry
("Data Out") as UDP packets to any IP and port. The `forza` plugin makes
that stream a vehicle: `can-proxyd` publishes it as the normal contract and
the cluster shows the car you drive in the game, with a `SIM` badge because
it is not a live vehicle.

## In the game

Settings → HUD and Gameplay:

| Setting | Value |
|---|---|
| Data Out | ON |
| Data Out IP Address | the IP of the machine running `can-proxyd` (the Pi: `192.168.1.94`) |
| Data Out IP Port | `1101` (the plugin's default; any port, matched by `port=`) |
| Data Out Packet Format (Motorsport only) | Dash (Sled works too, with fewer signals) |

The game and the Pi must be on the same network; nothing else is needed on
the Pi, which listens on all interfaces.

## On the Pi (or any host)

```bash
# one-off
can-proxyd --contract-if=vcan0 --plugin=forza --plugin-arg port=1101 --plugin-arg drivetrain=ice

# as the service: edit /home/pi/car-can-proxy/systemd/can-proxyd.env
PLUGIN=forza
PLUGIN_ARGS=--plugin-arg port=1101 --plugin-arg drivetrain=ice
# VEHICLE_IF stays whatever it was; the plugin does not use a CAN interface
sudo systemctl restart can-proxyd
```

`drivetrain=ice|bev|hev` only decides which theme `--theme=auto` picks
(analog, EV, or the third theme); the game does not say what the car is.
The cluster's `--power-kw-max` (default 90) sets the power gauge's full
scale; a 400 kW game car saturates it, so raise it for the EV theme.

## What arrives and what does not

| Contract signal | From |
|---|---|
| speed | Dash `Speed` (m/s × 3.6), or the sled velocity vector |
| rpm | `CurrentEngineRpm` |
| gear | `P` when not in a race (menus, pause), `R` for the game's gear 0, else `D` |
| power state | `ready` in a race, `on` otherwise |
| motor power | Dash `Power` in kW, negative while engine braking |
| fuel level | Dash `Fuel` (0..1) as % |
| odometer | Dash `DistanceTraveled`, the session's distance |
| lamps | parking brake from the handbrake; low fuel below 10 % |

Not published (the game does not send them): coolant, module voltage,
ambient, state of charge, range, consumption, driver assist. The cluster
hides those gauges.

The plugin accepts the four known packet sizes (232 sled, 311 FM7 dash, 324
FH4/FH5, 331 FM 2023); an unknown size is logged once and ignored. One
second without packets is reported as no vehicle.

## Reference

Layouts follow the CarCluster project (`r00li/CarCluster`,
`src/Games/ForzaHorizonGame.cpp`), which drives real instrument clusters
from the same stream through an ESP32; this plugin does the same job for the
contract instead of a car's own CAN matrix. `tests/integration/forza_test.sh`
synthesises the stream with python for CI.
