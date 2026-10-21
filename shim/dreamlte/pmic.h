#pragma once
#include <stdint.h>

/* Powers the touch rails, logs the CPU clock; returns the raw RTC bytes
 * (sec, min, hour, weekday, day, month, year, next) little-endian. */
uint64_t pmic_init(void (*kv)(const char *, uint64_t), void (*out)(const char *));

/* Logs the Mongoose (BUCK2) and A53 (BUCK3) rails; enables BUCK2 if off.
 * Call after pmic_init. */
void pmic_mngs_rail(void);
