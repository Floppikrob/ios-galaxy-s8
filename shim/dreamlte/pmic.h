#pragma once
#include <stdint.h>

/* Powers the touch rails, logs the CPU clock; returns the raw RTC bytes
 * (sec, min, hour, weekday, day, month, year, next) little-endian. */
uint64_t pmic_init(void (*kv)(const char *, uint64_t), void (*out)(const char *));

/* Logs the Mongoose (BUCK2) and A53 (BUCK3) rails; enables BUCK2 if off.
 * Call after pmic_init. */
void pmic_mngs_rail(void);

/* Logs the Mali rail (BUCK6); enables it at 0.8 V if it is off. */
void pmic_g3d_rail(void);

/* "s8boost=1": BUCK3 to 0.95 V, A53 PLL to 1449.5 MHz; logs the result. */
void pmic_cpu_boost(void);
