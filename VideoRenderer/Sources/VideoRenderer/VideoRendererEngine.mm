#import "VideoRendererEngine.h"

#import "VRMemoryAssetLoader.h"

#import <QuartzCore/QuartzCore.h>

NSString *const VRVideoEngineErrorDomain = @"VideoRenderer.Engine";

enum {
    VRVideoEngineErrorInvalidManifest = 1,
    VRVideoEngineErrorCannotQueueItem,
};

static NSError *VRVideoEngineError(NSInteger code, NSString *description) {
    return [NSError errorWithDomain:VRVideoEngineErrorDomain
                               code:code
                           userInfo:@{ NSLocalizedDescriptionKey: description }];
}

static AVLayerVideoGravity VRLayerGravityForFillMode(VRVideoFillMode mode) {
    switch (mode) {
    case VRVideoFillModeContain: return AVLayerVideoGravityResizeAspect;
    case VRVideoFillModeStretch: return AVLayerVideoGravityResize;
    case VRVideoFillModeCover:
    default: return AVLayerVideoGravityResizeAspectFill;
    }
}

static float VRClampVolume(float value) {
    if (!isfinite(value)) return 1.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

@interface VRVideoRendererEngine ()
@property (nonatomic, strong) AVQueuePlayer *player;
@property (nonatomic, strong) AVPlayerLayer *playerLayer;
@property (nonatomic, strong) AVPlayerLooper *looper;
@property (nonatomic, strong) id activity;
@property (nonatomic, strong) VRMemoryAssetLoader *memLoader;   // 持有内存加载器，避免释放
@property (nonatomic, assign) BOOL loaded;
@property (nonatomic, assign) float volume;
@property (nonatomic, assign) BOOL muted;
@property (nonatomic, assign) VRVideoFillMode fillMode;
@property (nonatomic, assign) BOOL autoplay;
@end

@implementation VRVideoRendererEngine

+ (VRVideoEngineConfig)defaultConfig {
    VRVideoEngineConfig config;
    config.fillMode = VRVideoFillModeCover;
    config.initialVolume = 1.0f;
    config.muted = NO;
    config.autoplay = YES;
    return config;
}

- (instancetype)initWithFrame:(NSRect)frameRect config:(VRVideoEngineConfig)config {
    self = [super initWithFrame:frameRect];
    if (self) {
        self.wantsLayer = YES;
        self.layer = [CALayer layer];
        self.layer.backgroundColor = NSColor.blackColor.CGColor;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;

        _player = [AVQueuePlayer queuePlayerWithItems:@[]];
        _player.actionAtItemEnd = AVPlayerActionAtItemEndNone;
        _player.automaticallyWaitsToMinimizeStalling = YES;
        _player.volume = VRClampVolume(config.initialVolume);
        _player.muted = config.muted;

        _playerLayer = [AVPlayerLayer playerLayerWithPlayer:_player];
        _playerLayer.frame = self.bounds;
        _playerLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        _playerLayer.backgroundColor = NSColor.blackColor.CGColor;
        _playerLayer.needsDisplayOnBoundsChange = NO;
        [self.layer addSublayer:_playerLayer];

        _volume = _player.volume;
        _muted = config.muted;
        _autoplay = config.autoplay;
        [self setFillMode:config.fillMode];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)layout {
    [super layout];
    self.playerLayer.frame = self.bounds;
}

- (void)dealloc {
    [self pause];
    [self.player removeAllItems];
}

- (BOOL)openWallpaper:(VRVideoManifest *)manifest error:(NSError **)error {
    if (manifest == nil || manifest.videoURL == nil) {
        if (error != NULL) *error = VRVideoEngineError(VRVideoEngineErrorInvalidManifest,
                                                       @"invalid video wallpaper manifest");
        return NO;
    }

    [self.player pause];
    [self.player removeAllItems];
    self.looper = nil;
    self.memLoader = nil;
    self.loaded = NO;

    NSError *readErr = nil;
    NSData *prewarm = [NSData dataWithContentsOfURL:manifest.videoURL
                                            options:NSDataReadingMappedIfSafe
                                              error:&readErr];
    if (prewarm != nil) {
        volatile char c = 0;
        const uint8_t *bytes = (const uint8_t *)prewarm.bytes;
        NSUInteger len = prewarm.length;
        NSUInteger step = 4096;
        for (NSUInteger i = 0; i < len; i += step) {
            c = bytes[i];
        }
        (void)c;
        // 用一个长生命周期的 loader 持有这份数据，保证页缓存常驻
        self.memLoader = [VRMemoryAssetLoader loaderWithData:prewarm fileURL:manifest.videoURL];
    }

    NSDictionary *assetOptions = @{
        AVURLAssetPreferPreciseDurationAndTimingKey: @NO,
    };
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:manifest.videoURL options:assetOptions];
    AVPlayerItem *item = [AVPlayerItem playerItemWithAsset:asset];
    item.preferredForwardBufferDuration = 0;   // 不限制缓冲，尽量多预读

    if (![self.player canInsertItem:item afterItem:nil]) {
        if (error != NULL) {
            *error = VRVideoEngineError(VRVideoEngineErrorCannotQueueItem,
                                        [NSString stringWithFormat:@"cannot queue video: %@",
                                                                   manifest.videoURL.path]);
        }
        return NO;
    }

    self.looper = [AVPlayerLooper playerLooperWithPlayer:self.player templateItem:item];
    self.player.volume = self.volume;
    self.player.muted = self.muted;
    self.loaded = YES;

    if (self.autoplay) [self play];
    return YES;
}

- (void)play {
    if (!self.loaded) return;
    if (self.activity == nil) {
        self.activity = [NSProcessInfo.processInfo
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                              reason:@"VideoRenderer video wallpaper playback"];
    }
    [self.player play];
}

- (void)pause {
    [self.player pause];
    if (self.activity != nil) {
        [NSProcessInfo.processInfo endActivity:self.activity];
        self.activity = nil;
    }
}

- (void)setVolume:(float)volume {
    _volume = VRClampVolume(volume);
    self.player.volume = _volume;
}

- (void)setMuted:(BOOL)muted {
    _muted = muted;
    self.player.muted = muted;
}

- (void)setFillMode:(VRVideoFillMode)fillMode {
    _fillMode = fillMode;
    self.playerLayer.videoGravity = VRLayerGravityForFillMode(fillMode);
}

@end
