#import "VRMemoryAssetLoader.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static const NSUInteger kVRMemoryAssetChunkSize = 4 * 1024 * 1024;

@interface VRMemoryAssetLoader ()
- (void)serveLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest;
@end

@implementation VRMemoryAssetLoader {
    NSData *_data;
    dispatch_data_t _backing;
    NSURL *_assetURL;
    NSString *_contentType;
    dispatch_queue_t _loaderQueue;
}

+ (instancetype)loaderWithFileURL:(NSURL *)fileURL error:(NSError **)error {
    // Map, don't read. A 2 GB video used to be pulled into anonymous memory in
    // full and synchronously — on the main thread, since this runs before
    // [app run] — and NSDataReadingUncached explicitly told the kernel it may
    // not reclaim any of it, so the whole file stayed resident for the lifetime
    // of the wallpaper. Mapped pages fault in on demand and evict under
    // pressure, which is exactly the behaviour wanted for linear playback.
    NSData *data = [NSData dataWithContentsOfURL:fileURL
                                         options:NSDataReadingMappedIfSafe
                                           error:error];
    if (data == nil) return nil;

    NSURLComponents *components = [NSURLComponents componentsWithURL:fileURL
                                                resolvingAgainstBaseURL:NO];
    components.scheme = @"mirage-memory-video";
    NSURL *assetURL = components.URL;
    if (assetURL == nil) return nil;

    VRMemoryAssetLoader *loader = [VRMemoryAssetLoader new];
    loader->_data = data;
    // Wrapped once so range requests can be answered with zero-copy subranges.
    // AVFoundation asks for the whole resource at offset 0, and the previous
    // subdataWithRange: answered that by allocating and copying a second full
    // copy of the file — peak memory was twice the file size. The destructor
    // block captures `data`, so every outstanding subrange keeps the mapping
    // alive even if this loader is replaced.
    loader->_backing = dispatch_data_create(
        data.bytes, data.length,
        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0),
        ^{ (void)data; });
    if (loader->_backing == nil) return nil;
    loader->_assetURL = assetURL;
    UTType *type = [UTType typeWithFilenameExtension:fileURL.pathExtension];
    loader->_contentType = type.identifier ?: UTTypeMovie.identifier;
    loader->_loaderQueue = dispatch_queue_create("VideoRenderer.memoryAsset", DISPATCH_QUEUE_SERIAL);
    return loader;
}

- (NSURL *)assetURL { return _assetURL; }
- (NSUInteger)length { return _data.length; }

- (void)attachToAsset:(AVURLAsset *)asset {
    [asset.resourceLoader setDelegate:self queue:_loaderQueue];
}

- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader
    shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest {
    (void)resourceLoader;
    AVAssetResourceLoadingContentInformationRequest *content =
        loadingRequest.contentInformationRequest;
    if (content != nil) {
        NSArray<NSString *> *allowed = content.allowedContentTypes;
        content.contentType = allowed.count == 0 || [allowed containsObject:_contentType]
            ? _contentType : allowed.firstObject;
        content.contentLength = (long long)_data.length;
        content.byteRangeAccessSupported = YES;
    }

    if (loadingRequest.dataRequest == nil) {
        [loadingRequest finishLoading];
        return YES;
    }

    dispatch_async(_loaderQueue, ^{
        [self serveLoadingRequest:loadingRequest];
    });
    return YES;
}

- (void)serveLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest {
    if (loadingRequest.cancelled || loadingRequest.finished) return;

    AVAssetResourceLoadingDataRequest *request = loadingRequest.dataRequest;
    if (request == nil) {
        [loadingRequest finishLoading];
        return;
    }

    long long currentOffset = request.currentOffset;
    if (currentOffset < request.requestedOffset) currentOffset = request.requestedOffset;
    if (currentOffset < 0 || (unsigned long long)currentOffset > _data.length) {
        [loadingRequest finishLoadingWithError:[NSError
            errorWithDomain:NSURLErrorDomain code:NSURLErrorBadServerResponse userInfo:nil]];
        return;
    }

    NSUInteger offset = (NSUInteger)currentOffset;
    NSUInteger end = _data.length;
    if (!request.requestsAllDataToEndOfResource) {
        unsigned long long start = (unsigned long long)MAX(request.requestedOffset, 0);
        unsigned long long length = (unsigned long long)MAX(request.requestedLength, 0);
        unsigned long long requestedEnd = length > ULLONG_MAX - start
            ? ULLONG_MAX : start + length;
        end = (NSUInteger)MIN(requestedEnd, (unsigned long long)_data.length);
    }

    if (offset >= end) {
        [loadingRequest finishLoading];
        return;
    }

    NSUInteger length = MIN(kVRMemoryAssetChunkSize, end - offset);
    dispatch_data_t slice = dispatch_data_create_subrange(_backing, offset, length);
    if (slice == nil) {
        [loadingRequest finishLoadingWithError:[NSError
            errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotDecodeContentData userInfo:nil]];
        return;
    }

    [request respondWithData:(NSData *)(id)slice];
    if (loadingRequest.cancelled || loadingRequest.finished) return;
    if (offset + length >= end) {
        [loadingRequest finishLoading];
        return;
    }

    dispatch_async(_loaderQueue, ^{
        [self serveLoadingRequest:loadingRequest];
    });
}

@end
