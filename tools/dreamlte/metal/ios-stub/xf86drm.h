/* Minimal libdrm declarations so Mesa's panfrost headers compile for iOS
 * (the S8 Metal plugin never opens a DRM device). */
#pragma once
#include <stdint.h>
#include <errno.h>
typedef struct _drmVersion {
	int version_major, version_minor, version_patchlevel;
	int name_len; char *name; int date_len; char *date; int desc_len; char *desc;
} drmVersion, *drmVersionPtr;
#define DRM_CLOEXEC 02000000
#define DRM_RDWR 2
static inline int drmIoctl(int fd, unsigned long request, void *arg) { (void)fd; (void)request; (void)arg; errno = ENODEV; return -1; }
static inline int drmPrimeHandleToFD(int fd, uint32_t h, uint32_t f, int *pfd) { (void)fd; (void)h; (void)f; (void)pfd; return -ENODEV; }
static inline int drmPrimeFDToHandle(int fd, int pfd, uint32_t *h) { (void)fd; (void)pfd; (void)h; return -ENODEV; }
static inline drmVersionPtr drmGetVersion(int fd) { (void)fd; return 0; }
static inline void drmFreeVersion(drmVersionPtr v) { (void)v; }
static inline int drmSyncobjCreate(int fd, uint32_t f, uint32_t *h) { (void)fd; (void)f; (void)h; return -ENODEV; }
