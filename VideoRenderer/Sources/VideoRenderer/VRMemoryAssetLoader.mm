//
//  VRMemoryAssetLoader.mm
//  VideoRenderer
//

#import "VRMemoryAssetLoader.h"

@implementation VRMemoryAssetLoader {
    NSData *_data;
    NSURL *_fileURL;
}

+ (instancetype)loaderWithData:(NSData *)data fileURL:(NSURL *)fileURL {
    if (data == nil) return nil;
    VRMemoryAssetLoader *loader = [VRMemoryAssetLoader new];
    loader->_data = data;
    loader->_fileURL = fileURL;
    return loader;
}

- (NSUInteger)length { return _data.length; }

@end
