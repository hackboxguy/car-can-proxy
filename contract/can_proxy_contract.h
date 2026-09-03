/*
 * can_proxy_contract.h — the CAN proxy contract, version 1.1
 *
 * Single source of truth for the frame set published by can-proxyd and
 * consumed by an instrument-cluster application. Every ID, cycle time,
 * staleness window, SNA (signal not available) encoding, scale factor, enum
 * and bit assignment lives here, together with pack/unpack helpers.
 *
 * Plain C99, header-only, no allocation, no libc beyond <stdint.h> and
 * <stdbool.h>. Compiles as C++ too, so a Qt consumer can vendor this file
 * unchanged. See docs/contract-versioning.md for the compatibility rules.
 *
 * Wire conventions (unchanged from v1.0):
 *   - classic CAN, 11-bit IDs, always 8 data bytes
 *   - little-endian multi-byte fields
 *   - physical units on the wire, never display scaling
 *   - every signal has an SNA encoding; reserved bytes transmit as 0x00
 *   - broadcast only; the cluster never sends
 */
#ifndef CAN_PROXY_CONTRACT_H
#define CAN_PROXY_CONTRACT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Version                                                                   */
/* ------------------------------------------------------------------------ */

#define CANPROXY_CONTRACT_MAJOR 1u
#define CANPROXY_CONTRACT_MINOR 1u

#define CANPROXY_FRAME_DLC 8u

/* ------------------------------------------------------------------------ */
/* Frame IDs, cycle times, staleness windows                                 */
/* ------------------------------------------------------------------------ */

#define CANPROXY_ID_STATUS    0x400u  /* heartbeat, proxy state, capabilities */
#define CANPROXY_ID_IDENTITY  0x401u  /* drivetrain, source kind, plugin, id  */
#define CANPROXY_ID_MOTION    0x410u  /* speed, rpm, gear, power state        */
#define CANPROXY_ID_EDRIVE    0x411u  /* motor power, pack voltage/current    */
#define CANPROXY_ID_TELLTALES 0x420u  /* 32 warning-lamp bits                 */
#define CANPROXY_ID_ENERGY    0x430u  /* SoC, SoH, range, consumption, charge */
#define CANPROXY_ID_TRIP      0x431u  /* odometer, ambient, cabin temperature */
#define CANPROXY_ID_THERMAL   0x440u  /* coolant, fuel, auxiliary battery     */
#define CANPROXY_ID_ASSIST    0x450u  /* eco score, speed limit, ADAS (opt.)  */

#define CANPROXY_ID_FIRST     CANPROXY_ID_STATUS
#define CANPROXY_ID_LAST      CANPROXY_ID_ASSIST
#define CANPROXY_FRAME_COUNT  9u

#define CANPROXY_CYCLE_STATUS_MS      100u
#define CANPROXY_CYCLE_IDENTITY_MS   1000u
#define CANPROXY_CYCLE_MOTION_MS       50u
#define CANPROXY_CYCLE_EDRIVE_MS       50u
#define CANPROXY_CYCLE_TELLTALES_MS   100u
#define CANPROXY_CYCLE_ENERGY_MS      500u
#define CANPROXY_CYCLE_TRIP_MS       1000u
#define CANPROXY_CYCLE_THERMAL_MS     500u
#define CANPROXY_CYCLE_ASSIST_MS      200u

#define CANPROXY_STALE_STATUS_MS      500u
#define CANPROXY_STALE_IDENTITY_MS   4000u
#define CANPROXY_STALE_MOTION_MS      250u
#define CANPROXY_STALE_EDRIVE_MS      250u
#define CANPROXY_STALE_TELLTALES_MS   500u
#define CANPROXY_STALE_ENERGY_MS     2000u
#define CANPROXY_STALE_TRIP_MS       4000u
#define CANPROXY_STALE_THERMAL_MS    2000u
#define CANPROXY_STALE_ASSIST_MS      800u

/* ------------------------------------------------------------------------ */
/* SNA encodings                                                             */
/* ------------------------------------------------------------------------ */

#define CANPROXY_SNA_U8   0xFFu
#define CANPROXY_SNA_I8   ((int8_t)-128)        /* 0x80 */
#define CANPROXY_SNA_U16  0xFFFFu
#define CANPROXY_SNA_I16  ((int16_t)-32768)     /* 0x8000 */
#define CANPROXY_SNA_U32  0xFFFFFFFFu

/* ------------------------------------------------------------------------ */
/* Enumerations                                                              */
/* ------------------------------------------------------------------------ */

typedef enum {
    CANPROXY_STATE_STARTING   = 0,  /* daemon up, plugin not yet reporting   */
    CANPROXY_STATE_NO_VEHICLE = 1,  /* bus silent / no ECU answering         */
    CANPROXY_STATE_DEGRADED   = 2,  /* vehicle present, some signals stale   */
    CANPROXY_STATE_OK         = 3   /* vehicle present, all signals fresh    */
} canproxy_proxy_state_t;

typedef enum {
    CANPROXY_GEAR_P = 0,
    CANPROXY_GEAR_R = 1,
    CANPROXY_GEAR_N = 2,
    CANPROXY_GEAR_D = 3,
    CANPROXY_GEAR_L = 4
} canproxy_gear_t;

typedef enum {
    CANPROXY_POWER_OFF   = 0,
    CANPROXY_POWER_ACC   = 1,
    CANPROXY_POWER_ON    = 2,
    CANPROXY_POWER_READY = 3   /* ready to drive / engine running */
} canproxy_power_state_t;

typedef enum {
    CANPROXY_CHARGE_NONE     = 0,
    CANPROXY_CHARGE_AC       = 1,
    CANPROXY_CHARGE_DC       = 2,
    CANPROXY_CHARGE_COMPLETE = 3
} canproxy_charging_state_t;

typedef enum {
    CANPROXY_DRIVETRAIN_UNKNOWN = 0,
    CANPROXY_DRIVETRAIN_ICE     = 1,  /* combustion */
    CANPROXY_DRIVETRAIN_BEV     = 2,  /* battery electric */
    CANPROXY_DRIVETRAIN_HEV     = 3,  /* hybrid */
    CANPROXY_DRIVETRAIN_PHEV    = 4,  /* plug-in hybrid */
    CANPROXY_DRIVETRAIN_FCEV    = 5   /* fuel cell */
} canproxy_drivetrain_t;

typedef enum {
    CANPROXY_SOURCE_SIMULATED = 0,  /* no vehicle bus involved              */
    CANPROXY_SOURCE_EMULATOR  = 1,  /* bench emulator on a real/virtual bus */
    CANPROXY_SOURCE_LIVE      = 2,  /* a real vehicle                       */
    CANPROXY_SOURCE_REPLAY    = 3   /* recorded vehicle traffic replayed    */
} canproxy_source_kind_t;

typedef enum {
    CANPROXY_RISK_NONE   = 0,
    CANPROXY_RISK_LOW    = 1,
    CANPROXY_RISK_MEDIUM = 2,
    CANPROXY_RISK_HIGH   = 3
} canproxy_collision_risk_t;

/* ------------------------------------------------------------------------ */
/* Capability bitmap (0x400 bytes 4-7, uint32 LE)                            */
/* "This vehicle can ever provide the signal", not "the value is present".   */
/* ------------------------------------------------------------------------ */

#define CANPROXY_CAP_SPEED         (1u << 0)
#define CANPROXY_CAP_RPM           (1u << 1)
#define CANPROXY_CAP_GEAR          (1u << 2)
#define CANPROXY_CAP_MOTOR_POWER   (1u << 3)
#define CANPROXY_CAP_PACK_VI       (1u << 4)   /* pack voltage and current */
#define CANPROXY_CAP_SOC           (1u << 5)
#define CANPROXY_CAP_RANGE         (1u << 6)
#define CANPROXY_CAP_CONSUMPTION   (1u << 7)
#define CANPROXY_CAP_ODOMETER      (1u << 8)
#define CANPROXY_CAP_AMBIENT_TEMP  (1u << 9)
#define CANPROXY_CAP_COOLANT_TEMP  (1u << 10)
#define CANPROXY_CAP_FUEL_LEVEL    (1u << 11)
#define CANPROXY_CAP_AUX_BATTERY   (1u << 12)
#define CANPROXY_CAP_SOH           (1u << 13)  /* v1.1 */
#define CANPROXY_CAP_CHARGING      (1u << 14)  /* v1.1 */
#define CANPROXY_CAP_CABIN_TEMP    (1u << 15)  /* v1.1 */
#define CANPROXY_CAP_POWER_STATE   (1u << 16)  /* v1.1 */
#define CANPROXY_CAP_ASSIST        (1u << 17)  /* v1.1: frame 0x450 is published */
#define CANPROXY_CAP_RESERVED_MASK 0xFFFC0000u /* bits 18-31 must be zero */

/* ------------------------------------------------------------------------ */
/* Telltale bitmap (0x420 bytes 0-3, uint32 LE)                              */
/* Bits 0-11 are frozen: they predate this contract and an older 16-bit      */
/* reader depends on them. No SNA: unlit and unsupported are both zero.      */
/* ------------------------------------------------------------------------ */

#define CANPROXY_TT_ENGINE        (1u << 0)
#define CANPROXY_TT_OIL           (1u << 1)
#define CANPROXY_TT_BATTERY       (1u << 2)
#define CANPROXY_TT_BRAKE         (1u << 3)
#define CANPROXY_TT_LEFT          (1u << 4)
#define CANPROXY_TT_RIGHT         (1u << 5)
#define CANPROXY_TT_HIGHBEAM      (1u << 6)
#define CANPROXY_TT_DOOR          (1u << 7)
#define CANPROXY_TT_SEATBELT      (1u << 8)
#define CANPROXY_TT_ABS           (1u << 9)
#define CANPROXY_TT_TRACTION      (1u << 10)
#define CANPROXY_TT_TPMS          (1u << 11)
#define CANPROXY_TT_EV_READY      (1u << 12)  /* v1.1 */
#define CANPROXY_TT_CHARGING      (1u << 13)  /* v1.1 */
#define CANPROXY_TT_LIMITED_POWER (1u << 14)  /* v1.1: "turtle" */
#define CANPROXY_TT_LOW_TRACTION_BATTERY (1u << 15) /* v1.1 */
#define CANPROXY_TT_PARK_BRAKE    (1u << 16)  /* v1.1 */
#define CANPROXY_TT_LOW_FUEL      (1u << 17)  /* v1.1 */
#define CANPROXY_TT_FOG           (1u << 18)  /* v1.1 */
#define CANPROXY_TT_HV_SYSTEM_FAULT (1u << 19) /* v1.1: hybrid/EV system */
#define CANPROXY_TT_RESERVED_MASK 0xFFF00000u /* bits 20-31 must be zero */
#define CANPROXY_TT_LEGACY_MASK   0x0000FFFFu /* what a 16-bit reader sees */

/* Lane state bitfield (0x450 byte 3) */
#define CANPROXY_LANE_LEFT_SEEN       (1u << 0)
#define CANPROXY_LANE_RIGHT_SEEN      (1u << 1)
#define CANPROXY_LANE_LEFT_DEPARTURE  (1u << 2)
#define CANPROXY_LANE_RIGHT_DEPARTURE (1u << 3)
#define CANPROXY_LANE_VALID_MASK      0x0Fu

/* ------------------------------------------------------------------------ */
/* Decoded frame structures                                                  */
/* Physical units. Each optional signal carries a `*_valid` flag: false on   */
/* pack means "encode SNA", false on unpack means "wire carried SNA".        */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint8_t  contract_major;
    uint8_t  contract_minor;
    uint8_t  counter;        /* rolling 0-255 */
    uint8_t  proxy_state;    /* canproxy_proxy_state_t */
    uint32_t capabilities;   /* CANPROXY_CAP_* */
} canproxy_status_t;

typedef struct {
    uint8_t  drivetrain;     /* canproxy_drivetrain_t */
    uint8_t  source_kind;    /* canproxy_source_kind_t */
    uint8_t  plugin_major;
    uint8_t  plugin_minor;
    uint32_t vehicle_id;     /* FNV-1a of plugin name + VIN; 0 = unknown */
} canproxy_identity_t;

typedef struct {
    double   speed_kmh;        bool speed_valid;        /* 0.01 km/h, 0..655.34 */
    uint16_t rpm;              bool rpm_valid;          /* 1 rpm, 0..65534 */
    uint8_t  gear;             bool gear_valid;         /* canproxy_gear_t */
    uint8_t  power_state;      bool power_state_valid;  /* canproxy_power_state_t */
} canproxy_motion_t;

typedef struct {
    double motor_power_kw;     bool motor_power_valid;  /* 0.1 kW, negative = regen */
    double pack_voltage_v;     bool pack_voltage_valid; /* 0.1 V */
    double pack_current_a;     bool pack_current_valid; /* 0.1 A, negative = charging */
} canproxy_edrive_t;

typedef struct {
    uint32_t bits;             /* CANPROXY_TT_* */
} canproxy_telltales_t;

typedef struct {
    double   soc_pct;          bool soc_valid;          /* 0.5 %, 0..100 */
    double   soh_pct;          bool soh_valid;          /* 0.5 %, 0..100 */
    uint16_t range_km;         bool range_valid;        /* 1 km */
    int16_t  consumption_wh_km; bool consumption_valid; /* 1 Wh/km */
    uint8_t  charging_state;   bool charging_state_valid; /* canproxy_charging_state_t */
} canproxy_energy_t;

typedef struct {
    double   odometer_km;      bool odometer_valid;     /* 0.1 km */
    int8_t   ambient_c;        bool ambient_valid;      /* 1 °C, -127..127 */
    int8_t   cabin_c;          bool cabin_valid;        /* 1 °C */
} canproxy_trip_t;

typedef struct {
    int8_t   coolant_c;        bool coolant_valid;      /* 1 °C */
    double   fuel_pct;         bool fuel_valid;         /* 0.5 %, 0..100 */
    double   aux_battery_v;    bool aux_battery_valid;  /* 0.01 V */
} canproxy_thermal_t;

typedef struct {
    uint8_t  eco_score;        bool eco_score_valid;    /* 0..100 */
    uint8_t  speed_limit_kmh;  bool speed_limit_valid;  /* 0 = none known */
    uint8_t  collision_risk;   bool collision_risk_valid; /* canproxy_collision_risk_t */
    uint8_t  lane_state;       bool lane_state_valid;   /* CANPROXY_LANE_* */
    double   lead_gap_m;       bool lead_gap_valid;     /* 0.1 m */
} canproxy_assist_t;

/* ------------------------------------------------------------------------ */
/* Byte helpers                                                              */
/* ------------------------------------------------------------------------ */

static inline void canproxy_put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static inline uint16_t canproxy_get_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void canproxy_put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t canproxy_get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void canproxy_clear(uint8_t d[CANPROXY_FRAME_DLC])
{
    unsigned i;
    for (i = 0; i < CANPROXY_FRAME_DLC; i++)
        d[i] = 0;
}

/* Round-to-nearest, half away from zero, with no <math.h> dependency. */
static inline long canproxy_round(double v)
{
    return (v >= 0.0) ? (long)(v + 0.5) : -(long)(-v + 0.5);
}

/* Scaled encoders: clamp to the encodable range that excludes SNA. */
static inline uint8_t canproxy_enc_u8(double v, double lsb, bool valid, uint8_t max)
{
    long r;
    if (!valid) return CANPROXY_SNA_U8;
    r = canproxy_round(v / lsb);
    if (r < 0) r = 0;
    if (r > (long)max) r = (long)max;
    return (uint8_t)r;
}

static inline int8_t canproxy_enc_i8(double v, double lsb, bool valid)
{
    long r;
    if (!valid) return CANPROXY_SNA_I8;
    r = canproxy_round(v / lsb);
    if (r < -127) r = -127;
    if (r > 127) r = 127;
    return (int8_t)r;
}

static inline uint16_t canproxy_enc_u16(double v, double lsb, bool valid)
{
    long r;
    if (!valid) return CANPROXY_SNA_U16;
    r = canproxy_round(v / lsb);
    if (r < 0) r = 0;
    if (r > 0xFFFE) r = 0xFFFE;
    return (uint16_t)r;
}

static inline int16_t canproxy_enc_i16(double v, double lsb, bool valid)
{
    long r;
    if (!valid) return CANPROXY_SNA_I16;
    r = canproxy_round(v / lsb);
    if (r < -32767) r = -32767;
    if (r > 32767) r = 32767;
    return (int16_t)r;
}

static inline uint32_t canproxy_enc_u32(double v, double lsb, bool valid)
{
    double r;
    if (!valid) return CANPROXY_SNA_U32;
    r = (double)canproxy_round(v / lsb);
    if (r < 0.0) r = 0.0;
    if (r > 4294967294.0) r = 4294967294.0;
    return (uint32_t)r;
}

/* ------------------------------------------------------------------------ */
/* 0x400 status                                                              */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_status(const canproxy_status_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    d[0] = s->contract_major;
    d[1] = s->contract_minor;
    d[2] = s->counter;
    d[3] = s->proxy_state;
    canproxy_put_u32le(&d[4], s->capabilities & ~CANPROXY_CAP_RESERVED_MASK);
}

static inline void canproxy_unpack_status(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_status_t *s)
{
    s->contract_major = d[0];
    s->contract_minor = d[1];
    s->counter        = d[2];
    s->proxy_state    = d[3];
    s->capabilities   = canproxy_get_u32le(&d[4]);
}

/* True when a received status frame is from a contract this header can read:
 * same major, any minor (minor bumps are additive). */
static inline bool canproxy_status_compatible(const canproxy_status_t *s)
{
    return s->contract_major == CANPROXY_CONTRACT_MAJOR;
}

/* ------------------------------------------------------------------------ */
/* 0x401 identity                                                            */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_identity(const canproxy_identity_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    d[0] = s->drivetrain;
    d[1] = s->source_kind;
    d[2] = s->plugin_major;
    d[3] = s->plugin_minor;
    canproxy_put_u32le(&d[4], s->vehicle_id);
}

static inline void canproxy_unpack_identity(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_identity_t *s)
{
    s->drivetrain   = d[0];
    s->source_kind  = d[1];
    s->plugin_major = d[2];
    s->plugin_minor = d[3];
    s->vehicle_id   = canproxy_get_u32le(&d[4]);
}

/* FNV-1a 32-bit over a NUL-terminated string; the vehicle_id convention is
 * hash(plugin_name "/" VIN) with VIN omitted when unknown. Never returns 0
 * for a non-empty string in practice; callers use 0 to mean "unknown". */
static inline uint32_t canproxy_fnv1a32(const char *s)
{
    uint32_t h = 0x811C9DC5u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193u;
    }
    return h;
}

/* ------------------------------------------------------------------------ */
/* 0x410 motion                                                              */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_motion(const canproxy_motion_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    canproxy_put_u16le(&d[0], canproxy_enc_u16(s->speed_kmh, 0.01, s->speed_valid));
    canproxy_put_u16le(&d[2], s->rpm_valid ? (s->rpm == 0xFFFFu ? 0xFFFEu : s->rpm) : CANPROXY_SNA_U16);
    d[4] = s->gear_valid ? s->gear : CANPROXY_SNA_U8;
    d[5] = s->power_state_valid ? s->power_state : CANPROXY_SNA_U8;
}

static inline void canproxy_unpack_motion(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_motion_t *s)
{
    uint16_t raw;
    raw = canproxy_get_u16le(&d[0]);
    s->speed_valid = (raw != CANPROXY_SNA_U16);
    s->speed_kmh   = s->speed_valid ? raw * 0.01 : 0.0;
    raw = canproxy_get_u16le(&d[2]);
    s->rpm_valid = (raw != CANPROXY_SNA_U16);
    s->rpm       = s->rpm_valid ? raw : 0;
    s->gear_valid = (d[4] != CANPROXY_SNA_U8);
    s->gear       = s->gear_valid ? d[4] : 0;
    s->power_state_valid = (d[5] != CANPROXY_SNA_U8);
    s->power_state       = s->power_state_valid ? d[5] : 0;
}

/* ------------------------------------------------------------------------ */
/* 0x411 electric drive                                                      */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_edrive(const canproxy_edrive_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    canproxy_put_u16le(&d[0], (uint16_t)canproxy_enc_i16(s->motor_power_kw, 0.1, s->motor_power_valid));
    canproxy_put_u16le(&d[2], canproxy_enc_u16(s->pack_voltage_v, 0.1, s->pack_voltage_valid));
    canproxy_put_u16le(&d[4], (uint16_t)canproxy_enc_i16(s->pack_current_a, 0.1, s->pack_current_valid));
}

static inline void canproxy_unpack_edrive(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_edrive_t *s)
{
    int16_t  sr;
    uint16_t ur;
    sr = (int16_t)canproxy_get_u16le(&d[0]);
    s->motor_power_valid = (sr != CANPROXY_SNA_I16);
    s->motor_power_kw    = s->motor_power_valid ? sr * 0.1 : 0.0;
    ur = canproxy_get_u16le(&d[2]);
    s->pack_voltage_valid = (ur != CANPROXY_SNA_U16);
    s->pack_voltage_v     = s->pack_voltage_valid ? ur * 0.1 : 0.0;
    sr = (int16_t)canproxy_get_u16le(&d[4]);
    s->pack_current_valid = (sr != CANPROXY_SNA_I16);
    s->pack_current_a     = s->pack_current_valid ? sr * 0.1 : 0.0;
}

/* ------------------------------------------------------------------------ */
/* 0x420 telltales                                                           */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_telltales(const canproxy_telltales_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    canproxy_put_u32le(&d[0], s->bits & ~CANPROXY_TT_RESERVED_MASK);
}

static inline void canproxy_unpack_telltales(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_telltales_t *s)
{
    s->bits = canproxy_get_u32le(&d[0]);
}

/* ------------------------------------------------------------------------ */
/* 0x430 energy                                                              */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_energy(const canproxy_energy_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    d[0] = canproxy_enc_u8(s->soc_pct, 0.5, s->soc_valid, 200);
    d[1] = canproxy_enc_u8(s->soh_pct, 0.5, s->soh_valid, 200);
    canproxy_put_u16le(&d[2], s->range_valid ? (s->range_km == 0xFFFFu ? 0xFFFEu : s->range_km) : CANPROXY_SNA_U16);
    canproxy_put_u16le(&d[4], (uint16_t)(s->consumption_valid
                                 ? (s->consumption_wh_km == CANPROXY_SNA_I16 ? (int16_t)-32767 : s->consumption_wh_km)
                                 : CANPROXY_SNA_I16));
    d[6] = s->charging_state_valid ? s->charging_state : CANPROXY_SNA_U8;
}

static inline void canproxy_unpack_energy(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_energy_t *s)
{
    uint16_t ur;
    int16_t  sr;
    s->soc_valid = (d[0] != CANPROXY_SNA_U8);
    s->soc_pct   = s->soc_valid ? d[0] * 0.5 : 0.0;
    s->soh_valid = (d[1] != CANPROXY_SNA_U8);
    s->soh_pct   = s->soh_valid ? d[1] * 0.5 : 0.0;
    ur = canproxy_get_u16le(&d[2]);
    s->range_valid = (ur != CANPROXY_SNA_U16);
    s->range_km    = s->range_valid ? ur : 0;
    sr = (int16_t)canproxy_get_u16le(&d[4]);
    s->consumption_valid = (sr != CANPROXY_SNA_I16);
    s->consumption_wh_km = s->consumption_valid ? sr : 0;
    s->charging_state_valid = (d[6] != CANPROXY_SNA_U8);
    s->charging_state       = s->charging_state_valid ? d[6] : 0;
}

/* ------------------------------------------------------------------------ */
/* 0x431 trip and environment                                                */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_trip(const canproxy_trip_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    canproxy_put_u32le(&d[0], canproxy_enc_u32(s->odometer_km, 0.1, s->odometer_valid));
    d[4] = (uint8_t)canproxy_enc_i8((double)s->ambient_c, 1.0, s->ambient_valid);
    d[5] = (uint8_t)canproxy_enc_i8((double)s->cabin_c, 1.0, s->cabin_valid);
}

static inline void canproxy_unpack_trip(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_trip_t *s)
{
    uint32_t ur = canproxy_get_u32le(&d[0]);
    s->odometer_valid = (ur != CANPROXY_SNA_U32);
    s->odometer_km    = s->odometer_valid ? ur * 0.1 : 0.0;
    s->ambient_valid = ((int8_t)d[4] != CANPROXY_SNA_I8);
    s->ambient_c     = s->ambient_valid ? (int8_t)d[4] : 0;
    s->cabin_valid = ((int8_t)d[5] != CANPROXY_SNA_I8);
    s->cabin_c     = s->cabin_valid ? (int8_t)d[5] : 0;
}

/* ------------------------------------------------------------------------ */
/* 0x440 thermal and auxiliary                                               */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_thermal(const canproxy_thermal_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    d[0] = (uint8_t)canproxy_enc_i8((double)s->coolant_c, 1.0, s->coolant_valid);
    d[1] = canproxy_enc_u8(s->fuel_pct, 0.5, s->fuel_valid, 200);
    canproxy_put_u16le(&d[2], canproxy_enc_u16(s->aux_battery_v, 0.01, s->aux_battery_valid));
}

static inline void canproxy_unpack_thermal(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_thermal_t *s)
{
    uint16_t ur;
    s->coolant_valid = ((int8_t)d[0] != CANPROXY_SNA_I8);
    s->coolant_c     = s->coolant_valid ? (int8_t)d[0] : 0;
    s->fuel_valid = (d[1] != CANPROXY_SNA_U8);
    s->fuel_pct   = s->fuel_valid ? d[1] * 0.5 : 0.0;
    ur = canproxy_get_u16le(&d[2]);
    s->aux_battery_valid = (ur != CANPROXY_SNA_U16);
    s->aux_battery_v     = s->aux_battery_valid ? ur * 0.01 : 0.0;
}

/* ------------------------------------------------------------------------ */
/* 0x450 driver assist and eco (optional, gated by CANPROXY_CAP_ASSIST)      */
/* ------------------------------------------------------------------------ */

static inline void canproxy_pack_assist(const canproxy_assist_t *s, uint8_t d[CANPROXY_FRAME_DLC])
{
    canproxy_clear(d);
    d[0] = canproxy_enc_u8((double)s->eco_score, 1.0, s->eco_score_valid, 100);
    d[1] = s->speed_limit_valid ? (s->speed_limit_kmh == CANPROXY_SNA_U8 ? 0xFEu : s->speed_limit_kmh) : CANPROXY_SNA_U8;
    d[2] = s->collision_risk_valid ? s->collision_risk : CANPROXY_SNA_U8;
    d[3] = s->lane_state_valid ? (uint8_t)(s->lane_state & CANPROXY_LANE_VALID_MASK) : CANPROXY_SNA_U8;
    canproxy_put_u16le(&d[4], canproxy_enc_u16(s->lead_gap_m, 0.1, s->lead_gap_valid));
}

static inline void canproxy_unpack_assist(const uint8_t d[CANPROXY_FRAME_DLC], canproxy_assist_t *s)
{
    uint16_t ur;
    s->eco_score_valid = (d[0] != CANPROXY_SNA_U8);
    s->eco_score       = s->eco_score_valid ? d[0] : 0;
    s->speed_limit_valid = (d[1] != CANPROXY_SNA_U8);
    s->speed_limit_kmh   = s->speed_limit_valid ? d[1] : 0;
    s->collision_risk_valid = (d[2] != CANPROXY_SNA_U8);
    s->collision_risk       = s->collision_risk_valid ? d[2] : 0;
    s->lane_state_valid = (d[3] != CANPROXY_SNA_U8);
    s->lane_state       = s->lane_state_valid ? d[3] : 0;
    ur = canproxy_get_u16le(&d[4]);
    s->lead_gap_valid = (ur != CANPROXY_SNA_U16);
    s->lead_gap_m     = s->lead_gap_valid ? ur * 0.1 : 0.0;
}

/* ------------------------------------------------------------------------ */
/* Lookups by ID                                                             */
/* ------------------------------------------------------------------------ */

/* Cycle time in ms for a contract ID, 0 if the ID is not in the contract. */
static inline unsigned canproxy_cycle_ms(uint32_t id)
{
    switch (id) {
    case CANPROXY_ID_STATUS:    return CANPROXY_CYCLE_STATUS_MS;
    case CANPROXY_ID_IDENTITY:  return CANPROXY_CYCLE_IDENTITY_MS;
    case CANPROXY_ID_MOTION:    return CANPROXY_CYCLE_MOTION_MS;
    case CANPROXY_ID_EDRIVE:    return CANPROXY_CYCLE_EDRIVE_MS;
    case CANPROXY_ID_TELLTALES: return CANPROXY_CYCLE_TELLTALES_MS;
    case CANPROXY_ID_ENERGY:    return CANPROXY_CYCLE_ENERGY_MS;
    case CANPROXY_ID_TRIP:      return CANPROXY_CYCLE_TRIP_MS;
    case CANPROXY_ID_THERMAL:   return CANPROXY_CYCLE_THERMAL_MS;
    case CANPROXY_ID_ASSIST:    return CANPROXY_CYCLE_ASSIST_MS;
    default:                    return 0;
    }
}

/* Staleness window in ms: after this long without the frame, a consumer
 * must treat every signal in it as unknown. 0 if not a contract ID. */
static inline unsigned canproxy_stale_ms(uint32_t id)
{
    switch (id) {
    case CANPROXY_ID_STATUS:    return CANPROXY_STALE_STATUS_MS;
    case CANPROXY_ID_IDENTITY:  return CANPROXY_STALE_IDENTITY_MS;
    case CANPROXY_ID_MOTION:    return CANPROXY_STALE_MOTION_MS;
    case CANPROXY_ID_EDRIVE:    return CANPROXY_STALE_EDRIVE_MS;
    case CANPROXY_ID_TELLTALES: return CANPROXY_STALE_TELLTALES_MS;
    case CANPROXY_ID_ENERGY:    return CANPROXY_STALE_ENERGY_MS;
    case CANPROXY_ID_TRIP:      return CANPROXY_STALE_TRIP_MS;
    case CANPROXY_ID_THERMAL:   return CANPROXY_STALE_THERMAL_MS;
    case CANPROXY_ID_ASSIST:    return CANPROXY_STALE_ASSIST_MS;
    default:                    return 0;
    }
}

static inline bool canproxy_is_contract_id(uint32_t id)
{
    return canproxy_cycle_ms(id) != 0;
}

static inline const char *canproxy_id_name(uint32_t id)
{
    switch (id) {
    case CANPROXY_ID_STATUS:    return "status";
    case CANPROXY_ID_IDENTITY:  return "identity";
    case CANPROXY_ID_MOTION:    return "motion";
    case CANPROXY_ID_EDRIVE:    return "edrive";
    case CANPROXY_ID_TELLTALES: return "telltales";
    case CANPROXY_ID_ENERGY:    return "energy";
    case CANPROXY_ID_TRIP:      return "trip";
    case CANPROXY_ID_THERMAL:   return "thermal";
    case CANPROXY_ID_ASSIST:    return "assist";
    default:                    return "unknown";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* CAN_PROXY_CONTRACT_H */
