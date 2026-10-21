/*
 * /dev/s8mali: user access to the Galaxy S8 Mali-G71 (kernel side in
 * tools/dreamlte/s8_gpu_present.inc and s8_mali_dev.c, both pasted into the
 * xnu tree by apply-dreamlte.py). Shared by the kernel and iOS programs.
 *
 * Memory: S8MALI_ALLOC hands out physically contiguous GPU memory from the
 * RAM above XNU's 512 MiB window. The GPU sees it at gpu_va (= its physical
 * address, mapped 1:1 in the Mali page tables); the calling process sees the
 * same bytes at user_va, mapped write-combined (uncached), so neither side
 * needs cache maintenance. Allocations are freed by S8MALI_FREE or when the
 * descriptor is closed.
 *
 * Surfaces: S8MALI_MAP_SURFACE maps an IOSurface's pages into one of the
 * kernel's GPU windows (least recently mapped is reused) so job chains can
 * sample or render to it. A window stays valid until 16 other surfaces
 * have been mapped; map again before each command buffer that uses it.
 *
 * Jobs: S8MALI_SUBMIT starts a job chain (GPU address of the first job
 * header) on a job slot -- 0 fragment, 1 vertex/tiler/compute -- and waits
 * for it. The kernel serialises it with the frame presentation it does
 * itself on slot 0.
 */
#ifndef S8MALI_IOCTL_H
#define S8MALI_IOCTL_H

#include <stdint.h>
#include <sys/ioccom.h>

struct s8mali_info {
	uint32_t gpu_id;                /* GPU_ID, 0x60a00001 on the S8 */
	uint32_t shader_present;        /* SHADER_PRESENT low word */
	uint32_t tiler_features;        /* TILER_FEATURES */
	uint32_t ready;                 /* 1: powered, AS0 on */
	uint64_t heap_base, heap_size;  /* GPU memory pool (physical) */
	uint64_t heap_free;
};

struct s8mali_alloc {
	uint64_t size;                  /* in: bytes (rounded to 64 KiB) */
	uint64_t gpu_va;                /* out */
	uint64_t user_va;               /* out */
};

struct s8mali_free {
	uint64_t gpu_va;
};

struct s8mali_submit {
	uint64_t jc;                    /* in: GPU address of the first job */
	uint32_t slot;                  /* in: 0 fragment, 1 vertex/tiler/compute */
	uint32_t timeout_ms;            /* in: 0 = 1000, max 5000 */
	uint32_t result;                /* out: 0 done, 1 job fault, 2 timeout, 3 busy/off */
	uint32_t js_status;             /* out: JS_STATUS (0x01 done, 0x4x/0x5x faults) */
	uint32_t irq_rawstat;           /* out: JOB_IRQ_RAWSTAT */
	uint32_t as_fault;              /* out: AS0_FAULTSTATUS */
	uint64_t as_fault_addr;         /* out: AS0_FAULTADDRESS */
	uint64_t elapsed_us;            /* out */
};

struct s8mali_map_surface {
	uint32_t id;                    /* in: IOSurface id */
	uint32_t flags;                 /* in: S8MALI_MAP_* */
	uint64_t offset, size;          /* in: byte range (size 0 = whole surface) */
	uint64_t gpu_va;                /* out: GPU address of byte `offset` */
	uint32_t width, height;         /* out: surface geometry */
	uint32_t bytes_per_row, pixel_format;
};
#define S8MALI_MAP_FLUSH        1       /* clean+invalidate the CPU caches first */

struct s8mali_log {
	char text[120];                 /* NUL-terminated; printed as "S8 MALI| ..." */
};

#define S8MALI_INFO     _IOR('M', 1, struct s8mali_info)
#define S8MALI_ALLOC    _IOWR('M', 2, struct s8mali_alloc)
#define S8MALI_FREE     _IOW('M', 3, struct s8mali_free)
#define S8MALI_SUBMIT   _IOWR('M', 4, struct s8mali_submit)
#define S8MALI_LOG      _IOW('M', 5, struct s8mali_log)
#define S8MALI_MAP_SURFACE _IOWR('M', 6, struct s8mali_map_surface)

#endif
