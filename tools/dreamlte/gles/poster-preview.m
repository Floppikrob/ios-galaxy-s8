/* PosterBoard-only compatibility path for a still preview when Metal is absent.
 * Public UIKit bitmap APIs render this small preview; backboardd stays on GLES.
 * No Apple instructions or cached shader/source text is embedded here.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CGGeometry.h>
#include <dispatch/dispatch.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "poster-preview-bounds.h"

static id (*original_image)(id,SEL,id);
__attribute__((minsize)) static const char *text(id obj) {
    if(!obj)return "(nil)";
    return ((const char *(*)(id,SEL))objc_msgSend)(
        ((id(*)(id,SEL))objc_msgSend)(obj,sel_registerName("description")),sel_registerName("UTF8String"));
}
__attribute__((minsize)) static int responds(id obj,const char *name) {
    return obj && class_getInstanceMethod(object_getClass(obj),sel_registerName(name))!=NULL;
}
__attribute__((minsize)) static id call(id obj,const char *name) {
    return responds(obj,name)?((id(*)(id,SEL))objc_msgSend)(obj,sel_registerName(name)):nil;
}
__attribute__((minsize)) static id preview_image(id self,SEL sel,id view) {
    id (*metal_device)(void)=dlsym(RTLD_DEFAULT,"MTLCreateSystemDefaultDevice");
    id device=metal_device?metal_device():nil;
    if(device) {
        ((void(*)(id,SEL))objc_msgSend)(device,sel_registerName("release"));
        return original_image(self,sel,view);
    }
    static unsigned reports;
    int report=reports++<8,begun=0;
    void (*begin)(CGSize,BOOL,double)=dlsym(RTLD_DEFAULT,"UIGraphicsBeginImageContextWithOptions");
    void *(*context)(void)=dlsym(RTLD_DEFAULT,"UIGraphicsGetCurrentContext");
    id (*image)(void)=dlsym(RTLD_DEFAULT,"UIGraphicsGetImageFromCurrentImageContext");
    void (*end)(void)=dlsym(RTLD_DEFAULT,"UIGraphicsEndImageContext");
    void (*translate)(void*,double,double)=dlsym(RTLD_DEFAULT,"CGContextTranslateCTM");
    if(!begin||!context||!image||!end||!translate)return nil;
    id result=nil;
    @try {
        id layer=call(view,"layer");
        if(!responds(layer,"bounds")||!responds(layer,"contentsScale")||!responds(layer,"renderInContext:"))return nil;
        CGRect rect=((CGRect(*)(id,SEL))objc_msgSend)(layer,sel_registerName("bounds"));
        double scale=((double(*)(id,SEL))objc_msgSend)(layer,sel_registerName("contentsScale"));
        if(!dream_poster_preview_bounds(rect.origin.x,rect.origin.y,rect.size.width,rect.size.height,scale)) {
            if(report)fprintf(stderr,"DREAM POSTER PREVIEW: invalid bounds w=%.2f h=%.2f scale=%.2f\n",rect.size.width,rect.size.height,scale);
            return nil;
        }
        begin(rect.size,NO,scale);begun=1;
        void *ctx=context();
        if(ctx) {
            translate(ctx,-rect.origin.x,-rect.origin.y);
            ((void(*)(id,SEL,void*))objc_msgSend)(layer,sel_registerName("renderInContext:"),ctx);
            result=image();
        }
        if(report)fprintf(stderr,"DREAM POSTER PREVIEW: Metal=0 bitmap=%u w=%.2f h=%.2f scale=%.2f compositor-GPU-kept=1\n",result!=nil,rect.size.width,rect.size.height,scale);
    } @catch(id e) {
        fprintf(stderr,"DREAM POSTER PREVIEW: exception %.160s\n",text(e));
    } @finally {
        if(begun)end();
    }
    return result;
}

/* Refresh only the stock wallpaper frameworks. PosterKit owns the provider
 * extension point; its SDK record must exist before registering providers.
 * The native workspace completion has (BOOL success, NSError *error) ABI.
 */
__attribute__((minsize)) static void provider_diagnostic(void *unused);
__attribute__((minsize)) static void refresh_framework(void *context) {
    unsigned index=(unsigned)(uintptr_t)context;
    if(index==3){provider_diagnostic(NULL);return;}
    @autoreleasepool { @try {
        dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices",RTLD_NOW|RTLD_LOCAL);
        Class cls=objc_getClass("LSApplicationWorkspace");
        SEL sel=sel_registerName("rebuildDatabaseContentForFrameworkAtURL:completionHandler:");
        Method method=cls?class_getInstanceMethod(cls,sel):NULL;
        if(!method||strcmp(method_getTypeEncoding(method),"v32@0:8@16@?24"))return;
        id workspace=call((id)cls,"defaultWorkspace");
        if(!workspace)return;
        const char *paths[]={
            "/System/Library/PrivateFrameworks/PosterKit.framework",
            "/System/Library/PrivateFrameworks/WallpaperKit.framework",
            "/System/Library/PrivateFrameworks/GradientPoster.framework"
        };
        const char *ids[]={"com.apple.PosterKit","com.apple.WallpaperKit","com.apple.GradientPoster"};
        CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8*)paths[index],strlen(paths[index]),true);
        CFBundleRef bundle=url?CFBundleCreate(NULL,url):NULL;
        CFStringRef identity=bundle?CFBundleGetIdentifier(bundle):NULL;
        CFStringRef expected=CFStringCreateWithCString(NULL,ids[index],kCFStringEncodingUTF8);
        int valid=identity&&expected&&CFEqual(identity,expected);
        fprintf(stderr,"DREAM WALLPAPER PREP: %u %u\n",index,valid);
        if(valid) {
            ((void(*)(id,SEL,id,id))objc_msgSend)(workspace,sel,(id)url,(id)^(BOOL success,id error){
                fprintf(stderr,"DREAM WALLPAPER REFRESH: index=%u success=%u error=%.200s\n",index,success,text(error));
                if(success&&!error)dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW,0),(void*)(uintptr_t)(index+1),refresh_framework);
            });
        }
        if(expected)CFRelease(expected);
        if(bundle)CFRelease(bundle);
        if(url)CFRelease(url);
    } @catch(id e){fprintf(stderr,"DREAM WALLPAPER REFRESH: exception %.160s\n",text(e));} }
}

__attribute__((minsize)) static void provider_diagnostic(void *unused) {
    (void)unused;
    @autoreleasepool { @try {
        Class cls=objc_getClass("NSExtension");
        SEL sel=sel_registerName("extensionsWithMatchingAttributes:error:");
        Method method=cls?class_getClassMethod(cls,sel):NULL;
        if(!method || strcmp(method_getTypeEncoding(method),"@32@0:8@16^@24")) {
            fprintf(stderr,"DREAM WALLPAPER PROVIDERS: query-supported=0\n");
            return;
        }
        const void *key=CFSTR("NSExtensionPointName"),*value=CFSTR("com.apple.posterkit.provider");
        CFDictionaryRef attributes=CFDictionaryCreate(NULL,&key,&value,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        id error=nil;
        id extensions=((id(*)(id,SEL,id,id*))objc_msgSend)((id)cls,sel,(id)attributes,&error);
        CFRelease(attributes);
        unsigned long long count=responds(extensions,"count")?((unsigned long long(*)(id,SEL))objc_msgSend)(extensions,sel_registerName("count")):0;
        fprintf(stderr,"DREAM WALLPAPER PROVIDERS: result=%u count=%llu error=%.180s\n",extensions!=nil,count,text(error));
        /* One attempt per process; failed queries never trigger registration. */
        static int attempted;
        if(extensions && !count && !error && !attempted) {attempted=1;refresh_framework(NULL);}
    } @catch(id e) {fprintf(stderr,"DREAM WALLPAPER PROVIDERS: exception %.180s\n",text(e));} }
}

__attribute__((minsize)) int dream_poster_init(void) {
    char path[1024];uint32_t length=sizeof(path);
    if(_NSGetExecutablePath(path,&length)||strcmp(path,"/Applications/PosterBoard.app/PosterBoard"))return 0;
    const unsigned char expected[16]={0xfb,0xd3,0x2e,0xac,0xfb,0xbf,0x3f,0x56,0xa0,0xba,0x5b,0xd4,0x4d,0x2e,0x27,0x25};
    int installed=0;
    for(uint32_t i=0;i<_dyld_image_count();i++) {
        const char *name=_dyld_get_image_name(i);
        if(!name||!strstr(name,"/PosterBoard.framework/PosterBoard"))continue;
        const struct mach_header_64 *mh=(const void*)_dyld_get_image_header(i);
        intptr_t slide=_dyld_get_image_vmaddr_slide(i);
        if(mh->magic!=MH_MAGIC_64 || (uintptr_t)mh!=0x1c175b000ULL+slide || mh->sizeofcmds>65536)break;
        const unsigned char *p=(const void*)(mh+1);size_t at=0;int uuid=0;
        for(uint32_t n=0;n<mh->ncmds;n++) {
            if(at+8>mh->sizeofcmds)break;
            const struct load_command *c=(const void*)(p+at);
            if(c->cmdsize<8||c->cmdsize>mh->sizeofcmds-at)break;
            if(c->cmd==LC_UUID && c->cmdsize>=sizeof(struct uuid_command))uuid=!memcmp(((const struct uuid_command*)c)->uuid,expected,16);
            at+=c->cmdsize;
        }
        Method method=class_getInstanceMethod(objc_getClass("PBFHomeScreenPreviewImageBuilder"),sel_registerName("_imageForView:"));
        if(uuid && method && !strcmp(method_getTypeEncoding(method),"@24@0:8@16") && (uintptr_t)method_getImplementation(method)==0x1c17cc8fcULL+slide) {
            original_image=(void*)method_getImplementation(method);
            method_setImplementation(method,(IMP)preview_image);installed=1;
        }
        break;
    }
    fprintf(stderr,"DREAM POSTER HOOK: preview=%u guarded=1 compositor-GPU-kept=1\n",installed);
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW,0),NULL,provider_diagnostic);
    return 1; /* This process does not replace the compositor's EAGL methods. */
}
