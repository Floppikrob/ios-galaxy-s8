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

__attribute__((minsize)) static void provider_diagnostic(void *unused) {
    (void)unused;
    @autoreleasepool { @try {
        Class cls=objc_getClass("NSExtension");
        SEL sel=sel_registerName("extensionsWithMatchingAttributes:error:");
        Method method=cls?class_getClassMethod(cls,sel):NULL;
        if(!method || strcmp(method_getTypeEncoding(method),"@32@0:8@16^@24")) {
            fprintf(stderr,"DREAM WALLPAPER PROVIDERS: query-supported=0 type=%.64s\n",method?method_getTypeEncoding(method):"nil");
            return;
        }
        const void *key=CFSTR("NSExtensionPointName"),*value=CFSTR("com.apple.posterkit.provider");
        CFDictionaryRef attributes=CFDictionaryCreate(NULL,&key,&value,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        id error=nil;
        id extensions=((id(*)(id,SEL,id,id*))objc_msgSend)((id)cls,sel,(id)attributes,&error);
        CFRelease(attributes);
        unsigned long long count=responds(extensions,"count")?((unsigned long long(*)(id,SEL))objc_msgSend)(extensions,sel_registerName("count")):0;
        fprintf(stderr,"DREAM WALLPAPER PROVIDERS: query-supported=1 result=%u count=%llu error=%.180s\n",extensions!=nil,count,text(error));
        for(unsigned long long i=0;i<count && i<8;i++) {
            id extension=((id(*)(id,SEL,unsigned long long))objc_msgSend)(extensions,sel_registerName("objectAtIndex:"),i);
            fprintf(stderr,"DREAM WALLPAPER PROVIDER: index=%llu id=%.120s\n",i,text(call(extension,"identifier")));
        }
    } @catch(id e) {fprintf(stderr,"DREAM WALLPAPER PROVIDERS: exception %.180s\n",text(e));} }
}

__attribute__((minsize)) static void catalog_diagnostic(void *unused) {
    (void)unused;
    @try {
        int class_before=objc_getClass("WKWallpaperRepresentingCollectionsManager")!=Nil;
        dlerror();
        void *wallpaper=dlopen("/System/Library/PrivateFrameworks/WallpaperKit.framework/WallpaperKit",RTLD_NOW|RTLD_LOCAL);
        const char *load_error=wallpaper?NULL:dlerror();
        Class cls=objc_getClass("WKWallpaperRepresentingCollectionsManager");
        fprintf(stderr,"DREAM WALLPAPER FRAMEWORK: loaded=%u class-before=%u class-after=%u default-selector=%u error=%.200s\n",wallpaper!=NULL,class_before,cls!=Nil,responds((id)cls,"defaultManager"),load_error?load_error:"none");
        id manager=call((id)cls,"defaultManager");
        unsigned long long count=responds(manager,"numberOfWallpaperCollections")?((unsigned long long(*)(id,SEL))objc_msgSend)(manager,sel_registerName("numberOfWallpaperCollections")):0;
        fprintf(stderr,"DREAM WALLPAPER CATALOG: manager=%u collections=%llu\n",manager!=nil,count);
        id defaults=call((id)objc_getClass("WKDefaultWallpaperManager"),"defaultWallpaperManager");
        id bundle=call(defaults,"defaultWallpaperBundle");
        fprintf(stderr,"DREAM WALLPAPER DEFAULT: manager=%u bundle=%u class=%s\n",defaults!=nil,bundle!=nil,bundle?object_getClassName(bundle):"nil");
    } @catch(id e) {
        fprintf(stderr,"DREAM WALLPAPER CATALOG: exception %.200s\n",text(e));
    }
    dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW,0),NULL,provider_diagnostic);
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
    dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),NULL,catalog_diagnostic);
    return 1; /* This process does not replace the compositor's EAGL methods. */
}
