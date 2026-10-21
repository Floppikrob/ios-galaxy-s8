#pragma once
#include <stdint.h>

int mngs_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *), int dbg);
void smp_probe(void (*kv)(const char *, uint64_t), void (*out)(const char *));
