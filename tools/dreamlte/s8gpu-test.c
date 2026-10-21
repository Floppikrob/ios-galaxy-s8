/*
 * Galaxy S8: user-space test of the S8GPU service (xnu patch 0136), without
 * Mesa. Opens the service, reads GPU parameters, creates two buffer objects,
 * maps them, puts a WRITE_VALUE job (Mesa genxml v6 layout) into the first
 * that stores 0xCAFEBABE into the second, submits it and checks the value.
 * Every step is logged into the kernel log ("S8GPU client: ...") through the
 * service's selector 0x70, since a daemon's output goes nowhere here.
 *
 * Build: xcrun -sdk iphoneos clang -target arm64-apple-ios16.0 -O2 \
 *          -framework IOKit -framework CoreFoundation s8gpu-test.c -o s8gpu-test
 *        codesign -f -s - s8gpu-test
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>

static io_connect_t conn;

static void
say(const char *fmt, ...)
{
	char line[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", line);
	if (conn) {
		size_t out = 0;
		IOConnectCallStructMethod(conn, 0x70, line, strlen(line), NULL, &out);
	}
}

static kern_return_t
req(unsigned nr, void *data, size_t size)
{
	size_t out = size;
	return IOConnectCallStructMethod(conn, nr, data, size, data, &out);
}

struct create_bo { uint32_t size, flags, handle, pad; uint64_t offset; };
struct get_param { uint32_t param, pad; uint64_t value; };
struct submit { uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync; uint64_t bo_handles;
	        uint32_t bo_handle_count, requirements, jm_ctx_handle, pad; };

int
main(void)
{
	sleep(20);      /* let the boot settle */
	io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("S8GPU"));
	if (!svc || IOServiceOpen(svc, mach_task_self(), 0, &conn) != KERN_SUCCESS) {
		fprintf(stderr, "no S8GPU\n");
		return 1;
	}
	say("open ok");
	for (unsigned p = 0; p <= 14; p++) {
		struct get_param g = { p, 0, 0 };
		kern_return_t kr = req(4, &g, sizeof(g));
		say("param %u = %#llx (%#x)", p, (unsigned long long)g.value, kr);
	}
	struct create_bo job = { 4096, 0, 0, 0, 0 }, tgt = { 4096, 1, 0, 0, 0 };
	kern_return_t k1 = req(2, &job, sizeof(job)), k2 = req(2, &tgt, sizeof(tgt));
	say("create_bo: job handle %u va %#llx (%#x), target handle %u va %#llx (%#x)", job.handle,
	    (unsigned long long)job.offset, k1, tgt.handle, (unsigned long long)tgt.offset, k2);
	mach_vm_address_t ja = 0, ta = 0;
	mach_vm_size_t js = 0, ts = 0;
	k1 = IOConnectMapMemory64(conn, job.handle, mach_task_self(), &ja, &js, kIOMapAnywhere);
	k2 = IOConnectMapMemory64(conn, tgt.handle, mach_task_self(), &ta, &ts, kIOMapAnywhere);
	say("map: job %#llx+%#llx (%#x), target %#llx+%#llx (%#x)", (unsigned long long)ja, (unsigned long long)js,
	    k1, (unsigned long long)ta, (unsigned long long)ts, k2);
	if (k1 || k2) {
		return 1;
	}
	volatile uint32_t *j = (volatile uint32_t *)ja, *t = (volatile uint32_t *)ta;
	memset((void *)ja, 0, 64);
	j[4] = 1u | (2u << 1) | (1u << 16);     /* Is 64b, type Write value, index 1 */
	j[8] = (uint32_t)tgt.offset;
	j[9] = (uint32_t)(tgt.offset >> 32);
	j[10] = 6;                              /* Immediate 32 */
	j[12] = 0xcafebabe;
	t[0] = 0x22222222;
	__asm__ volatile ("dsb sy" ::: "memory");
	struct submit s = { 0 };
	s.jc = job.offset;
	kern_return_t kr = req(0, &s, sizeof(s));
	say("submit -> %#x; job header exception %#x, target %#x (%s)", kr, j[0], t[0],
	    t[0] == 0xcafebabe ? "GPU WROTE IT FROM USERSPACE" : "not written");
	return 0;
}
