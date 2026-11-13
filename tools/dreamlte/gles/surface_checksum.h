/* Pixel change detection only: fingerprints are never persisted or used for security. */
#ifndef HAKIOS_SURFACE_CHECKSUM_H
#define HAKIOS_SURFACE_CHECKSUM_H
#include <stddef.h>
#include <stdint.h>
uint64_t hakios_surface_checksum(const void *pixels, size_t bytes);
#endif
