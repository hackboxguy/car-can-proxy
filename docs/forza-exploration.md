# Forza Horizon 5 telemetry: what else the cluster could show

An exploration, 2026-09-05, no code. What the game actually sends, which
of it the contract already carries, what a small contract extension would
unlock, and what a "real" map could mean for a game and for a car.

## What Forza Horizon 5 sends

One UDP packet per frame (60 Hz), 324 bytes: the "Sled" block that every
Forza title sends, 12 bytes of padding, then the "Dash" block. Everything
is little-endian float unless noted.

| Group | Fields | Notes |
|---|---|---|
| Engine | max rpm, idle rpm, current rpm | max rpm is per car: the redline changes when you change car |
| Motion | acceleration X/Y/Z (m/s²), velocity X/Y/Z (m/s), angular velocity X/Y/Z, yaw, pitch, roll | body frame: X right, Y up, Z forward |
| Wheels, 4 corners each | suspension travel (normalised and metres), tire slip ratio, slip angle, combined slip, wheel rotation speed, on rumble strip (flag), puddle depth, surface rumble | the richest block; nothing like it exists in a road car's OBD |
| Car identity | car ordinal, class (D to X), performance index, drivetrain type (FWD/RWD/AWD), cylinders | ordinal maps to a name with a community table |
| Dash | position X/Y/Z (m, game world), speed (m/s), power (W, signed), torque (Nm), tire temperature × 4 (°F), boost (psi), fuel (0..1), distance travelled (m), best/last/current lap and race time (s), lap number, race position, accel / brake / clutch / handbrake / gear (0..255 or gear index), steer (−127..127), normalised driving line (−127..127), normalised AI brake difference (−127..127) | position is metres in the game world, not latitude/longitude |

What it does not send: other cars, road geometry, posted speed limits,
lights, indicators, doors, coolant, battery. Anything shown for those would
be invented, which the proxy's rules forbid.

## Already on the cluster today

Through the `forza` plugin: speed, rpm, gear, power state (menu vs race),
motor power, fuel, session distance, handbrake and low-fuel lamps, `SIM`
badge. Enough for the analog theme and half of the EV theme.

## Within the current contract, one plugin change each

| Feature | Source field | Where it shows |
|---|---|---|
| Traction lamp flickering on wheelspin | combined slip above a threshold on a driven wheel | telltale row, all themes |
| ABS lamp under hard braking with slip | brake pedal high and slip ratio negative | telltale row |
| Lane departure bits | on-rumble-strip flags (left pair vs right pair) | contract lane state → third theme's road view lane tint |
| Driving-line coach | normalised driving line as lane state left/right, AI brake difference as collision risk low/medium | third theme's badge slot and risk glow, honestly labelled as "line" |
| Eco score | derived from accel/brake pedal smoothness and slip | third theme's eco ring already runs off power; the score could feed a second ring |
| Reverse and neutral | gear 0 is reverse; clutch high with zero torque could be neutral | gear readout |
| Wet-road indicator | puddle depth on any wheel | fog lamp slot is wrong; needs a new lamp bit (reserved bits 20-31 exist) |

None of these need a contract change; they need a decision on which game
facts are honest enough to light a road-car lamp.

## With a minor contract extension (v1.2, additive, reserved IDs and bits)

| New frame | Payload | Unlocks |
|---|---|---|
| `0x402` vehicle limits, 1 s | max rpm, max power kW, max speed, class + performance index | gauges that rescale per car: the tachometer redline and the power gauge full scale follow the car you picked; an "A 800" class badge in the info bar; car name via an ordinal table on the cluster side |
| `0x412` dynamics, 20 ms | longitudinal and lateral acceleration, yaw rate, pitch, roll | a g-force ball in the centre stack; the third theme's road view tilting with pitch and roll; a drift angle readout from slip angle |
| `0x413` tires, 200 ms | 4 × temperature, 4 × combined slip | a four-corner tire widget (colour by temperature, flash on slip), the classic racing-dash element |
| `0x414` boost and pedals, 50 ms | boost, throttle, brake, clutch, steer | boost gauge on the analog theme; pedal bars on the EV centre stack; steering angle for a small wheel glyph |
| `0x470` race, 200 ms | current/last/best lap, race position, lap number | a race panel in the centre slot; best-lap delta; "P3 / L2" |
| `0x460` position, 100 ms | frame type (world metres or WGS-84), X or lat, Y or lon | the moving map below; also breadcrumb trails |
| `0x461` attitude, 100 ms | heading, altitude, speed over ground | map rotation; the same frame a GPS receiver fills for a real car |

Fourteen capability bits are free, so each frame gets one and a vehicle
without it hides the widget, exactly as today. The `forza` plugin would
fill all of these from fields it already receives; the `sim` plugin could
fill dynamics and position from its drive cycle so the widgets can be
developed without the game.

Dynamics at 20 ms means 50 frames/s on the contract bus, still under 1 %
of a 500 kbit/s wire and free on vcan; a g-ball at 20 Hz looks laggy, at
50 Hz it does not.

## The map

Today the third theme's map is a still PNG under the gauges: scenery. Three
different things could replace it, and they are different problems.

**1. A moving map of the game world.** Forza's position is metres in the
game's own coordinate system, and yaw gives heading. Community full-map
renders of the FH5 world exist at several thousand pixels across; two
landmarks driven to (a corner of the airstrip, the festival site) give the
affine transform from game metres to image pixels. The cluster then pans
and rotates the existing backdrop image by position and heading, keeps the
car glyph centred, and draws the driven path as a breadcrumb trail. The
GPU does the pan/rotate for free, so it costs less than the static wash
with the starfield. Honest by construction: the map is the game's own map.
This is the highest-value, lowest-risk map feature for gaming, and it also
gives the cluster its first "route" that agrees with the speed. What it
needs: the position frame above, the map raster and its calibration, and
one QML component that replaces the still image.

**2. A track map that draws itself.** No base image at all: the breadcrumb
trail alone, auto-scaled to the extent driven so far, with the car at the
head. Cheap, works for any game or car, and looks like a rally stage map
after a lap. Good first step towards 1.

**3. A real-world map for a real car.** Only meaningful with a position
source that is geographic: a USB GPS receiver on the Pi (or a phone), not
Forza, whose Mexico is fictional geography. Rendering options on the Pi's
Qt 5.15: Qt Location's map item with the OpenStreetMap plugin and an
offline tile cache (a few hundred MB for a region at the zooms a cluster
needs), or a MapLibre/vector-tile stack, which is heavier. Google Maps is
the wrong tool here: its SDK and terms are built for phones and browsers
with online keys, not an offline embedded cluster; OpenStreetMap-derived
tiles are what automotive hobby projects use. Cost is real: the third
theme already takes most of one core on the Pi 4, and a live tile map adds
decoding and compositing; it would need measuring before committing. The
contract side is identical to option 1 (frame type WGS-84 instead of
metres), so the plugin work is a `gps` plugin reading NMEA from the
receiver, which is a small, well-understood job.

Recommendation, in order: 2, then 1 for gaming, then 3 only once a GPS
receiver and a car are on the bench.

## Beyond Forza: BeamNG.drive

The CarCluster project also supports BeamNG's OutGauge stream, and for a
road-car cluster it is the better game: its packet carries a dash-lights
bitmask with turn signals, high beam, handbrake, traction control, oil,
battery and ABS as the game's own flags. That is the whole telltale row
from real game state rather than inference, plus engine temperature, fuel,
turbo, rpm and speed. A `beamng` plugin would be about the size of the
`forza` one and would light lamps Forza cannot. Worth doing before the
lamp inference ideas above.

## What all of this keeps

The boundary. Every feature here is a plugin filling a contract frame and a
QML widget gated by a capability bit. The game never reaches the cluster,
the cluster never learns which game it is, and the `SIM` badge stays on.
