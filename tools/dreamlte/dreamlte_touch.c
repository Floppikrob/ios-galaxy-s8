#if defined(DREAMLTE)
/*
 * Galaxy S8 I2C devices, polled (no XNU interrupt routing for them).
 * The loader (shim/dreamlte) ungates both USI blocks with their clock on
 * OSCCLK, sets USI v1 I2C mode and the pins, and powers the touch rails.
 *
 * Touch: Samsung S6SY761 at 0x48 on hsi2c_23 (USI09, 0x108e0000), its
 * interrupt gpa1-0 active low (boot 26: boot status 0x20, device id
 * 0xac3761). Event protocol from mainline s6sy761.c; raw 0..4095 axes go
 * to SweetHIDTouch's 1080x1920 grid.
 *
 * Battery: MAX77865 on hsi2c_32 (USI13 second HSI2C, 0x10970000), fuel
 * gauge 0x36 and charger 0x69 (Samsung max77865_fuelgauge.c/_charger.c).
 */
#define S8TS_HSI2C_PA           0x108e0000u
#define S8BAT_HSI2C_PA          0x10970000u
#define S8TS_ALIVE_PA           0x164b0000u
#define S8TS_GPA1_DAT           0x44u
#define S8TS_ADDR               0x48u
#define S8BAT_FG                0x36u
#define S8BAT_CHG               0x69u
#define S8TS_FINGERS            10
#define S8_I2C_CLKIN            26000000u
#define S8_I2C_SCL_HZ           400000u

static vm_offset_t s8ts_i2c, s8ts_alive, s8bat_i2c;
static int s8ts_sleep;
static int s8ts_native;         /* sweetfb_native: iOS sees the whole panel */
static int s8ts_scale;          /* sweetfb_scale: canvas stretched over the panel */
static bool s8ts_down[S8TS_FINGERS];
static uint16_t s8ts_x[S8TS_FINGERS], s8ts_y[S8TS_FINGERS];

/* i2c-exynos5.c: reset, Exynos8895 fast-mode timing, master auto mode. */
static void
s8i2c_reset(vm_offset_t bus)
{
	a5touch_write(bus, 0x00, a5touch_read(bus, 0x00) | (1u << 31));
	a5touch_write(bus, 0x00, a5touch_read(bus, 0x00) & ~(1u << 31));
	unsigned ftl = (a5touch_read(bus, 0x40) >> 16) & 7;
	unsigned temp = S8_I2C_CLKIN / S8_I2C_SCL_HZ - (ftl + 3) * 2;
	unsigned div = temp / 512;
	unsigned cyc = (temp + ((ftl + 3) % (div + 1)) * 2) / (div + 1) - 2;
	unsigned l = cyc * 64 / 100, h = cyc - l;
	a5touch_write(bus, 0x60, l << 24 | l << 16 | l << 8);
	a5touch_write(bus, 0x64, (l / 2) << 24 | l << 8 | h);
	a5touch_write(bus, 0x68, div << 16 | cyc);
	a5touch_write(bus, 0x6c, l / 2);
	a5touch_write(bus, 0x48, a5touch_read(bus, 0x48) & ~(1u << 31));
	a5touch_write(bus, 0x00, 0x9);
	a5touch_write(bus, 0x08, 0xf);
	a5touch_write(bus, 0x40, a5touch_read(bus, 0x40) | (1u << 31));
}

/* INT_STATUS: bit 7 TRANS_DONE, bits 8-11 abort/no-ack/no-dev/timeout. */
static bool
s8i2c_wait_done(vm_offset_t bus)
{
	for (unsigned us = 0; us < 20000; us++) {
		uint32_t s = a5touch_read(bus, 0x24);
		if (s & 0xf80u) {
			a5touch_write(bus, 0x24, s);
			return !(s & 0xf00u);
		}
		delay(1);
	}
	return false;
}

/* Write wlen bytes, then read rlen bytes (repeated start); STOP at the end. */
static bool
s8i2c_xfer(vm_offset_t bus, unsigned addr, const uint8_t *w, unsigned wlen,
    uint8_t *r, unsigned rlen)
{
	a5touch_write(bus, 0x24, a5touch_read(bus, 0x24));
	a5touch_write(bus, 0x70, addr << 10);
	a5touch_write(bus, 0x20, 0xf80u);
	if (wlen) {
		a5touch_write(bus, 0x04, 3);
		a5touch_write(bus, 0x00, 0x9 | 0x80);
		a5touch_write(bus, 0x44, (rlen ? 0 : (1u << 17)) | wlen | (1u << 31));
		for (unsigned i = 0; i < wlen; i++) {
			for (unsigned n = 0; n < 1000 && (a5touch_read(bus, 0x30) & (1u << 7)); n++) {
				delay(1);
			}
			a5touch_write(bus, 0x34, w[i]);
		}
		if (!s8i2c_wait_done(bus)) {
			goto fail;
		}
	}
	if (rlen) {
		a5touch_write(bus, 0x04, 3);
		a5touch_write(bus, 0x00, 0x9 | 0x40);
		a5touch_write(bus, 0x44, (1u << 16) | (1u << 17) | rlen | (1u << 31));
		for (unsigned i = 0; i < rlen; i++) {
			unsigned n = 0;
			while ((a5touch_read(bus, 0x30) & (1u << 24)) && n++ < 20000) {
				delay(1);
			}
			if (a5touch_read(bus, 0x30) & (1u << 24)) {
				goto fail;
			}
			r[i] = (uint8_t)a5touch_read(bus, 0x38);
		}
		if (!s8i2c_wait_done(bus)) {
			goto fail;
		}
	}
	a5touch_write(bus, 0x00, 0x9);
	return true;
fail:
	s8i2c_reset(bus);
	return false;
}

static bool
s8ts_cmd_read(uint8_t cmd, uint8_t *r, unsigned rlen)
{
	return s8i2c_xfer(s8ts_i2c, S8TS_ADDR, &cmd, 1, r, rlen);
}

static void
s8ts_report(const uint8_t *ev)
{
	if ((ev[0] & 3) != 0 || !(ev[0] & 0x3c)) {
		return;         /* status/info event, or no touch id */
	}
	unsigned tid = ((ev[0] & 0x3cu) >> 2) - 1;
	unsigned state = (ev[0] & 0xc0u) >> 6;
	if (tid >= S8TS_FINGERS) {
		return;
	}
	unsigned x = ((unsigned)ev[1] << 4) | (ev[3] >> 4);
	unsigned y = ((unsigned)ev[2] << 4) | (ev[3] & 0xf);
	if (s8ts_native || s8ts_scale) {
		/* The canvas covers the whole panel. */
		x = x * 1080 / 4096;
		y = y * 1920 / 4096;
	} else {
		/* 1125x2436 is shown centered on the 1440x2960 panel. */
		int px = (int)(x * 1440 / 4096) - (1440 - 1125) / 2;
		int py = (int)(y * 2960 / 4096) - (2960 - 2436) / 2;
		px = px < 0 ? 0 : (px > 1124 ? 1124 : px);
		py = py < 0 ? 0 : (py > 2435 ? 2435 : py);
		x = (unsigned)px * 1080 / 1125;
		y = (unsigned)py * 1920 / 2436;
	}
	if (state == 1 || state == 2) {             /* press, move */
		if (!s8ts_down[tid]) {
			printf("S8 TOUCH: down id=%u x=%u y=%u\n", tid, x, y);
		}
		s8ts_down[tid] = true;
		s8ts_x[tid] = (uint16_t)x;
		s8ts_y[tid] = (uint16_t)y;
		if (tid < 5) {
			sweet_hid_touch((int)tid, 1, (int)x, (int)y);
		}
	} else if (state == 3 && s8ts_down[tid]) { /* release */
		s8ts_down[tid] = false;
		if (tid < 5) {
			sweet_hid_touch((int)tid, 0, s8ts_x[tid], s8ts_y[tid]);
		}
	}
}

static void
s8ts_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&s8ts_sleep, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static void
s8ts_thread(void *arg, wait_result_t result)
{
	uint8_t buf[8 * 32];
	unsigned errors = 0;
	(void)arg;
	(void)result;
	printf("S8 TOUCH: worker started\n");
	for (;;) {
		/* INT is level low while events are queued. */
		if (a5touch_read(s8ts_alive, S8TS_GPA1_DAT) & 1u) {
			s8ts_wait(8);
			continue;
		}
		if (!s8ts_cmd_read(0x60, buf, 8)) {         /* READ_ONE_EVENT */
			if (++errors <= 8 || !(errors & 255)) {
				printf("S8 TOUCH: event read failed (%u)\n", errors);
			}
			s8ts_wait(20);
			continue;
		}
		unsigned left = buf[7] & 0x3fu;
		if (buf[0] && left && left < 32 &&
		    !s8ts_cmd_read(0x61, buf + 8, left * 8)) {   /* READ_ALL_EVENT */
			left = 0;
		}
		if (!buf[0]) {
			s8ts_wait(4);       /* INT low with an empty queue */
			continue;
		}
		for (unsigned i = 0; i <= left; i++) {
			if (buf[i * 8]) {
				s8ts_report(buf + i * 8);
			}
		}
		errors = 0;
	}
}

static bool
s8bat_read16(unsigned addr, uint8_t reg, uint16_t *value)
{
	uint8_t b[2];
	if (!s8i2c_xfer(s8bat_i2c, addr, &reg, 1, b, 2)) {
		return false;
	}
	*value = (uint16_t)(b[0] | (b[1] << 8));
	return true;
}

/*
 * SweetBattery (patch 0124) calls this every 30 s. 0 = valid sample.
 * MAX77865 fuel gauge: SOCREP 0x06 in 1/256 %, VCELL 0x09 in 78.125 uV,
 * CURRENT 0x0a signed 156.25 uA (0.01 ohm sense, positive = charging).
 * Charger: INT_OK 0xb2 bit 6 CHGIN_OK; DETAILS_01 0xb4 [3:0] 0-2 charging,
 * 3-4 done.
 */
int dreamlte_battery_read(unsigned *percent, unsigned *mv, int *ma,
    int *external, int *charging, int *full);
int
dreamlte_battery_read(unsigned *percent, unsigned *mv, int *ma,
    int *external, int *charging, int *full)
{
	uint16_t soc, vcell, cur;
	uint8_t reg, ok = 0, dtls = 0;
	if (!s8bat_i2c) {
		s8bat_i2c = ml_io_map(S8BAT_HSI2C_PA, 0x1000);
		if (!s8bat_i2c) {
			return -1;
		}
		s8i2c_reset(s8bat_i2c);
	}
	if (!s8bat_read16(S8BAT_FG, 0x06, &soc) || !s8bat_read16(S8BAT_FG, 0x09, &vcell) ||
	    !s8bat_read16(S8BAT_FG, 0x0a, &cur)) {
		return -1;
	}
	reg = 0xb2;
	(void)s8i2c_xfer(s8bat_i2c, S8BAT_CHG, &reg, 1, &ok, 1);
	reg = 0xb4;
	(void)s8i2c_xfer(s8bat_i2c, S8BAT_CHG, &reg, 1, &dtls, 1);
	unsigned pct = (unsigned)(soc >> 8) + ((soc & 0xffu) >= 128 ? 1 : 0);
	*percent = pct > 100 ? 100 : pct;
	*mv = (unsigned)((uint32_t)vcell * 78125u / 1000000u);
	*ma = (int)((int16_t)cur) * 15625 / 100000;
	*external = (ok & 0x40u) != 0;
	dtls &= 0x0f;
	*charging = *external && dtls <= 2;
	*full = *external && (dtls == 3 || dtls == 4);
	return (*mv >= 2800 && *mv <= 4600) ? 0 : -1;
}

static void
dreamlte_touch_start(void)
{
	uint8_t b[11] = {0};
	thread_t thread;
	int enabled = 1;
	(void)PE_parse_boot_argn("s8touch", &enabled, sizeof(enabled));
	(void)PE_parse_boot_argn("sweetfb_native", &s8ts_native, sizeof(s8ts_native));
	(void)PE_parse_boot_argn("sweetfb_scale", &s8ts_scale, sizeof(s8ts_scale));
	if (!enabled) {
		printf("S8 TOUCH: disabled\n");
		return;
	}
	s8ts_i2c = ml_io_map(S8TS_HSI2C_PA, 0x1000);
	s8ts_alive = ml_io_map(S8TS_ALIVE_PA, 0x1000);
	if (!s8ts_i2c || !s8ts_alive) {
		printf("S8 TOUCH: MMIO mapping failed\n");
		return;
	}
	s8i2c_reset(s8ts_i2c);
	if (!s8ts_cmd_read(0x55, b, 1) || b[0] != 0x20) {     /* BOOT_STATUS */
		printf("S8 TOUCH: no S6SY761 in application mode (status %#x)\n", b[0]);
		return;
	}
	if (s8ts_cmd_read(0x52, b, 3)) {                       /* DEVICE_ID */
		printf("S8 TOUCH: S6SY761 id %02x%02x%02x\n", b[0], b[1], b[2]);
	}
	uint8_t func[3] = {0x30, 0x01, 0x00};                  /* TOUCH_FUNCTION = touch */
	uint8_t sense = 0x10;                                   /* SENSE_ON */
	if (!s8i2c_xfer(s8ts_i2c, S8TS_ADDR, func, 3, NULL, 0) ||
	    !s8i2c_xfer(s8ts_i2c, S8TS_ADDR, &sense, 1, NULL, 0)) {
		printf("S8 TOUCH: enable sequence failed\n");
		return;
	}
	if (kernel_thread_start(s8ts_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("S8 TOUCH: thread start failed\n");
	}
}
#endif /* DREAMLTE */

