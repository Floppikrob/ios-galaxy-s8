#pragma once
#include <stdint.h>

/* Powers pd-g3d and reads the Mali ID; 1 when a Mali-G71 answers. */
int gpu_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *));
