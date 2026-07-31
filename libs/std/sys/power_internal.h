// SPDX-License-Identifier: BSD-3-Clause

#ifndef JELLOVM_POWER_INTERNAL_H
#define JELLOVM_POWER_INTERNAL_H

#include <jello/jdll.h>

#include <stdint.h>

#define POWER_MAX_SUPPLIES 16
#define POWER_MAX_ZONES 32

#define POWER_SNAPSHOT_MAGIC 0x504F5772u

typedef struct power_supply_info {
  char name[64];
  char type[32];
  char status[32];
  int present;
  int percent;
  int voltage_uv;
  int current_ua;
  int energy_uwh;
  int online;
} power_supply_info;

typedef struct thermal_zone_info {
  char name[64];
  char type[64];
  int temp_mc;
  int trip_mc;
} thermal_zone_info;

typedef struct power_snapshot {
  uint32_t magic;
  int supply_count;
  power_supply_info supplies[POWER_MAX_SUPPLIES];
  int zone_count;
  thermal_zone_info zones[POWER_MAX_ZONES];
  int on_ac;
} power_snapshot;

void power_snapshot_populate(power_snapshot* snap);
int power_snapshot_primary_percent(const power_snapshot* snap);
power_snapshot* power_snapshot_from_arg(jdlo_ctx* c, int index);
void power_snapshot_fin(void* payload);

#endif
