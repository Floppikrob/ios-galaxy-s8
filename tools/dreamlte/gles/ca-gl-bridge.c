/* Narrow equivalent of donor inject.c's function_uniform/function_supported
 * forwarding. See PROVENANCE.md. Only private COW vtable data is changed;
 * framework instructions and the original support decision stay intact.
 */
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern void hakios_gl_function_context(void *);
extern void hakios_gl_function_values(void *, unsigned, unsigned, size_t, size_t, const float *);
extern void hakios_gl_function_refused(unsigned);

static int (*dream_ca_original_supported)(void *);
static void (*dream_ca_original_uniform)(void *, unsigned, size_t, size_t, const float *);
static unsigned dream_ca_function(void *context)
{
    void *state = NULL;
    if (!context) return 90;
    memcpy(&state, (const char *)context + 16, sizeof(state));
    return state ? *((const uint8_t *)state + 16) : 90;
}
static int dream_ca_supported(void *context)
{
    hakios_gl_function_context(context);
    int result = dream_ca_original_supported(context);
    if (!result) hakios_gl_function_refused(dream_ca_function(context));
    return result;
}
static void dream_ca_uniform(void *context, unsigned index, size_t components, size_t count, const float *values)
{
    hakios_gl_function_values(context, dream_ca_function(context), index, components, count, values);
    dream_ca_original_uniform(context, index, components, count, values);
}

/* Caller has already checked image identity and slot range. Fail closed on a
 * changed table. Originals are published before either callback can run. */
static bool dream_ca_patch_slots(uintptr_t *supported, uintptr_t *uniform,
    uintptr_t expectedSupported, uintptr_t expectedUniform)
{
    uintptr_t page = (uintptr_t)supported & ~(uintptr_t)(vm_page_size - 1);
    if (!vm_page_size || (vm_page_size & (vm_page_size - 1)) ||
        ((uintptr_t)uniform & ~(uintptr_t)(vm_page_size - 1)) != page ||
        *supported != expectedSupported || *uniform != expectedUniform) return false;
    if (vm_protect(mach_task_self(), page, vm_page_size, FALSE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return false;
    if (*supported != expectedSupported || *uniform != expectedUniform) {
        vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
        return false;
    }
    dream_ca_original_supported = (int (*)(void *))*supported;
    dream_ca_original_uniform = (void (*)(void *, unsigned, size_t, size_t, const float *))*uniform;
    __atomic_store_n(supported, (uintptr_t)dream_ca_supported, __ATOMIC_RELEASE);
    __atomic_store_n(uniform, (uintptr_t)dream_ca_uniform, __ATOMIC_RELEASE);
    (void)vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
    return true;
}

void dream_ca_gl_hooks(void)
{
    static const uint8_t expectedUuid[16] = {
        0x9e,0x40,0xb2,0xeb,0x26,0x0c,0x3a,0xf1,0xae,0x45,0xa1,0xa5,0x51,0xa8,0xc1,0xb9
    };
    bool installed = false;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *name = _dyld_get_image_name(i);
        if (!name || !strstr(name, "/QuartzCore.framework/QuartzCore")) continue;
        const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!header || header->magic != MH_MAGIC_64 || header->ncmds > 256 || header->sizeofcmds > 65536) break;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        if ((uintptr_t)header != 0x188284000ULL + slide) break;
        const uint8_t *commands = (const uint8_t *)(header + 1);
        size_t at = 0;
        bool uuidMatches = false, slotsMapped = false;
        uintptr_t supported = 0x1d5b2fa70ULL + slide, uniform = 0x1d5b2fa80ULL + slide;
        for (uint32_t n = 0; n < header->ncmds; ++n) {
            if (at + sizeof(struct load_command) > header->sizeofcmds) break;
            const struct load_command *cmd = (const struct load_command *)(commands + at);
            if (cmd->cmdsize < sizeof(*cmd) || cmd->cmdsize > header->sizeofcmds - at) break;
            if (cmd->cmd == LC_UUID && cmd->cmdsize >= sizeof(struct uuid_command))
                uuidMatches = !memcmp(((const struct uuid_command *)cmd)->uuid, expectedUuid, 16);
            if (cmd->cmd == LC_SEGMENT_64 && cmd->cmdsize >= sizeof(struct segment_command_64)) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
                uintptr_t begin = seg->vmaddr + slide;
                if ((seg->initprot & VM_PROT_READ) && !(seg->initprot & VM_PROT_EXECUTE) &&
                    supported >= begin && uniform >= supported && seg->vmsize >= 8 &&
                    uniform - begin <= seg->vmsize - 8) slotsMapped = true;
            }
            at += cmd->cmdsize;
        }
        if (uuidMatches && slotsMapped)
            installed = dream_ca_patch_slots((uintptr_t *)supported, (uintptr_t *)uniform,
                0x1884b2164ULL + slide, 0x1884b21acULL + slide);
        break;
    }
    const char *message = installed ? "DREAM CA GL: function context/uniform bridge installed; private table; original support kept\n" :
        "DREAM CA GL: identity/table/protection check rejected bridge; framework unchanged\n";
    write(2, message, strlen(message));
}
