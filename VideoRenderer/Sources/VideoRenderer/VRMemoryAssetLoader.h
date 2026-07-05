//
//  VRMemoryAssetLoader.h
//  VideoRenderer
//
//  预热视频文件到 OS 页缓存并长期持有，让 AVPlayer 循环播放时减少磁盘 I/O。
//  AVPlayer 仍用 file:// URL（原生 MP4 demuxer），数据驻留在 OS 页缓存中。
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// 持有视频文件的 mmap 数据，保持 OS 页缓存常驻，避免循环播放反复读盘。
@interface VRMemoryAssetLoader : NSObject

+ (nullable instancetype)loaderWithData:(NSData *)data fileURL:(NSURL *)fileURL;

/// 已载入的字节数。
@property (nonatomic, readonly) NSUInteger length;

@end

NS_ASSUME_NONNULL_END
