// SPDX-License-Identifier: BSD-3-Clause

#include "power_internal.h"

#include <jello/jdll.h>

#include <string.h>

void jdll_std_thermal_zone_count(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  jdl_return_i32(c, (int32_t)snap->zone_count);
}

void jdll_std_thermal_zone_name(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->zone_count) {
    jdl_return_bytes_copy(c, (const uint8_t*)"", 0);
    return;
  }
  const char* name = snap->zones[idx].name;
  jdl_return_bytes_copy(c, (const uint8_t*)name, (uint32_t)strlen(name));
}

void jdll_std_thermal_zone_type(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->zone_count) {
    jdl_return_bytes_copy(c, (const uint8_t*)"", 0);
    return;
  }
  const char* ty = snap->zones[idx].type;
  jdl_return_bytes_copy(c, (const uint8_t*)ty, (uint32_t)strlen(ty));
}

void jdll_std_thermal_zone_temp_mc(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->zone_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->zones[idx].temp_mc);
}

void jdll_std_thermal_zone_trip_mc(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->zone_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->zones[idx].trip_mc);
}
