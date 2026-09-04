#include "Mapping.h"
#include <cstring>

namespace canproxy {

uint32_t capabilityBitFor(canproxy_signal sig)
{
    switch (sig) {
    case CANPROXY_SIG_SPEED:          return CANPROXY_CAP_SPEED;
    case CANPROXY_SIG_RPM:            return CANPROXY_CAP_RPM;
    case CANPROXY_SIG_GEAR:           return CANPROXY_CAP_GEAR;
    case CANPROXY_SIG_POWER_STATE:    return CANPROXY_CAP_POWER_STATE;
    case CANPROXY_SIG_MOTOR_POWER:    return CANPROXY_CAP_MOTOR_POWER;
    case CANPROXY_SIG_PACK_VOLTAGE:   return CANPROXY_CAP_PACK_VI;
    case CANPROXY_SIG_PACK_CURRENT:   return CANPROXY_CAP_PACK_VI;
    case CANPROXY_SIG_SOC:            return CANPROXY_CAP_SOC;
    case CANPROXY_SIG_SOH:            return CANPROXY_CAP_SOH;
    case CANPROXY_SIG_RANGE:          return CANPROXY_CAP_RANGE;
    case CANPROXY_SIG_CONSUMPTION:    return CANPROXY_CAP_CONSUMPTION;
    case CANPROXY_SIG_CHARGING_STATE: return CANPROXY_CAP_CHARGING;
    case CANPROXY_SIG_ODOMETER:       return CANPROXY_CAP_ODOMETER;
    case CANPROXY_SIG_AMBIENT_TEMP:   return CANPROXY_CAP_AMBIENT_TEMP;
    case CANPROXY_SIG_CABIN_TEMP:     return CANPROXY_CAP_CABIN_TEMP;
    case CANPROXY_SIG_COOLANT_TEMP:   return CANPROXY_CAP_COOLANT_TEMP;
    case CANPROXY_SIG_FUEL_LEVEL:     return CANPROXY_CAP_FUEL_LEVEL;
    case CANPROXY_SIG_AUX_BATTERY:    return CANPROXY_CAP_AUX_BATTERY;
    case CANPROXY_SIG_ECO_SCORE:
    case CANPROXY_SIG_SPEED_LIMIT:
    case CANPROXY_SIG_COLLISION_RISK:
    case CANPROXY_SIG_LANE_STATE:
    case CANPROXY_SIG_LEAD_GAP:       return CANPROXY_CAP_ASSIST;
    case CANPROXY_SIG_COUNT:          break;
    }
    return 0;
}

uint32_t capabilitiesFrom(uint32_t capableMask)
{
    uint32_t caps = 0;
    for (int i = 0; i < CANPROXY_SIG_COUNT; i++)
        if (capableMask & CANPROXY_SIG_BIT(i))
            caps |= capabilityBitFor(static_cast<canproxy_signal>(i));
    return caps;
}

uint32_t telltaleBitFor(canproxy_lamp lamp)
{
    switch (lamp) {
    case CANPROXY_LAMP_ENGINE:               return CANPROXY_TT_ENGINE;
    case CANPROXY_LAMP_OIL:                  return CANPROXY_TT_OIL;
    case CANPROXY_LAMP_BATTERY:              return CANPROXY_TT_BATTERY;
    case CANPROXY_LAMP_BRAKE:                return CANPROXY_TT_BRAKE;
    case CANPROXY_LAMP_TURN_LEFT:            return CANPROXY_TT_LEFT;
    case CANPROXY_LAMP_TURN_RIGHT:           return CANPROXY_TT_RIGHT;
    case CANPROXY_LAMP_HIGH_BEAM:            return CANPROXY_TT_HIGHBEAM;
    case CANPROXY_LAMP_DOOR:                 return CANPROXY_TT_DOOR;
    case CANPROXY_LAMP_SEATBELT:             return CANPROXY_TT_SEATBELT;
    case CANPROXY_LAMP_ABS:                  return CANPROXY_TT_ABS;
    case CANPROXY_LAMP_TRACTION:             return CANPROXY_TT_TRACTION;
    case CANPROXY_LAMP_TPMS:                 return CANPROXY_TT_TPMS;
    case CANPROXY_LAMP_EV_READY:             return CANPROXY_TT_EV_READY;
    case CANPROXY_LAMP_CHARGING:             return CANPROXY_TT_CHARGING;
    case CANPROXY_LAMP_LIMITED_POWER:        return CANPROXY_TT_LIMITED_POWER;
    case CANPROXY_LAMP_LOW_TRACTION_BATTERY: return CANPROXY_TT_LOW_TRACTION_BATTERY;
    case CANPROXY_LAMP_PARK_BRAKE:           return CANPROXY_TT_PARK_BRAKE;
    case CANPROXY_LAMP_LOW_FUEL:             return CANPROXY_TT_LOW_FUEL;
    case CANPROXY_LAMP_FOG:                  return CANPROXY_TT_FOG;
    case CANPROXY_LAMP_HV_SYSTEM_FAULT:      return CANPROXY_TT_HV_SYSTEM_FAULT;
    case CANPROXY_LAMP_COUNT:                break;
    }
    return 0;
}

uint32_t telltalesFrom(uint32_t lampsMask)
{
    uint32_t bits = 0;
    for (int i = 0; i < CANPROXY_LAMP_COUNT; i++)
        if (lampsMask & CANPROXY_LAMP_BIT(i))
            bits |= telltaleBitFor(static_cast<canproxy_lamp>(i));
    return bits;
}

uint8_t gearFrom(int g)
{
    switch (g) {
    case CP_GEAR_P: return CANPROXY_GEAR_P;
    case CP_GEAR_R: return CANPROXY_GEAR_R;
    case CP_GEAR_N: return CANPROXY_GEAR_N;
    case CP_GEAR_D: return CANPROXY_GEAR_D;
    case CP_GEAR_L: return CANPROXY_GEAR_L;
    default:              return CANPROXY_SNA_U8;
    }
}

uint8_t powerStateFrom(int p)
{
    switch (p) {
    case CP_POWER_OFF:   return CANPROXY_POWER_OFF;
    case CP_POWER_ACC:   return CANPROXY_POWER_ACC;
    case CP_POWER_ON:    return CANPROXY_POWER_ON;
    case CP_POWER_READY: return CANPROXY_POWER_READY;
    default:                   return CANPROXY_SNA_U8;
    }
}

uint8_t chargingStateFrom(int c)
{
    switch (c) {
    case CP_CHARGING_NONE:     return CANPROXY_CHARGE_NONE;
    case CP_CHARGING_AC:       return CANPROXY_CHARGE_AC;
    case CP_CHARGING_DC:       return CANPROXY_CHARGE_DC;
    case CP_CHARGING_COMPLETE: return CANPROXY_CHARGE_COMPLETE;
    default:                         return CANPROXY_SNA_U8;
    }
}

uint8_t drivetrainFrom(int d)
{
    switch (d) {
    case CP_DRIVETRAIN_ICE:  return CANPROXY_DRIVETRAIN_ICE;
    case CP_DRIVETRAIN_BEV:  return CANPROXY_DRIVETRAIN_BEV;
    case CP_DRIVETRAIN_HEV:  return CANPROXY_DRIVETRAIN_HEV;
    case CP_DRIVETRAIN_PHEV: return CANPROXY_DRIVETRAIN_PHEV;
    case CP_DRIVETRAIN_FCEV: return CANPROXY_DRIVETRAIN_FCEV;
    default:                       return CANPROXY_DRIVETRAIN_UNKNOWN;
    }
}

uint8_t sourceKindFrom(int s)
{
    switch (s) {
    case CP_SOURCE_EMULATOR: return CANPROXY_SOURCE_EMULATOR;
    case CP_SOURCE_LIVE:     return CANPROXY_SOURCE_LIVE;
    case CP_SOURCE_REPLAY:   return CANPROXY_SOURCE_REPLAY;
    default:                       return CANPROXY_SOURCE_SIMULATED;
    }
}

uint8_t collisionRiskFrom(int r)
{
    switch (r) {
    case CP_RISK_LOW:    return CANPROXY_RISK_LOW;
    case CP_RISK_MEDIUM: return CANPROXY_RISK_MEDIUM;
    case CP_RISK_HIGH:   return CANPROXY_RISK_HIGH;
    default:                   return CANPROXY_RISK_NONE;
    }
}

static inline bool has(const canproxy_vehicle_state &s, bool live, canproxy_signal sig)
{
    return live && (s.valid & CANPROXY_SIG_BIT(sig)) && (s.capable & CANPROXY_SIG_BIT(sig));
}

void framesFrom(const canproxy_vehicle_state &s, bool live, uint8_t proxyState, uint8_t counter,
                uint8_t pluginMajor, uint8_t pluginMinor, Frames &f)
{
    std::memset(&f, 0, sizeof f);

    f.status.contract_major = CANPROXY_CONTRACT_MAJOR;
    f.status.contract_minor = CANPROXY_CONTRACT_MINOR;
    f.status.counter = counter;
    f.status.proxy_state = proxyState;
    f.status.capabilities = capabilitiesFrom(s.capable);

    f.identity.drivetrain = drivetrainFrom(s.drivetrain);
    f.identity.source_kind = sourceKindFrom(s.source);
    f.identity.plugin_major = pluginMajor;
    f.identity.plugin_minor = pluginMinor;
    f.identity.vehicle_id = s.vehicle_id;

    f.motion.speed_valid = has(s, live, CANPROXY_SIG_SPEED);
    f.motion.speed_kmh = s.speed_kmh;
    f.motion.rpm_valid = has(s, live, CANPROXY_SIG_RPM);
    f.motion.rpm = s.rpm < 0 ? 0 : (s.rpm > 65534.0 ? 65534 : static_cast<uint16_t>(s.rpm + 0.5));
    f.motion.gear_valid = has(s, live, CANPROXY_SIG_GEAR) && gearFrom(s.gear) != CANPROXY_SNA_U8;
    f.motion.gear = gearFrom(s.gear);
    f.motion.power_state_valid = has(s, live, CANPROXY_SIG_POWER_STATE) && powerStateFrom(s.power_state) != CANPROXY_SNA_U8;
    f.motion.power_state = powerStateFrom(s.power_state);

    f.edrive.motor_power_valid = has(s, live, CANPROXY_SIG_MOTOR_POWER);
    f.edrive.motor_power_kw = s.motor_power_kw;
    f.edrive.pack_voltage_valid = has(s, live, CANPROXY_SIG_PACK_VOLTAGE);
    f.edrive.pack_voltage_v = s.pack_voltage_v;
    f.edrive.pack_current_valid = has(s, live, CANPROXY_SIG_PACK_CURRENT);
    f.edrive.pack_current_a = s.pack_current_a;

    f.telltales.bits = live ? telltalesFrom(s.lamps) : 0;

    f.energy.soc_valid = has(s, live, CANPROXY_SIG_SOC);
    f.energy.soc_pct = s.soc_pct;
    f.energy.soh_valid = has(s, live, CANPROXY_SIG_SOH);
    f.energy.soh_pct = s.soh_pct;
    f.energy.range_valid = has(s, live, CANPROXY_SIG_RANGE);
    f.energy.range_km = s.range_km < 0 ? 0 : (s.range_km > 65534.0 ? 65534 : static_cast<uint16_t>(s.range_km + 0.5));
    f.energy.consumption_valid = has(s, live, CANPROXY_SIG_CONSUMPTION);
    {
        double c = s.consumption_wh_km;
        if (c > 32767.0) c = 32767.0;
        if (c < -32767.0) c = -32767.0;
        f.energy.consumption_wh_km = static_cast<int16_t>(canproxy_round(c));
    }
    f.energy.charging_state_valid = has(s, live, CANPROXY_SIG_CHARGING_STATE) && chargingStateFrom(s.charging_state) != CANPROXY_SNA_U8;
    f.energy.charging_state = chargingStateFrom(s.charging_state);

    f.trip.odometer_valid = has(s, live, CANPROXY_SIG_ODOMETER);
    f.trip.odometer_km = s.odometer_km;
    f.trip.ambient_valid = has(s, live, CANPROXY_SIG_AMBIENT_TEMP);
    f.trip.ambient_c = static_cast<int8_t>(canproxy_enc_i8(s.ambient_c, 1.0, true));
    f.trip.cabin_valid = has(s, live, CANPROXY_SIG_CABIN_TEMP);
    f.trip.cabin_c = static_cast<int8_t>(canproxy_enc_i8(s.cabin_c, 1.0, true));

    f.thermal.coolant_valid = has(s, live, CANPROXY_SIG_COOLANT_TEMP);
    f.thermal.coolant_c = static_cast<int8_t>(canproxy_enc_i8(s.coolant_c, 1.0, true));
    f.thermal.fuel_valid = has(s, live, CANPROXY_SIG_FUEL_LEVEL);
    f.thermal.fuel_pct = s.fuel_pct;
    f.thermal.aux_battery_valid = has(s, live, CANPROXY_SIG_AUX_BATTERY);
    f.thermal.aux_battery_v = s.aux_battery_v;

    f.assist.eco_score_valid = has(s, live, CANPROXY_SIG_ECO_SCORE);
    f.assist.eco_score = static_cast<uint8_t>(s.eco_score < 0 ? 0 : (s.eco_score > 100 ? 100 : s.eco_score));
    f.assist.speed_limit_valid = has(s, live, CANPROXY_SIG_SPEED_LIMIT);
    f.assist.speed_limit_kmh = static_cast<uint8_t>(s.speed_limit_kmh < 0 ? 0 : (s.speed_limit_kmh > 254 ? 254 : s.speed_limit_kmh));
    f.assist.collision_risk_valid = has(s, live, CANPROXY_SIG_COLLISION_RISK);
    f.assist.collision_risk = collisionRiskFrom(s.collision_risk);
    f.assist.lane_state_valid = has(s, live, CANPROXY_SIG_LANE_STATE);
    f.assist.lane_state = static_cast<uint8_t>(s.lane_state & CANPROXY_LANE_VALID_MASK);
    f.assist.lead_gap_valid = has(s, live, CANPROXY_SIG_LEAD_GAP);
    f.assist.lead_gap_m = s.lead_gap_m;
}

} // namespace canproxy
