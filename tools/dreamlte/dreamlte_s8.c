/*
 * Galaxy S8 (DREAMLTE) BSD pieces, run once from bsd_init just before
 * launchd (tools/dreamlte/apply-dreamlte.py pastes this file into the xnu
 * tree as bsd/dev/dreamlte_s8.c):
 *
 * 1. /dev/s8mali, a cloning character device for the Mali-G71: each open
 *    gets its own minor (client); the ioctls (tools/dreamlte/s8mali_ioctl.h)
 *    are served by s8gpu_user_ioctl() in SweetFramebuffer.cpp, which owns
 *    the GPU. Closing frees the client's GPU memory.
 *
 * 2. The boot overlay: tools/dreamlte/mkoverlay.py packs files (our iOS
 *    programs) into an "S8OVL1" archive, tools/mkpayload.py --overlay puts
 *    it in kernel-owned RAM and names it in /chosen/memory-map "S8Overlay".
 *    Its directories and files are written to the root filesystem here, so
 *    a new program reaches the phone with the BOOT image alone.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/kauth.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm64/board_config.h>
#include <libkern/libkern.h>

#if defined(DREAMLTE)

extern int s8gpu_user_ioctl(int client, unsigned long cmd, void *data);
extern void s8gpu_user_close(int client);
extern int s8_kernel_mkdir(const char *path, int mode);
extern vm_offset_t ml_static_ptovirt(vm_offset_t paddr);

#define S8MALI_CLIENTS 16

static LCK_GRP_DECLARE(s8mali_grp, "s8mali");
static LCK_MTX_DECLARE(s8mali_mtx, &s8mali_grp);
static bool s8mali_open[S8MALI_CLIENTS];

static int
s8mali_clone(__unused dev_t dev, int action)
{
	int m = 0;
	if (action != DEVFS_CLONE_ALLOC) {
		return action == DEVFS_CLONE_FREE ? 0 : -1;
	}
	lck_mtx_lock(&s8mali_mtx);
	for (int i = 0; i < S8MALI_CLIENTS; i++) {
		if (!s8mali_open[i]) {
			m = i;
			break;
		}
	}
	lck_mtx_unlock(&s8mali_mtx);
	return m;       /* all busy: open() of minor 0 then fails with EBUSY */
}

static int
s8mali_dev_open(dev_t dev, __unused int flags, __unused int devtype, __unused proc_t p)
{
	int m = minor(dev), err = 0;
	if (m < 0 || m >= S8MALI_CLIENTS) {
		return ENXIO;
	}
	lck_mtx_lock(&s8mali_mtx);
	if (s8mali_open[m]) {
		err = EBUSY;
	} else {
		s8mali_open[m] = true;
	}
	lck_mtx_unlock(&s8mali_mtx);
	return err;
}

static int
s8mali_dev_close(dev_t dev, __unused int flags, __unused int devtype, __unused proc_t p)
{
	int m = minor(dev);
	if (m < 0 || m >= S8MALI_CLIENTS) {
		return ENXIO;
	}
	s8gpu_user_close(m);
	lck_mtx_lock(&s8mali_mtx);
	s8mali_open[m] = false;
	lck_mtx_unlock(&s8mali_mtx);
	return 0;
}

static int
s8mali_dev_ioctl(dev_t dev, u_long cmd, caddr_t data, __unused int flag, __unused proc_t p)
{
	return s8gpu_user_ioctl(minor(dev), cmd, data);
}

static const struct cdevsw s8mali_cdevsw = {
	.d_open = s8mali_dev_open, .d_close = s8mali_dev_close,
	.d_ioctl = s8mali_dev_ioctl,
	.d_read = eno_rdwrt, .d_write = eno_rdwrt, .d_stop = eno_stop,
	.d_reset = eno_reset, .d_ttys = NULL, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};

static void
s8mali_dev_init(void)
{
	int major = cdevsw_add(-1, &s8mali_cdevsw);
	if (major < 0) {
		printf("S8 MALI: cdevsw_add failed %d\n", major);
		return;
	}
	void *node = devfs_make_node_clone(makedev(major, 0), DEVFS_CHAR,
	    UID_ROOT, GID_WHEEL, 0666, s8mali_clone, "s8mali");
	printf("S8 MALI: /dev/s8mali major %d %s\n", major, node ? "ready" : "devfs failed");
}

/* ---- boot overlay ---- */

#define S8OVL_DIR  1
#define S8OVL_FILE 2

struct s8ovl_entry {
	uint32_t kind, mode, uid, gid;
	uint64_t size;
	uint32_t pathlen, pad;          /* path follows, NUL included, then data */
};

static int
s8ovl_owner(const char *path, const struct s8ovl_entry *e, vfs_context_t ctx)
{
	vnode_t vp = NULLVP;
	int err = vnode_lookup(path, 0, &vp, ctx);
	if (err != 0) {
		return err;
	}
	struct vnode_attr va;
	VATTR_INIT(&va);
	VATTR_SET(&va, va_uid, e->uid);
	VATTR_SET(&va, va_gid, e->gid);
	VATTR_SET(&va, va_mode, e->mode & 07777);
	err = vnode_setattr(vp, &va, ctx);
	vnode_put(vp);
	return err;
}

static int
s8ovl_write(const char *path, const struct s8ovl_entry *e, const uint8_t *data, vfs_context_t ctx)
{
	vnode_t vp = NULLVP;
	int err = vnode_open(path, O_CREAT | O_TRUNC | FWRITE | O_NOFOLLOW, (int)(e->mode & 07777),
	    0, &vp, ctx);
	if (err != 0) {
		return err;
	}
	int resid = 0;
	err = vn_rdwr(UIO_WRITE, vp, (caddr_t)(uintptr_t)data, (int)e->size, 0, UIO_SYSSPACE,
	    IO_UNIT | IO_SYNC, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
	if (err == 0 && resid != 0) {
		err = EIO;
	}
	vnode_close(vp, FWRITE, ctx);
	return err ? err : s8ovl_owner(path, e, ctx);
}

static void
s8ovl_install(void)
{
	DTEntry entry;
	void const *prop;
	unsigned int size;
	if (SecureDTLookupEntry(NULL, "/chosen/memory-map", &entry) != kSuccess ||
	    SecureDTGetProperty(entry, "S8Overlay", &prop, &size) != kSuccess || size < 16) {
		printf("S8 OVERLAY: none\n");
		return;
	}
	uint64_t range[2];
	memcpy(range, prop, sizeof(range));
	const uint8_t *b = (const uint8_t *)ml_static_ptovirt((vm_offset_t)range[0]);
	uint64_t len = range[1];
	if (b == NULL || len < 16 || memcmp(b, "S8OVL1\0\0", 8) != 0) {
		printf("S8 OVERLAY: bad archive at 0x%llx\n", range[0]);
		return;
	}
	uint32_t count;
	memcpy(&count, b + 8, 4);
	vfs_context_t ctx = vfs_context_kernel();
	uint64_t off = 16;
	unsigned written = 0, failed = 0;
	for (uint32_t i = 0; i < count; i++) {
		struct s8ovl_entry e;
		if (off + sizeof(e) > len) {
			break;
		}
		memcpy(&e, b + off, sizeof(e));
		off += sizeof(e);
		if (e.pathlen < 2 || e.pathlen > 1024 || off + e.pathlen > len ||
		    b[off + e.pathlen - 1] != 0 || b[off] != '/') {
			printf("S8 OVERLAY: bad entry %u\n", i);
			break;
		}
		const char *path = (const char *)(b + off);
		off = (off + e.pathlen + 7) & ~7ULL;
		int err;
		if (e.kind == S8OVL_DIR) {
			err = s8_kernel_mkdir(path, (int)(e.mode & 07777));
			if (err == 0 || err == EEXIST) {
				err = s8ovl_owner(path, &e, ctx);
			}
		} else if (e.kind == S8OVL_FILE && e.size <= len - off && e.size < (1ULL << 30)) {
			err = s8ovl_write(path, &e, b + off, ctx);
			off = (off + e.size + 7) & ~7ULL;
		} else {
			printf("S8 OVERLAY: bad entry %u (%s)\n", i, path);
			break;
		}
		if (err == 0) {
			written++;
		} else {
			failed++;
		}
		printf("S8 OVERLAY: %s %s -> %d\n", e.kind == S8OVL_DIR ? "dir " : "file", path, err);
	}
	printf("S8 OVERLAY: %u written, %u failed\n", written, failed);
}

void
dreamlte_bsd_late(void)
{
	s8mali_dev_init();
	s8ovl_install();
}

#else

void
dreamlte_bsd_late(void)
{
}

#endif /* DREAMLTE */
