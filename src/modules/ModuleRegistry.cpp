#include "ModuleRegistry.h"

#include "IDeviceModule.h"
#include "jbdbms/Jbdbms.h"
#include "rtusw/RtuSwMk1.h"
#include "rtusw/RtuSwMk2.h"
#include "upower/Upower.h"

namespace essio {

namespace {
const ModuleTypeInfo TYPES[] = {
    {"upower", "UPower Inverter/Charger", 0},
    {"jbdbms", "JBD Smart BMS", 0},
    {"rtusw_mk1", "RTU Relay Board Mk1 (coils)", 8},
    {"rtusw_mk2", "RTU Relay Board Mk2 (registers)", 4},
};
}

const ModuleTypeInfo* ModuleRegistry::types(size_t& count) {
    count = sizeof(TYPES) / sizeof(TYPES[0]);
    return TYPES;
}

bool ModuleRegistry::isKnown(const String& type) {
    for (const auto& t : TYPES) {
        if (type == t.type) return true;
    }
    return false;
}

IDeviceModule* ModuleRegistry::create(const String& type) {
    if (type == "upower") return new Upower();
    if (type == "jbdbms") return new Jbdbms();
    if (type == "rtusw_mk1") return new RtuSwMk1();
    if (type == "rtusw_mk2") return new RtuSwMk2();
    return nullptr;
}

// docs/12-config-schema.md §2 params 기본값
void ModuleRegistry::defaultParams(const String& type, JsonObject out) {
    if (type == "upower") {
        JsonObject blocks = out["blocks"].to<JsonObject>();
        blocks["grid"] = true;
        blocks["pv"] = true;
        blocks["inverter"] = true;
        blocks["battery"] = true;
        out["write_retries"] = 3;
        out["mask_inactive_output"] = false;
    } else if (type == "jbdbms") {
        out["cell_count"] = 16;
        out["ntc_count"] = 2;
        out["expose_cells"] = true;
        out["expose_protection_bits"] = true;
        JsonObject cl = out["charge_limit"].to<JsonObject>();
        cl["enabled"] = false;
        cl["soc_high"] = 80;
        cl["soc_low"] = 70;
    } else if (type == "rtusw_mk1" || type == "rtusw_mk2") {
        uint8_t n = type == "rtusw_mk1" ? 8 : 4;
        out["write_retries"] = 3;
        JsonArray channels = out["channels"].to<JsonArray>();
        for (uint8_t ch = 1; ch <= n; ch++) {
            JsonObject c = channels.add<JsonObject>();
            c["ch"] = ch;
            c["name"] = String("Channel ") + ch;
            c["enabled"] = true;
        }
    }
}

void ModuleRegistry::schemaJson(JsonObject out) {
    JsonArray types = out["types"].to<JsonArray>();
    for (const auto& t : TYPES) {
        JsonObject o = types.add<JsonObject>();
        o["type"] = t.type;
        o["label"] = t.label;
        o["max_channels"] = t.maxChannels;
        defaultParams(t.type, o["params_default"].to<JsonObject>());
    }
}

}  // namespace essio
