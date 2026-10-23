/*
 * S8MaliMetal C helpers (s8mtl_core.m), shared by the Objective-C objects
 * and the C draw path (s8mtl_draw.c).
 */
#pragma once
#include <stdint.h>

void s8_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int s8_fd(void);
/* GPU memory: write-combined CPU mapping + GPU address (VA = PA). Small
 * blocks come from shared slabs, large ones are their own allocation. */
typedef struct {
	uint64_t gpu;
	void *cpu;
	uint64_t size;
	int big;
} s8_mem;
int s8_mem_alloc(s8_mem *m, uint64_t size, uint64_t align);
void s8_mem_free(s8_mem *m);
uint64_t s8_now_ns(void);
