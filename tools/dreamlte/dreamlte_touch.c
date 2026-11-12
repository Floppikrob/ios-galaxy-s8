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
#define S8TS_ADDR               0x48u  /* dreamlte S6SY761 */
#define S8BAT_FG                0x36u
#define S8BAT_CHG               0x69u
#define S8TS_FINGERS            10
#define S8_I2C_CLKIN            26000000u
#define S8_I2C_SCL_HZ           400000u

static vm_offset_t s8ts_i2c, s8ts_alive, s8bat_i2c;
static int s8ts_sleep;
static int s8fts_stopwait; /* Samsung scl-clk-stretching final STOP check */
static int s8fts_prefill; /* optional FTS TX FIFO staging before AUTO_RUN */
static int s8ts_native;         /* sweetfb_native: iOS sees the whole panel */
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

/* Samsung Exynos8895 with samsung,scl-clk-stretching waits for the
 * final controller state to become idle before disabling channels.
 * TRANSFER_DONE alone can be observed while state=0x0002000d.
 * The check runs only for FTS when the diagnostic boot arg is enabled.
 */
static bool
s8fts_finish_stop(vm_offset_t bus)
{
    static unsigned traces, failures;
    uint32_t initial = a5touch_read(bus, 0x50);
    uint32_t state = initial, fifo = 0;
    unsigned us;
    for (us = 0; us < 100000; us++) {
        state = a5touch_read(bus, 0x50);
        fifo = a5touch_read(bus, 0x30);
        if (!(state & 0x0fu) && (fifo & (1u << 8))) break;
        delay(1);
    }
    bool ok = us < 100000;
    bool log = traces++ < 16;
    if (!ok) log = ++failures <= 8 || !(failures & 127u);
    if (log) {
        printf("FTS TOUCH: STOP complete=%u waited_us=%u state=%08x->%08x fifo=%08x\n",
            (unsigned)ok, us, initial, state, fifo);
    }
    return ok;
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
		bool prefill = s8fts_prefill && addr == 0x49u && wlen <= 64;
		if (!prefill) {
			a5touch_write(bus, 0x44, (rlen ? 0 : (1u << 17)) | wlen | (1u << 31));
		}
		for (unsigned i = 0; i < wlen; i++) {
			for (unsigned n = 0; n < 1000 && (a5touch_read(bus, 0x30) & (1u << 7)); n++) {
				delay(1);
			}
			a5touch_write(bus, 0x34, w[i]);
		}
		if (prefill) {
			static unsigned traces;
			uint32_t fifo = a5touch_read(bus, 0x30);
			unsigned queued = fifo & 0x7fu;
			if (traces++ < 16 || queued != wlen) {
				printf("FTS TOUCH: TX prefill cmd=%02x expected=%u queued=%u fifo=%08x\n",
				    w[0], wlen, queued, fifo);
			}
			if (queued != wlen) goto fail;
			a5touch_write(bus, 0x24, a5touch_read(bus, 0x24));
			a5touch_write(bus, 0x44, (rlen ? 0 : (1u << 17)) | wlen | (1u << 31));
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
	if (s8fts_stopwait && addr == 0x49u && (wlen || rlen) &&
	    !s8fts_finish_stop(bus)) {
		goto fail;
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
	if (s8ts_native) {
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

/*
 * Galaxy S8+ (dream2lte): STMicroelectronics FTS 36 70 (I2C 0x49).
 * Separate from the S8 S6SY761 path. Enable with s8touch=2.
 * This is an experimental polling driver (no XNU IRQ registration yet).
 * Linux: stmfts.c + Samsung fts8cd56_dream2. No firmware/NVM writes.
 */
#define S8FTS_ADDR                 0x49u
#define S8FTS_CMD_READ_EVENT       0x85u
#define S8FTS_CMD_SLEEP_OUT        0x91u
#define S8FTS_CMD_SENSE_ON         0x93u
#define S8FTS_CMD_CLEAR_EVENTS     0xa1u
#define S8FTS_CMD_PRESSURE_ON     0x9cu

/* Optional protocol isolation: complete STOP before the response read. */
static int s8fts_split;

static bool
s8fts_wait_idle(void)
{
    for (unsigned us = 0; us < 20000; us++) {
        if (!(a5touch_read(s8ts_i2c, 0x50) & (1u << 17))) return true;
        delay(1);
    }
    return false;
}

/* Diagnostic transport isolation: the event reader previously returned the
 * chip-ID response on every poll. Reset only the touch HSI2C controller
 * before each complete FTS transaction, retaining the repeated START.
 * This does not reset the FTS chip or write firmware/calibration/NVM.
 */
static bool
s8fts_xfer(const uint8_t *w, unsigned wlen, uint8_t *r, unsigned rlen)
{
    static unsigned traces;
    bool trace = traces++ < 16;
    uint32_t before = a5touch_read(s8ts_i2c, 0x30);
    s8i2c_reset(s8ts_i2c);
    uint32_t cleared = a5touch_read(s8ts_i2c, 0x30);
    bool ok;
    if (s8fts_split && wlen && rlen) {
        ok = s8i2c_xfer(s8ts_i2c, S8FTS_ADDR, w, wlen, NULL, 0);
        if (ok) ok = s8fts_wait_idle();
        if (ok) {
            delay(100);
            ok = s8i2c_xfer(s8ts_i2c, S8FTS_ADDR, NULL, 0, r, rlen);
            if (ok) ok = s8fts_wait_idle();
        }
        if (!ok) s8i2c_reset(s8ts_i2c);
    } else {
        ok = s8i2c_xfer(s8ts_i2c, S8FTS_ADDR, w, wlen, r, rlen);
    }
    if (trace) {
        printf("FTS TOUCH: xfer reset cmd=%02x w=%u r=%u ok=%u fifo=%08x/%08x/%08x state=%08x\n",
            wlen ? w[0] : 0, wlen, rlen, (unsigned)ok, before, cleared,
            a5touch_read(s8ts_i2c, 0x30), a5touch_read(s8ts_i2c, 0x50));
    }
    return ok;
}

static bool
s8fts_command(uint8_t cmd)
{
	return s8fts_xfer(&cmd, 1, NULL, 0);
}

static bool
s8fts_read_event(uint8_t ev[8])
{
	uint8_t cmd = S8FTS_CMD_READ_EVENT;
	return s8fts_xfer(&cmd, 1, ev, 8);
}

static void
s8fts_report(const uint8_t ev[8])
{
	unsigned kind = ev[0] & 0x0fu;    /* enter=3, leave=4, move=5 */
	unsigned tid = ev[0] >> 4;
	if (tid >= S8TS_FINGERS || (kind != 3 && kind != 4 && kind != 5)) {
		return;
	}
	if (kind == 4) {  /* use last known coordinates on finger release */
		if (s8ts_down[tid]) {
			s8ts_down[tid] = false;
			if (tid < 5) {
				sweet_hid_touch((int)tid, 0, s8ts_x[tid], s8ts_y[tid]);
			}
			printf("FTS TOUCH: up id=%u\n", tid);
		}
		return;
	}

	/* ST FTS event: slot[0] high nibble, X in 1 + low(2), Y in high(2) + 3. */
	unsigned rawx = (unsigned)ev[1] | (((unsigned)ev[2] & 0x0fu) << 8);
	unsigned rawy = ((unsigned)ev[2] >> 4) | ((unsigned)ev[3] << 4);
	if (rawx > 1439) rawx = 1439;
	if (rawy > 2959) rawy = 2959;
	unsigned x, y;
	if (s8ts_native) {
		/* SweetHIDTouch uses 1080 x 1920 even with native framebuffer. */
		x = rawx * 1080u / 1440u;
		y = rawy * 1920u / 2960u;
	} else {
		int px = (int)rawx - (1440 - 1125) / 2;
		int py = (int)rawy - (2960 - 2436) / 2;
		px = px < 0 ? 0 : (px > 1124 ? 1124 : px);
		py = py < 0 ? 0 : (py > 2435 ? 2435 : py);
		x = (unsigned)px * 1080u / 1125u;
		y = (unsigned)py * 1920u / 2436u;
	}
	if (!s8ts_down[tid]) {
		printf("FTS TOUCH: down id=%u x=%u y=%u\n", tid, x, y);
	}
	s8ts_down[tid] = true;
	s8ts_x[tid] = (uint16_t)x;
	s8ts_y[tid] = (uint16_t)y;
	if (tid < 5) {
		sweet_hid_touch((int)tid, 1, (int)x, (int)y);
	}
}

static void
s8fts_thread(void *arg, wait_result_t result)
{
	uint8_t ev[8];
	unsigned errors = 0;
	(void)arg;
	(void)result;
	printf("FTS TOUCH: polling worker started\n");
	for (;;) {
		/* Polling is intentional: the S8+ interrupt GPIO is not yet routed
		 * through XNU. Read at most 16 events per iteration to avoid hogging CPU. */
		for (unsigned i = 0; i < 16; i++) {
			if (!s8fts_read_event(ev)) {
				if (++errors <= 6 || !(errors & 127u)) {
					printf("FTS TOUCH: I2C event read failed (%u)\n", errors);
				}
				break;
			}
			errors = 0;
			static unsigned idle_polls, data_logs;
			unsigned any = 0;
			for (unsigned j = 0; j < 8; j++) any |= ev[j];
			bool log_event = any ? (data_logs++ < 80) :
			    (idle_polls++ < 3 || (idle_polls & 2047u) == 0);
			if (log_event) {
			    printf("FTS TOUCH: event raw %02x %02x %02x %02x %02x %02x %02x %02x\n",
			        ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
			}
			if (ev[0] == 0) break; /* no queued events */
			s8fts_report(ev);
		}
		s8ts_wait(errors ? 40 : 8);
	}
}

/* Samsung FTS8CD56 reinit: volatile software reset, wait for 0x10,
 * flush, sensing, pressure sensing, then controller interrupt enable.
 * Optional normal force calibration is selected separately; no firmware
 * upload, flash, or NVM-save commands are issued.
 */
static bool
s8fts_reset_ready(void)
{
    const uint8_t reset[4] = {0xb6, 0x00, 0x28, 0x80};
    uint8_t ev[8];
    if (!s8fts_xfer(reset, sizeof(reset), NULL, 0)) {
        printf("FTS TOUCH: controller reset write failed\n");
        return false;
    }
    delay(10000);
    for (unsigned n = 0; n < 50; n++) {
        if (!s8fts_read_event(ev)) {
            printf("FTS TOUCH: ready read failed at %u\n", n);
            return false;
        }
        if (n < 8 || ev[0] == 0x10 || ev[0] == 0x0f) {
            printf("FTS TOUCH: ready raw %02x %02x %02x %02x %02x %02x %02x %02x\n",
                ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
        }
        if (ev[0] == 0x10) {
            printf("FTS TOUCH: controller ready confirmed\n");
            return true;
        }
        delay(20000);
    }
    printf("FTS TOUCH: controller ready timeout; sensing not enabled\n");
    return false;
}

static void
s8fts_start(void)
{
	thread_t thread;
	uint8_t b[7] = {0};
	uint8_t id_cmd[3] = {0xb6, 0x00, 0x04};
	bool found = false;
	int reinit = 0, calibrate = 0;
	(void)PE_parse_boot_argn("s8fts_calibrate", &calibrate, sizeof(calibrate));
	(void)PE_parse_boot_argn("s8fts_stopwait", &s8fts_stopwait, sizeof(s8fts_stopwait));
	(void)PE_parse_boot_argn("s8fts_prefill", &s8fts_prefill, sizeof(s8fts_prefill));
	(void)PE_parse_boot_argn("s8fts_reinit", &reinit, sizeof(reinit));
	(void)PE_parse_boot_argn("s8fts_split", &s8fts_split, sizeof(s8fts_split));
	printf("FTS TOUCH: transaction mode=%s\n", s8fts_split ? "STOP-separated" : "repeated-START");
	if (reinit && !s8fts_reset_ready()) return;
	/* Exynos S2MPS17 LDO34/LDO35 were enabled by the dream2lte loader.
	 * Probe the IC without writing firmware, calibration data or NVM. */
	for (unsigned attempt = 0; attempt < 5; attempt++) {
		if (s8fts_xfer(id_cmd, 3, b, 7) &&
		    b[1] == 0x36 && b[2] == 0x70) {
			found = true;
			break;
		}
		delay(20000);
	}
	if (!found) {
		printf("FTS TOUCH: chip ID probe failed (raw %02x %02x %02x %02x %02x %02x %02x)\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6]);
		return;
	}
	printf("FTS TOUCH: detected 36 70, fw=%02x%02x\n", b[6], b[5]);
	/* Legacy wake, flush, sensing; 0x9c is pressure sensing, not IRQ enable. */
	if (!reinit && !s8fts_command(S8FTS_CMD_SLEEP_OUT)) {
		printf("FTS TOUCH: sleep-out command failed\n");
		return;
	}
	delay(50000);
	bool enabled = s8fts_command(S8FTS_CMD_CLEAR_EVENTS);
	if (reinit) delay(10000);
	enabled = enabled && s8fts_command(S8FTS_CMD_SENSE_ON) &&
	    s8fts_command(S8FTS_CMD_PRESSURE_ON);
	if (!enabled) {
		printf("FTS TOUCH: enable sequence failed\n");
		return;
	}
	if (reinit) {
		const uint8_t irq_on[4] = {0xb6, 0x00, 0x2c, 0x48};
		delay(50000);
		if (calibrate) {
			printf("FTS TOUCH: force calibration start (A2, no NVM save)\n");
			if (!s8fts_command(0xa2)) {
				printf("FTS TOUCH: force calibration command failed\n");
				return;
			}
			/* Same-controller Linux port waits 1000 ms before enabling IRQ. */
			delay(1000000);
			printf("FTS TOUCH: force calibration settle complete\n");
		}
		if (!s8fts_xfer(irq_on, sizeof(irq_on), NULL, 0)) {
			printf("FTS TOUCH: interrupt enable failed\n");
			return;
		}
		printf("FTS TOUCH: Samsung reinit sequence complete\n");
	}
	if (kernel_thread_start(s8fts_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("FTS TOUCH: worker thread failed\n");
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
	if (enabled == 2) {
		printf("FTS TOUCH: selecting dream2lte FTS driver\n");
		s8fts_start();
		return;
	}
	if (enabled != 1) {
		printf("S8 TOUCH: invalid s8touch mode %d\n", enabled);
		return;
	}
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

