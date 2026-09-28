#include "ModuleRegistry.h"

#include "IDeviceModule.h"
#include "jbdbms/Jbdbms.h"
#include "mach/Mach.h"
#include "rtusw/RtuSwMk1.h"
#include "rtusw/RtuSwMk2.h"
#include "upower/Upower.h"

namespace essio {

namespace {
// 기본값 출처: docs/03(UP5000 Slave 10 / 115200), docs/04(JBD 9600), docs/05·06(RTU 9600), 설계서 §11.2
const ModuleTypeInfo TYPES[] = {
    {"upower", "UP5000 (Modbus RTU)", "Inverter", 0, "upower", 10, 115200, 5000},
    {"jbdbms", "JBD BMS", "BMS", 0, "bms", 0, 9600, 5000},
    {"rtusw_mk1", "RTU 스위치 Mk1 (Coil)", "Switch", 8, "rtu", 255, 9600, 3000},
    {"rtusw_mk2", "RTU 스위치 Mk2 (Register)", "Switch", 4, "rtu", 1, 9600, 3000},
    // 프로토콜 미확인 — 수신 바이트만 기록하는 더미. 보레이트는 스니핑하며 맞춘다.
    {"mach", "MACH BMS (미구현·스니핑)", "BMS", 0, "mach", 0, 9600, 1000},
};
}

const ModuleTypeInfo* ModuleRegistry::types(size_t& count) {
    count = sizeof(TYPES) / sizeof(TYPES[0]);
    return TYPES;
}

const ModuleTypeInfo* ModuleRegistry::find(const String& type) {
    for (const auto& t : TYPES) {
        if (type == t.type) return &t;
    }
    return nullptr;
}

bool ModuleRegistry::isKnown(const String& type) {
    return find(type) != nullptr;
}

IDeviceModule* ModuleRegistry::create(const String& type) {
    if (type == "upower") return new Upower();
    if (type == "jbdbms") return new Jbdbms();
    if (type == "rtusw_mk1") return new RtuSwMk1();
    if (type == "rtusw_mk2") return new RtuSwMk2();
    if (type == "mach") return new Mach();
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
        out["mask_by_grid_prio"] = false;
        JsonObject sm = out["storage_mode"].to<JsonObject>();
        sm["enabled"] = false;
        sm["cells"] = 16;
        sm["bcv_mv"] = 3650;
        sm["fcv_mv"] = 3450;
        sm["bvr_mv"] = 3380;
        sm["storage_bcv_mv"] = 3400;
        sm["storage_fcv_mv"] = 3300;
        sm["storage_bvr_mv"] = 3200;
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
        o["slug"] = t.slug;
        o["slave_id"] = t.slaveId;
        o["baud"] = t.baud;
        o["poll_interval_ms"] = t.pollMs;
        defaultParams(t.type, o["params_default"].to<JsonObject>());
    }
}

}  // namespace essio
