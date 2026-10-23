/*
 * Metal declarations for the S8 Mali plugin (no iOS SDK on the build host).
 * Types follow Apple's public Metal headers; the private base classes are
 * declared from the iOS 16.7 Metal.framework Objective-C metadata (only
 * the methods the plugin calls or overrides).
 */
#pragma once
#import "objc_min.h"

typedef struct { NSUInteger x, y, z; } MTLOrigin;
typedef struct { NSUInteger width, height, depth; } MTLSize;
typedef struct { MTLOrigin origin; MTLSize size; } MTLRegion;
typedef struct { double originX, originY, width, height, znear, zfar; } MTLViewport;
typedef struct { NSUInteger x, y, width, height; } MTLScissorRect;
typedef struct { double red, green, blue, alpha; } MTLClearColor;
typedef struct { NSUInteger location, length; } NSRange;

/* MTLPixelFormat values used by CoreAnimation surfaces. */
enum {
	MTLPixelFormatInvalid = 0,
	MTLPixelFormatA8Unorm = 1,
	MTLPixelFormatR8Unorm = 10,
	MTLPixelFormatRG8Unorm = 30,
	MTLPixelFormatR16Float = 25,
	MTLPixelFormatRGBA8Unorm = 70,
	MTLPixelFormatRGBA8Unorm_sRGB = 71,
	MTLPixelFormatBGRA8Unorm = 80,
	MTLPixelFormatBGRA8Unorm_sRGB = 81,
	MTLPixelFormatRGB10A2Unorm = 90,
	MTLPixelFormatBGR10A2Unorm = 94,
	MTLPixelFormatRGBA16Float = 115,
	MTLPixelFormatBGR10_XR = 554,
	MTLPixelFormatBGRA10_XR = 552,
	MTLPixelFormatDepth32Float = 252,
	MTLPixelFormatStencil8 = 253,
};

enum { MTLLoadActionDontCare = 0, MTLLoadActionLoad = 1, MTLLoadActionClear = 2 };
enum { MTLStoreActionDontCare = 0, MTLStoreActionStore = 1 };
enum { MTLPrimitiveTypePoint = 0, MTLPrimitiveTypeLine = 1, MTLPrimitiveTypeLineStrip = 2,
       MTLPrimitiveTypeTriangle = 3, MTLPrimitiveTypeTriangleStrip = 4 };
enum { MTLFunctionTypeVertex = 1, MTLFunctionTypeFragment = 2, MTLFunctionTypeKernel = 3 };
enum { MTLStorageModeShared = 0, MTLStorageModeManaged = 1, MTLStorageModePrivate = 2,
       MTLStorageModeMemoryless = 3 };
enum { MTLTextureType2D = 2 };
enum { MTLCommandBufferStatusCompleted = 4 };

/* Read-only view of Metal's descriptor and IOSurface objects: their
 * accessors, declared once so the compiler emits the right calls. */
@protocol S8MetalAccessors
- (NSUInteger)width;
- (NSUInteger)height;
- (NSUInteger)depth;
- (NSUInteger)pixelFormat;
- (NSUInteger)textureType;
- (NSUInteger)mipmapLevelCount;
- (NSUInteger)arrayLength;
- (NSUInteger)sampleCount;
- (NSUInteger)usage;
- (NSUInteger)storageMode;
- (NSUInteger)cpuCacheMode;
- (NSUInteger)resourceOptions;
- (id)colorAttachments;
- (id)objectAtIndexedSubscript:(NSUInteger)i;
- (id)texture;
- (NSUInteger)level;
- (NSUInteger)slice;
- (NSUInteger)loadAction;
- (NSUInteger)storeAction;
- (MTLClearColor)clearColor;
- (id)depthAttachment;
- (id)stencilAttachment;
- (id)vertexFunction;
- (id)fragmentFunction;
- (BOOL)isBlendingEnabled;
- (NSUInteger)sourceRGBBlendFactor;
- (NSUInteger)destinationRGBBlendFactor;
- (NSUInteger)rgbBlendOperation;
- (NSUInteger)sourceAlphaBlendFactor;
- (NSUInteger)destinationAlphaBlendFactor;
- (NSUInteger)alphaBlendOperation;
- (NSUInteger)writeMask;
- (id)constantValues;
- (id)name;
- (id)description;
- (const char *)UTF8String;
- (NSUInteger)count;
- (id)path;
- (const char *)fileSystemRepresentation;
- (id)localizedDescription;
- (id)newNamedConstantArray;
- (id)objectAtIndex:(NSUInteger)i;
- (NSUInteger)dataType;
- (void *)data;
- (id)vertexDescriptor;
- (id)attributes;
- (id)layouts;
- (NSUInteger)format;
- (NSUInteger)offset;
- (NSUInteger)bufferIndex;
- (NSUInteger)stride;
- (NSUInteger)stepFunction;
- (NSUInteger)stepRate;
- (NSUInteger)minFilter;
- (NSUInteger)magFilter;
- (NSUInteger)mipFilter;
- (NSUInteger)sAddressMode;
- (NSUInteger)tAddressMode;
- (NSUInteger)rAddressMode;
- (BOOL)normalizedCoordinates;
- (float)lodMinClamp;
- (float)lodMaxClamp;
- (NSUInteger)size;
- (NSUInteger)type;
@end

/* Metal's private base classes (Metal.framework exports them). */
@interface _MTLObjectWithLabel : NSObject
@end

@interface _MTLCommandQueue : _MTLObjectWithLabel
- (instancetype)initWithDevice:(id)device;
- (void)submitCommandBuffers:(const id *)buffers count:(NSUInteger)count;
- (void)commandBufferDidComplete:(id)buffer startTime:(uint64_t)start completionTime:(uint64_t)end error:(id)error;
@end

@interface _MTLCommandBuffer : _MTLObjectWithLabel
- (instancetype)initWithQueue:(id)queue retainedReferences:(BOOL)retained;
- (void)didScheduleWithStartTime:(uint64_t)start endTime:(uint64_t)end error:(id)error;
- (void)setCurrentCommandEncoder:(id)encoder;
@end

@interface _MTLCommandEncoder : _MTLObjectWithLabel
- (instancetype)initWithCommandBuffer:(id)buffer;
- (void)endEncoding;
@end

@interface _MTLResource : _MTLObjectWithLabel
@end

@interface _MTLSamplerState : NSObject
- (instancetype)initWithDevice:(id)device samplerDescriptor:(id)descriptor;
@end

@interface _MTLDepthStencilState : NSObject
- (instancetype)initWithDevice:(id)device depthStencilDescriptor:(id)descriptor;
@end
