/*
 * canproxy/plugin.h — the vehicle plugin ABI, version 1
 *
 * A plugin is a shared object that knows one kind of vehicle. It reads the
 * vehicle (a CAN interface, a file, nothing at all) and pushes a normalised
 * `canproxy_vehicle_state` to the host whenever it has something new. The
 * host owns the contract bus, the publish schedule and the heartbeat.
 *
 * This header deliberately contains no CAN frame, no contract ID and no wire
 * encoding. A plugin cannot reach the contract bus, which is what keeps the
 * vehicle/cluster boundary real. Physical units only.
 *
 * Plain C so plugins can be written in C or C++.
 */
#ifndef CANPROXY_PLUGIN_H
#define CANPROXY_PLUGIN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANPROXY_PLUGIN_ABI_VERSION 1u

/* Entry point every plugin exports. */
#define CANPROXY_PLUGIN_ENTRY_SYMBOL "canproxy_plugin_entry"

/* ------------------------------------------------------------------------ */
/* Signals                                                                   */
/* Bit positions in `capable` ("this vehicle can ever provide it") and in    */
/* `valid` ("the field below holds a current measurement").                  */
/* ------------------------------------------------------------------------ */

enum canproxy_signal {
    CANPROXY_SIG_SPEED = 0,
    CANPROXY_SIG_RPM,             /* engine or motor speed */
    CANPROXY_SIG_GEAR,
    CANPROXY_SIG_POWER_STATE,
    CANPROXY_SIG_MOTOR_POWER,
    CANPROXY_SIG_PACK_VOLTAGE,
    CANPROXY_SIG_PACK_CURRENT,
    CANPROXY_SIG_SOC,
    CANPROXY_SIG_SOH,
    CANPROXY_SIG_RANGE,
    CANPROXY_SIG_CONSUMPTION,
    CANPROXY_SIG_CHARGING_STATE,
    CANPROXY_SIG_ODOMETER,
    CANPROXY_SIG_AMBIENT_TEMP,
    CANPROXY_SIG_CABIN_TEMP,
    CANPROXY_SIG_COOLANT_TEMP,
    CANPROXY_SIG_FUEL_LEVEL,
    CANPROXY_SIG_AUX_BATTERY,
    CANPROXY_SIG_ECO_SCORE,
    CANPROXY_SIG_SPEED_LIMIT,
    CANPROXY_SIG_COLLISION_RISK,
    CANPROXY_SIG_LANE_STATE,
    CANPROXY_SIG_LEAD_GAP,
    CANPROXY_SIG_COUNT
};

#define CANPROXY_SIG_BIT(sig) (1u << (sig))

/* ------------------------------------------------------------------------ */
/* Warning lamps                                                             */
/* Bit positions in `lamps`. Unlit and unsupported are both zero.            */
/* ------------------------------------------------------------------------ */

enum canproxy_lamp {
    CANPROXY_LAMP_ENGINE = 0,
    CANPROXY_LAMP_OIL,
    CANPROXY_LAMP_BATTERY,            /* 12 V system */
    CANPROXY_LAMP_BRAKE,
    CANPROXY_LAMP_TURN_LEFT,
    CANPROXY_LAMP_TURN_RIGHT,
    CANPROXY_LAMP_HIGH_BEAM,
    CANPROXY_LAMP_DOOR,
    CANPROXY_LAMP_SEATBELT,
    CANPROXY_LAMP_ABS,
    CANPROXY_LAMP_TRACTION,
    CANPROXY_LAMP_TPMS,
    CANPROXY_LAMP_EV_READY,
    CANPROXY_LAMP_CHARGING,
    CANPROXY_LAMP_LIMITED_POWER,
    CANPROXY_LAMP_LOW_TRACTION_BATTERY,
    CANPROXY_LAMP_PARK_BRAKE,
    CANPROXY_LAMP_LOW_FUEL,
    CANPROXY_LAMP_FOG,
    CANPROXY_LAMP_HV_SYSTEM_FAULT,
    CANPROXY_LAMP_COUNT
};

#define CANPROXY_LAMP_BIT(lamp) (1u << (lamp))

/* ------------------------------------------------------------------------ */
/* Enumerated values                                                         */
/* ------------------------------------------------------------------------ */

/* Enumerated values use the short CP_ prefix so that a host translation unit
 * can include this header next to the wire contract without name clashes. */
enum canproxy_gear        { CP_GEAR_P = 0, CP_GEAR_R, CP_GEAR_N, CP_GEAR_D, CP_GEAR_L };
enum canproxy_power_state { CP_POWER_OFF = 0, CP_POWER_ACC, CP_POWER_ON, CP_POWER_READY };
enum canproxy_charging    { CP_CHARGING_NONE = 0, CP_CHARGING_AC, CP_CHARGING_DC, CP_CHARGING_COMPLETE };
enum canproxy_drivetrain  { CP_DRIVETRAIN_UNKNOWN = 0, CP_DRIVETRAIN_ICE, CP_DRIVETRAIN_BEV,
                            CP_DRIVETRAIN_HEV, CP_DRIVETRAIN_PHEV, CP_DRIVETRAIN_FCEV };
enum canproxy_source      { CP_SOURCE_SIMULATED = 0, CP_SOURCE_EMULATOR, CP_SOURCE_LIVE, CP_SOURCE_REPLAY };
enum canproxy_risk        { CP_RISK_NONE = 0, CP_RISK_LOW, CP_RISK_MEDIUM, CP_RISK_HIGH };

#define CP_LANE_LEFT_SEEN        (1u << 0)
#define CP_LANE_RIGHT_SEEN       (1u << 1)
#define CP_LANE_LEFT_DEPARTURE   (1u << 2)
#define CP_LANE_RIGHT_DEPARTURE  (1u << 3)

/* ------------------------------------------------------------------------ */
/* Vehicle state                                                             */
/* A plugin fills what it knows and sets the matching `valid` bits. A field  */
/* whose bit is clear is ignored by the host. `capable` should be stable for */
/* a session and change only when discovery genuinely finds a new source.    */
/* ------------------------------------------------------------------------ */

struct canproxy_vehicle_state {
    uint32_t capable;             /* CANPROXY_SIG_BIT() mask */
    uint32_t valid;               /* CANPROXY_SIG_BIT() mask */

    /* identity, always meaningful */
    int      drivetrain;          /* enum canproxy_drivetrain */
    int      source;              /* enum canproxy_source */
    uint32_t vehicle_id;          /* 0 = unknown; see canproxy_vehicle_id() */

    /* motion */
    double   speed_kmh;
    double   rpm;
    int      gear;                /* enum canproxy_gear */
    int      power_state;         /* enum canproxy_power_state */

    /* electric drive */
    double   motor_power_kw;      /* negative = regeneration */
    double   pack_voltage_v;
    double   pack_current_a;      /* negative = charging */

    /* energy */
    double   soc_pct;             /* 0..100 */
    double   soh_pct;             /* 0..100 */
    double   range_km;
    double   consumption_wh_km;   /* negative = net regeneration */
    int      charging_state;      /* enum canproxy_charging */

    /* trip, environment, thermal, auxiliary */
    double   odometer_km;
    double   ambient_c;
    double   cabin_c;
    double   coolant_c;
    double   fuel_pct;            /* 0..100 */
    double   aux_battery_v;

    /* warning lamps */
    uint32_t lamps;               /* CANPROXY_LAMP_BIT() mask */

    /* driver assist and eco */
    int      eco_score;           /* 0..100 */
    int      speed_limit_kmh;     /* 0 = none known */
    int      collision_risk;      /* enum canproxy_risk */
    unsigned lane_state;          /* CP_LANE_* mask */
    double   lead_gap_m;
};

/* ------------------------------------------------------------------------ */
/* Host services                                                             */
/* All functions are thread-safe; a plugin may call them from any thread.    */
/* ------------------------------------------------------------------------ */

enum canproxy_log_level { CANPROXY_LOG_ERROR = 0, CANPROXY_LOG_WARN, CANPROXY_LOG_INFO, CANPROXY_LOG_DEBUG };

struct canproxy_host {
    void *ctx;
    /* Replace the host's copy of the vehicle state. */
    void (*publish)(void *ctx, const struct canproxy_vehicle_state *state);
    /* 1 = a vehicle is answering / the bus is alive; 0 = nothing there. The
     * host stays in "starting" until the first call. */
    void (*set_link)(void *ctx, int vehicle_present);
    /* Pre-formatted single-line message. */
    void (*log)(void *ctx, int level, const char *message);
};

/* ------------------------------------------------------------------------ */
/* Plugin                                                                    */
/* ------------------------------------------------------------------------ */

struct canproxy_kv {
    const char *key;
    const char *value;
};

struct canproxy_plugin_args {
    const char *vehicle_if;           /* CAN interface to read, may be NULL */
    const struct canproxy_kv *args;   /* --plugin-arg key=value, in order */
    size_t nargs;
};

struct canproxy_plugin {
    uint32_t    abi_version;          /* CANPROXY_PLUGIN_ABI_VERSION */
    const char *name;                 /* short, stable, e.g. "obd2-ice" */
    uint8_t     version_major;
    uint8_t     version_minor;

    /* Allocate. Return NULL on bad arguments (log why). */
    void *(*create)(const struct canproxy_plugin_args *args, const struct canproxy_host *host);
    /* Begin reading the vehicle; typically starts a thread. 0 on success. */
    int   (*start)(void *self);
    /* Stop reading and join threads. Must be safe to call twice. */
    void  (*stop)(void *self);
    /* Free. */
    void  (*destroy)(void *self);
};

typedef const struct canproxy_plugin *(*canproxy_plugin_entry_fn)(void);

/* Every plugin defines this with external linkage. */
const struct canproxy_plugin *canproxy_plugin_entry(void);

/* Helper for plugin authors: a stable 32-bit id from plugin name and VIN
 * ("name/VIN", or just "name" when the VIN is unknown). FNV-1a. */
static inline uint32_t canproxy_vehicle_id(const char *plugin_name, const char *vin_or_null)
{
    uint32_t h = 0x811C9DC5u;
    const char *p;
    for (p = plugin_name; p && *p; p++) { h ^= (uint8_t)*p; h *= 0x01000193u; }
    if (vin_or_null && *vin_or_null) {
        h ^= (uint8_t)'/'; h *= 0x01000193u;
        for (p = vin_or_null; *p; p++) { h ^= (uint8_t)*p; h *= 0x01000193u; }
    }
    return h;
}

/* Helper: look up a key in the argument list, NULL if absent. */
static inline const char *canproxy_arg(const struct canproxy_plugin_args *a, const char *key)
{
    size_t i;
    for (i = 0; i < a->nargs; i++) {
        const char *k = a->args[i].key, *q = key;
        while (*k && *q && *k == *q) { k++; q++; }
        if (*k == '\0' && *q == '\0')
            return a->args[i].value;
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* CANPROXY_PLUGIN_H */
