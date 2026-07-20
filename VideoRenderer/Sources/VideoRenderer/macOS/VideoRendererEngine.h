#pragma once

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

#include "VideoRendererTypes.h"

@class VRVideoManifest;

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSString *const VRVideoEngineErrorDomain;

@interface VRVideoRendererEngine : NSView

+ (VRVideoEngineConfig)defaultConfig;

- (instancetype)initWithFrame:(NSRect)frameRect config:(VRVideoEngineConfig)config;
- (nullable instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;

- (BOOL)openWallpaper:(VRVideoManifest *)manifest error:(NSError **)error;

- (void)play;
- (void)pause;
- (void)setVolume:(float)volume;
- (void)setMuted:(BOOL)muted;
- (void)setFillMode:(VRVideoFillMode)fillMode;

@property (nonatomic, copy, nullable) void (^videoDidEndBlock)(void);

@property (nonatomic, strong, readonly) AVQueuePlayer *player;
@property (nonatomic, assign, readonly) BOOL loaded;
@property (nonatomic, assign, readonly) float volume;
@property (nonatomic, assign, readonly) BOOL muted;
@property (nonatomic, assign, readonly) VRVideoFillMode fillMode;

@end

NS_ASSUME_NONNULL_END
