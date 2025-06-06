/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import <Foundation/Foundation.h>
#import <wtf/Platform.h>

#if ENABLE(MODEL_PROCESS)

#import <simd/simd.h>

typedef struct REAsset *REAssetRef;
typedef struct REEntity *REEntityRef;

NS_ASSUME_NONNULL_BEGIN

typedef struct {
    simd_float3 scale;
    simd_quatf rotation;
    simd_float3 translation;
} WKEntityTransform;

@protocol WKSRKEntityDelegate <NSObject>
@optional
- (void)entityAnimationPlaybackStateDidUpdate:(id)entity;
@end

@interface WKSRKEntity : NSObject
@property (nonatomic, weak) id <WKSRKEntityDelegate> delegate;
@property (nonatomic, copy, nullable) NSString *name;

@property (nonatomic, readonly) simd_float3 boundingBoxExtents;
@property (nonatomic, readonly) simd_float3 boundingBoxCenter;
@property (nonatomic, readonly) float boundingRadius;
@property (nonatomic, readonly) simd_float3 interactionPivotPoint;
@property (nonatomic) WKEntityTransform transform;
@property (nonatomic) float opacity;
@property (nonatomic, readonly) NSTimeInterval duration;
@property (nonatomic) bool loop;
@property (nonatomic) float playbackRate;
@property (nonatomic) bool paused;
@property (nonatomic) NSTimeInterval currentTime;

+ (bool)isLoadFromDataAvailable;
+ (void)loadFromData:(NSData *)data withAttributionTaskID:(nullable NSString *)attributionTaskId completionHandler:(void (^)(WKSRKEntity * _Nullable entity))completionHandler;
- (instancetype)initWithCoreEntity:(REEntityRef)coreEntity;
- (void)setParentCoreEntity:(REEntityRef)parentCoreEntity preservingWorldTransform:(BOOL)preservingWorldTransform NS_SWIFT_NAME(setParent(_:preservingWorldTransform:));
- (void)setUpAnimationWithAutoPlay:(BOOL)autoPlay;
- (void)applyIBLData:(NSData *)data attributionHandler:(void (^)(REAssetRef coreEnvironmentResourceAsset))attributionHandler withCompletion:(void (^)(BOOL success))completion;
- (void)interactionContainerDidRecenterFromTransform:(simd_float4x4)transform NS_SWIFT_NAME(interactionContainerDidRecenter(_:));
- (void)recenterEntityAtTransform:(WKEntityTransform)transform NS_SWIFT_NAME(recenterEntity(at:));
- (void)applyDefaultIBL;
@end

NS_ASSUME_NONNULL_END

#endif // defined(TARGET_OS_VISION) && TARGET_OS_VISION
