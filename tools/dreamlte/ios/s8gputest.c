/*
 * Galaxy S8: first iOS program on the Mali-G71, through /dev/s8mali.
 * Installed by the boot overlay as /usr/appleinternal/sbin/launchd.s8 and
 * started as process 1 with boot-arg launchdsuffix=s8 (a development
 * kernel tries that path first). It runs its GPU checks, reports through
 * S8MALI_LOG (kernel log lines "S8 MALI| ..."), then execs the real
 * /sbin/launchd, which keeps pid 1. Nothing here may stop that exec.
 *
 * Checks: (1) a WRITE_VALUE job on slot 1 stores a word into GPU memory the
 * process mapped; (2) a fragment job on slot 0 clears a full-screen
 * 1440x2960 render target in that memory (descriptors as the loader's
 * proven gpu_draw, shim/dreamlte/gpu_probe.c); (3) 60 such frames timed.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "../s8mali_ioctl.h"

int open(const char *, int, ...);
int close(int);
int ioctl(int, unsigned long, ...);
int execve(const char *, char *const[], char *const[]);
int snprintf(char *, size_t, const char *, ...);

#define O_RDWR 2

#define W 1440u
#define H 2960u
#define STRIDE (W * 4u)
#define CLEAR 0xff3060c0u

static int fd = -1;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vsnprintf(char *, size_t, const char *, va_list);
static void say(const char *fmt, ...)
{
	struct s8mali_log l;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(l.text, sizeof(l.text), fmt, ap);
	va_end(ap);
	if (fd >= 0)
		ioctl(fd, S8MALI_LOG, &l);
}

static inline void w32(uint64_t base, unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)(base + off) = v;
}

static inline uint32_t r32(uint64_t base, unsigned off)
{
	return *(volatile uint32_t *)(uintptr_t)(base + off);
}

static void job_header(uint64_t va, unsigned type)
{
	for (unsigned i = 0; i < 0x40; i += 4)
		w32(va, i, 0);
	w32(va, 0x10, 1u | (type << 1) | (1u << 16));	/* 64-bit descriptors, type, index 1 */
}

static int submit(uint64_t jc, unsigned slot, struct s8mali_submit *s)
{
	__asm__ volatile("dsb sy" ::: "memory");
	s->jc = jc;
	s->slot = slot;
	s->timeout_ms = 1000;
	if (ioctl(fd, S8MALI_SUBMIT, s) != 0)
		return -1;
	return (int)s->result;
}

static void gpu_tests(void)
{
	struct s8mali_info info;
	if (ioctl(fd, S8MALI_INFO, &info) != 0) {
		say("INFO failed");
		return;
	}
	say("pid 1 on the Mali: gpu 0x%x shaders 0x%x ready %u heap %llu MiB free",
	    info.gpu_id, info.shader_present, info.ready, (unsigned long long)(info.heap_free >> 20));
	if (!info.ready)
		return;

	struct s8mali_alloc d = { .size = 0x10000 }, rt = { .size = (uint64_t)STRIDE * H };
	if (ioctl(fd, S8MALI_ALLOC, &d) != 0 || ioctl(fd, S8MALI_ALLOC, &rt) != 0) {
		say("ALLOC failed");
		return;
	}
	say("alloc desc gpu 0x%llx user 0x%llx, rt gpu 0x%llx user 0x%llx (%llu KiB)",
	    (unsigned long long)d.gpu_va, (unsigned long long)d.user_va,
	    (unsigned long long)rt.gpu_va, (unsigned long long)rt.user_va,
	    (unsigned long long)(rt.size >> 10));
	uint64_t u = d.user_va, g = d.gpu_va;

	/* (1) WRITE_VALUE on slot 1. */
	struct s8mali_submit s;
	const uint32_t magic = 0x5a5a1234u;
	w32(u, 0x100, 0xdeadbeefu);
	job_header(u, 2);
	w32(u, 0x20, (uint32_t)(g + 0x100));
	w32(u, 0x24, (uint32_t)((g + 0x100) >> 32));
	w32(u, 0x28, 6);				/* immediate 32 */
	w32(u, 0x30, magic);
	int r = submit(g, 1, &s);
	say("WRITE_VALUE slot1: result %d js 0x%x exc 0x%x value 0x%x %llu us -> %s",
	    r, s.js_status, r32(u, 0), r32(u, 0x100), (unsigned long long)s.elapsed_us,
	    r32(u, 0x100) == magic ? "USER GPU JOB WORKS" : "FAILED");
	if (r32(u, 0x100) != magic)
		return;

	/* (2) Full-screen clear: sample positions at 0x1000, FBD at 0x1100,
	 * render target at FBD + 128, fragment job at 0x2000. */
	unsigned sp = 0x1000, fbd = 0x1100, rtd = fbd + 128, job = 0x2000;
	for (unsigned i = sp; i < 0x2000; i += 4)
		w32(u, i, 0);
	w32(u, sp + 0x00, 128u | 128u << 16);
	w32(u, sp + 0x80, 128u | 128u << 16);
	w32(u, fbd + 4 * 4, (uint32_t)(g + sp));
	w32(u, fbd + 5 * 4, (uint32_t)((g + sp) >> 32));
	w32(u, fbd + 8 * 4, (W - 1) | (H - 1) << 16);
	w32(u, fbd + 10 * 4, (W - 1) | (H - 1) << 16);
	w32(u, fbd + 11 * 4, (8u << 9) | (0u << 19) | (1u << 24));
	w32(u, rtd + 0 * 4, 1u << 26);
	w32(u, rtd + 1 * 4, 1u | (19u << 3) | (2u << 10) | (0x688u << 16) | (1u << 31));
	w32(u, rtd + 8 * 4, (uint32_t)rt.gpu_va);
	w32(u, rtd + 9 * 4, (uint32_t)(rt.gpu_va >> 32));
	w32(u, rtd + 10 * 4, STRIDE);
	for (unsigned c = 0; c < 4; c++)
		w32(u, rtd + (12 + c) * 4, CLEAR);
	uint64_t total = 0, worst = 0;
	unsigned ok = 0;
	for (unsigned frame = 0; frame < 61; frame++) {
		job_header(u + job, 9);
		w32(u, job + 0x20, 0);
		w32(u, job + 0x24, (W / 16 - 1) | (H / 16 - 1) << 16);
		w32(u, job + 0x28, (uint32_t)(g + fbd) | 1u);	/* MFBD, 1 RT */
		w32(u, job + 0x2c, (uint32_t)((g + fbd) >> 32));
		r = submit(g + job, 0, &s);
		if (r != 0) {
			say("FRAGMENT frame %u: result %d js 0x%x exc 0x%x fault 0x%x at 0x%llx",
			    frame, r, s.js_status, r32(u, job), s.as_fault,
			    (unsigned long long)s.as_fault_addr);
			return;
		}
		if (frame == 0) {
			unsigned good = 0;
			for (unsigned y = 0; y < H; y += 37)
				for (unsigned x = 0; x < W; x += 37)
					good += r32(rt.user_va, y * STRIDE + x * 4) == CLEAR;
			unsigned want = ((H + 36) / 37) * ((W + 36) / 37);
			say("FRAGMENT 1440x2960 clear: %u/%u samples, %llu us -> %s", good, want,
			    (unsigned long long)s.elapsed_us,
			    good == want ? "USER GPU DRAW WORKS" : "FAILED");
			if (good != want)
				return;
			continue;
		}
		ok++;
		total += s.elapsed_us;
		if (s.elapsed_us > worst)
			worst = s.elapsed_us;
	}
	say("60 full-screen GPU frames: avg %llu us, worst %llu us (%llu fps if GPU-bound)",
	    (unsigned long long)(total / ok), (unsigned long long)worst,
	    (unsigned long long)(total ? 1000000ull * ok / total : 0));
}

int main(int argc, char **argv, char **envp)
{
	fd = open("/dev/s8mali", O_RDWR);
	if (fd >= 0) {
		gpu_tests();
		say("done, exec /sbin/launchd");
		close(fd);	/* frees the GPU memory */
	}
	/* The kernel's arguments, with launchd's own path as argv[0]. */
	char *args[16] = { "/sbin/launchd" };
	for (int i = 1; i < argc && i < 15 && argv; i++)
		args[i] = argv[i];
	execve("/sbin/launchd", args, envp);
	return 1;
}
