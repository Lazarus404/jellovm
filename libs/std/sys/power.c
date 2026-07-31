// SPDX-License-Identifier: BSD-3-Clause

#include "power_internal.h"

#include <jello/jdll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JDLL_DEFINE_KIND(power_snapshot);

static int power_use_stub(void) {
  const char* v = getenv("JELLO_POWER_STUB");
  return v && v[0] && v[0] != '0';
}

static void power_snapshot_clear(power_snapshot* snap) {
  if(!snap) return;
  memset(snap, 0, sizeof(*snap));
  snap->magic = POWER_SNAPSHOT_MAGIC;
}

static void power_snapshot_stub(power_snapshot* snap) {
  power_snapshot_clear(snap);
  power_supply_info* bat = &snap->supplies[0];
  snprintf(bat->name, sizeof bat->name, "BAT0");
  snprintf(bat->type, sizeof bat->type, "Battery");
  snprintf(bat->status, sizeof bat->status, "Charging");
  bat->present = 1;
  bat->percent = 87;
  bat->voltage_uv = 12000000;
  bat->current_ua = 500000;
  bat->energy_uwh = 25000000;
  snap->supply_count = 1;

  power_supply_info* ac = &snap->supplies[1];
  snprintf(ac->name, sizeof ac->name, "AC");
  snprintf(ac->type, sizeof ac->type, "Mains");
  snprintf(ac->status, sizeof ac->status, "NotCharging");
  ac->present = 1;
  ac->online = 1;
  ac->percent = -1;
  snap->supply_count = 2;
  snap->on_ac = 1;

  thermal_zone_info* zone = &snap->zones[0];
  snprintf(zone->name, sizeof zone->name, "cpu-thermal");
  snprintf(zone->type, sizeof zone->type, "cpu-thermal");
  zone->temp_mc = 45000;
  zone->trip_mc = 85000;
  snap->zone_count = 1;
}

#if defined(__linux__)

#include <dirent.h>
#include <unistd.h>

static int read_sysfs_text(const char* path, char* out, size_t cap) {
  FILE* f = fopen(path, "r");
  if(!f) return 0;
  if(!fgets(out, (int)cap, f)) {
    fclose(f);
    out[0] = 0;
    return 0;
  }
  fclose(f);
  size_t n = strlen(out);
  while(n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
    out[--n] = 0;
  }
  return 1;
}

static int read_sysfs_int(const char* path, int* out) {
  char buf[64];
  if(!read_sysfs_text(path, buf, sizeof buf)) return 0;
  char* end = NULL;
  long v = strtol(buf, &end, 10);
  if(end == buf) return 0;
  *out = (int)v;
  return 1;
}

static int read_sysfs_online(const char* dir, int* out) {
  char path[512];
  snprintf(path, sizeof path, "%s/online", dir);
  return read_sysfs_int(path, out);
}

static void populate_supply_linux(const char* name, power_supply_info* s) {
  char dir[512];
  snprintf(dir, sizeof dir, "/sys/class/power_supply/%s", name);
  snprintf(s->name, sizeof s->name, "%s", name);
  s->percent = -1;
  s->voltage_uv = -1;
  s->current_ua = -1;
  s->energy_uwh = -1;

  char path[512];
  snprintf(path, sizeof path, "%s/type", dir);
  read_sysfs_text(path, s->type, sizeof s->type);
  if(!s->type[0]) snprintf(s->type, sizeof s->type, "Unknown");

  snprintf(path, sizeof path, "%s/status", dir);
  read_sysfs_text(path, s->status, sizeof s->status);
  if(!s->status[0]) snprintf(s->status, sizeof s->status, "Unknown");

  int present = 1;
  snprintf(path, sizeof path, "%s/present", dir);
  if(read_sysfs_int(path, &present)) s->present = present != 0;
  else s->present = 1;

  int online = 0;
  if(read_sysfs_online(dir, &online)) s->online = online != 0;

  int cap = -1;
  snprintf(path, sizeof path, "%s/capacity", dir);
  if(read_sysfs_int(path, &cap) && cap >= 0 && cap <= 100) s->percent = cap;

  snprintf(path, sizeof path, "%s/voltage_now", dir);
  read_sysfs_int(path, &s->voltage_uv);

  snprintf(path, sizeof path, "%s/current_now", dir);
  read_sysfs_int(path, &s->current_ua);

  snprintf(path, sizeof path, "%s/energy_now", dir);
  read_sysfs_int(path, &s->energy_uwh);
}

static void populate_linux_power(power_snapshot* snap) {
  DIR* d = opendir("/sys/class/power_supply");
  if(!d) return;
  struct dirent* ent;
  while((ent = readdir(d)) != NULL && snap->supply_count < POWER_MAX_SUPPLIES) {
    if(ent->d_name[0] == '.') continue;
    populate_supply_linux(ent->d_name, &snap->supplies[snap->supply_count]);
    snap->supply_count++;
  }
  closedir(d);
}

static void populate_linux_thermal(power_snapshot* snap) {
  DIR* d = opendir("/sys/class/thermal");
  if(!d) return;
  struct dirent* ent;
  while((ent = readdir(d)) != NULL && snap->zone_count < POWER_MAX_ZONES) {
    if(strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;
    thermal_zone_info* z = &snap->zones[snap->zone_count];
    snprintf(z->name, sizeof z->name, "%s", ent->d_name);

    char dir[512];
    snprintf(dir, sizeof dir, "/sys/class/thermal/%s", ent->d_name);
    char path[512];
    snprintf(path, sizeof path, "%s/type", dir);
    read_sysfs_text(path, z->type, sizeof z->type);

    int temp = -1;
    snprintf(path, sizeof path, "%s/temp", dir);
    if(read_sysfs_int(path, &temp)) z->temp_mc = temp;

    int trip = -1;
    snprintf(path, sizeof path, "%s/trip_point_0_temp", dir);
    if(read_sysfs_int(path, &trip)) z->trip_mc = trip;

    snap->zone_count++;
  }
  closedir(d);
}

static void populate_linux(power_snapshot* snap) {
  populate_linux_power(snap);
  populate_linux_thermal(snap);
}

#elif defined(_WIN32)

#include <windows.h>

static void populate_windows(power_snapshot* snap) {
  SYSTEM_POWER_STATUS sps;
  memset(&sps, 0, sizeof sps);
  if(!GetSystemPowerStatus(&sps)) return;

  if(sps.ACLineStatus == 1) {
    power_supply_info* ac = &snap->supplies[snap->supply_count++];
    snprintf(ac->name, sizeof ac->name, "AC");
    snprintf(ac->type, sizeof ac->type, "Mains");
    snprintf(ac->status, sizeof ac->status, "NotCharging");
    ac->present = 1;
    ac->online = 1;
    ac->percent = -1;
    snap->on_ac = 1;
  }

  if(sps.BatteryFlag != 128) {
    power_supply_info* bat = &snap->supplies[snap->supply_count++];
    snprintf(bat->name, sizeof bat->name, "Battery");
    snprintf(bat->type, sizeof bat->type, "Battery");
    bat->present = 1;
    if(sps.BatteryLifePercent != 255) bat->percent = (int)sps.BatteryLifePercent;
    else bat->percent = -1;
    if(sps.ACLineStatus == 1) snprintf(bat->status, sizeof bat->status, "Charging");
    else if(sps.BatteryFlag & 8) snprintf(bat->status, sizeof bat->status, "Charging");
    else snprintf(bat->status, sizeof bat->status, "Discharging");
  }
}

#elif defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>

static void populate_macos(power_snapshot* snap) {
  CFTypeRef blob = IOPSCopyPowerSourcesInfo();
  if(!blob) return;

  CFArrayRef sources = IOPSCopyPowerSourcesList(blob);
  if(!sources) {
    CFRelease(blob);
    return;
  }

  CFIndex count = CFArrayGetCount(sources);
  for(CFIndex i = 0; i < count && snap->supply_count < POWER_MAX_SUPPLIES; i++) {
    CFDictionaryRef desc =
        IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, i));
    if(!desc) continue;

    power_supply_info* s = &snap->supplies[snap->supply_count];
    snprintf(s->name, sizeof s->name, "mac-battery-%ld", (long)i);

    CFStringRef type_ref = CFDictionaryGetValue(desc, CFSTR(kIOPSTypeKey));
    char type_buf[32];
    if(type_ref && CFGetTypeID(type_ref) == CFStringGetTypeID()) {
      CFStringGetCString(type_ref, type_buf, sizeof type_buf, kCFStringEncodingUTF8);
      if(strcmp(type_buf, "InternalBattery") == 0) snprintf(s->type, sizeof s->type, "Battery");
      else if(strcmp(type_buf, "AC Power") == 0) snprintf(s->type, sizeof s->type, "Mains");
      else snprintf(s->type, sizeof s->type, "%s", type_buf);
    } else {
      snprintf(s->type, sizeof s->type, "Unknown");
    }

    CFNumberRef cur = CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey));
    CFNumberRef max = CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey));
    if(cur && max) {
      int c = 0;
      int m = 0;
      CFNumberGetValue(cur, kCFNumberIntType, &c);
      CFNumberGetValue(max, kCFNumberIntType, &m);
      if(m > 0) s->percent = (c * 100) / m;
    } else {
      s->percent = -1;
    }

    CFBooleanRef charging = CFDictionaryGetValue(desc, CFSTR(kIOPSIsChargingKey));
    if(charging && CFBooleanGetValue(charging)) snprintf(s->status, sizeof s->status, "Charging");
    else snprintf(s->status, sizeof s->status, "Discharging");

    CFBooleanRef present = CFDictionaryGetValue(desc, CFSTR(kIOPSIsPresentKey));
    s->present = (!present || CFBooleanGetValue(present)) ? 1 : 0;

    if(strcmp(s->type, "Mains") == 0) {
      s->online = 1;
      snap->on_ac = 1;
      s->percent = -1;
    }

    snap->supply_count++;
  }

  CFRelease(sources);
  CFRelease(blob);
}

#endif

static void power_recompute_on_ac(power_snapshot* snap) {
  snap->on_ac = 0;
  for(int i = 0; i < snap->supply_count; i++) {
    power_supply_info* s = &snap->supplies[i];
    if(!s->present) continue;
    if((strcmp(s->type, "Mains") == 0 || strcmp(s->type, "USB") == 0) && s->online) {
      snap->on_ac = 1;
      return;
    }
  }
}

void power_snapshot_populate(power_snapshot* snap) {
  power_snapshot_clear(snap);
  if(power_use_stub()) {
    power_snapshot_stub(snap);
    return;
  }

#if defined(__linux__)
  populate_linux(snap);
#elif defined(_WIN32)
  populate_windows(snap);
#elif defined(__APPLE__)
  populate_macos(snap);
#endif

  if(snap->supply_count == 0 && snap->zone_count == 0) {
    power_snapshot_stub(snap);
    return;
  }

  power_recompute_on_ac(snap);
}

int power_snapshot_primary_percent(const power_snapshot* snap) {
  int best = -1;
  for(int i = 0; i < snap->supply_count; i++) {
    const power_supply_info* s = &snap->supplies[i];
    if(!s->present) continue;
    if(strcmp(s->type, "Battery") != 0) continue;
    if(s->percent < 0 || s->percent > 100) continue;
    if(s->percent > best) best = s->percent;
  }
  return best;
}

void power_snapshot_fin(void* payload) {
  free(payload);
}

power_snapshot* power_snapshot_from_arg(jdlo_ctx* c, int index) {
  power_snapshot* snap = (power_snapshot*)jdl_arg_abstract_payload(c, index);
  if(!snap || snap->magic != POWER_SNAPSHOT_MAGIC) {
    jdl_fail(c, "power: invalid snapshot handle");
    return NULL;
  }
  return snap;
}

void jdll_std_power_snapshot(jdlo_ctx* c) {
  power_snapshot* snap = (power_snapshot*)calloc(1, sizeof(*snap));
  if(!snap) {
    jdl_fail(c, "power_snapshot: oom");
    return;
  }
  power_snapshot_populate(snap);
  jdl_return_abstract(c, snap, power_snapshot_fin);
}

void jdll_std_power_release(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  snap->magic = 0;
  jdl_return_bool(c, 1);
}

void jdll_std_power_supply_count(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  jdl_return_i32(c, (int32_t)snap->supply_count);
}

void jdll_std_power_supply_name(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_bytes_copy(c, (const uint8_t*)"", 0);
    return;
  }
  const char* name = snap->supplies[idx].name;
  jdl_return_bytes_copy(c, (const uint8_t*)name, (uint32_t)strlen(name));
}

void jdll_std_power_supply_type(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_bytes_copy(c, (const uint8_t*)"Unknown", 7);
    return;
  }
  const char* ty = snap->supplies[idx].type;
  jdl_return_bytes_copy(c, (const uint8_t*)ty, (uint32_t)strlen(ty));
}

void jdll_std_power_supply_present(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_return_bool(c, snap->supplies[idx].present ? 1 : 0);
}

void jdll_std_power_supply_status(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_bytes_copy(c, (const uint8_t*)"Unknown", 7);
    return;
  }
  const char* st = snap->supplies[idx].status;
  jdl_return_bytes_copy(c, (const uint8_t*)st, (uint32_t)strlen(st));
}

void jdll_std_power_percent(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->supplies[idx].percent);
}

void jdll_std_power_on_ac(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  jdl_return_bool(c, snap->on_ac ? 1 : 0);
}

void jdll_std_power_primary_percent(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  jdl_return_i32(c, (int32_t)power_snapshot_primary_percent(snap));
}

void jdll_std_power_voltage_uv(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->supplies[idx].voltage_uv);
}

void jdll_std_power_current_ua(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->supplies[idx].current_ua);
}

void jdll_std_power_energy_uwh(jdlo_ctx* c) {
  power_snapshot* snap = power_snapshot_from_arg(c, 0);
  if(!snap) return;
  int32_t idx = jdl_arg_i32(c, 1);
  if(idx < 0 || idx >= snap->supply_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)snap->supplies[idx].energy_uwh);
}
