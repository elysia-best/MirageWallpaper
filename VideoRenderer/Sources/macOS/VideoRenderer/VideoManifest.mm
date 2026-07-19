#import "VideoManifest.h"

NSString *const VRManifestErrorDomain = @"VideoRenderer.Manifest";

enum {
    VRManifestErrorOpenFailed = 1,
    VRManifestErrorInvalidJSON,
    VRManifestErrorWrongType,
    VRManifestErrorMissingVideo,
};

@interface VRVideoManifest ()
- (instancetype)initWithDirectory:(NSString *)directory
                            title:(NSString *)title
                          preview:(nullable NSString *)preview
                        videoFile:(NSString *)videoFile
                         videoURL:(NSURL *)videoURL
                   userProperties:(NSDictionary<NSString *, id> *)userProperties;
@end

static NSError *VRMakeError(NSInteger code, NSString *description, NSError *underlying) {
    NSMutableDictionary *info = [@{ NSLocalizedDescriptionKey: description } mutableCopy];
    if (underlying != nil) info[NSUnderlyingErrorKey] = underlying;
    return [NSError errorWithDomain:VRManifestErrorDomain code:code userInfo:info];
}

static BOOL VRIsDictionary(id value) {
    return [value isKindOfClass:NSDictionary.class];
}

static NSString *VRStringValue(id value) {
    return [value isKindOfClass:NSString.class] ? value : nil;
}

static NSSet<NSString *> *VRVideoExtensions(void) {
    static NSSet<NSString *> *extensions;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      extensions = [NSSet setWithArray:@[
          @"mp4", @"m4v", @"mov", @"qt", @"avi", @"mkv", @"webm", @"mpg", @"mpeg",
      ]];
    });
    return extensions;
}

static NSString *VRFindFirstVideoFile(NSString *directory) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray<NSString *> *children = [fm contentsOfDirectoryAtPath:directory error:nil];
    for (NSString *name in children) {
        if ([VRVideoExtensions() containsObject:name.pathExtension.lowercaseString]) {
            return name;
        }
    }
    return nil;
}

static NSDictionary<NSString *, id> *VRManifestUserProperties(NSDictionary *json) {
    id general = json[@"general"];
    if (!VRIsDictionary(general)) return @{};
    id properties = ((NSDictionary *)general)[@"properties"];
    return VRIsDictionary(properties) ? properties : @{};
}

@implementation VRVideoManifest {
    NSString *_wallpaperDirectory;
    NSString *_title;
    NSString *_preview;
    NSString *_videoFile;
    NSURL *_videoURL;
    NSDictionary<NSString *, id> *_userProperties;
}

+ (instancetype)loadFromDirectory:(NSString *)dir error:(NSError **)error {
    NSString *directory = dir.stringByStandardizingPath;
    BOOL isDir = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:directory isDirectory:&isDir] || !isDir) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorOpenFailed,
                                 [NSString stringWithFormat:@"wallpaper directory not found: %@", directory],
                                 nil);
        }
        return nil;
    }

    NSString *projectPath = [directory stringByAppendingPathComponent:@"project.json"];
    NSError *readError = nil;
    NSData *data = [NSData dataWithContentsOfFile:projectPath
                                          options:NSDataReadingMappedIfSafe
                                            error:&readError];
    if (data == nil) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorOpenFailed,
                                 [NSString stringWithFormat:@"cannot open %@", projectPath],
                                 readError);
        }
        return nil;
    }

    NSError *jsonError = nil;
    id parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
    if (!VRIsDictionary(parsed)) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorInvalidJSON,
                                 [NSString stringWithFormat:@"invalid project.json: %@", projectPath],
                                 jsonError);
        }
        return nil;
    }
    NSDictionary *json = parsed;

    NSString *type = VRStringValue(json[@"type"]).lowercaseString;
    if (![type isEqualToString:@"video"]) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorWrongType,
                                 [NSString stringWithFormat:@"project.json type is '%@', expected 'video'",
                                                            type.length ? type : @"<missing>"],
                                 nil);
        }
        return nil;
    }

    NSString *videoFile = VRStringValue(json[@"file"]);
    if (videoFile.length == 0) videoFile = VRFindFirstVideoFile(directory);
    if (videoFile.length == 0) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorMissingVideo,
                                 @"video wallpaper has no playable file entry",
                                 nil);
        }
        return nil;
    }

    NSString *videoPath = [[directory stringByAppendingPathComponent:videoFile] stringByStandardizingPath];
    BOOL isVideoDir = NO;
    if (![NSFileManager.defaultManager fileExistsAtPath:videoPath isDirectory:&isVideoDir] || isVideoDir) {
        if (error != NULL) {
            *error = VRMakeError(VRManifestErrorMissingVideo,
                                 [NSString stringWithFormat:@"video file not found: %@", videoPath],
                                 nil);
        }
        return nil;
    }

    NSString *title = VRStringValue(json[@"title"]);
    if (title.length == 0) title = directory.lastPathComponent.length ? directory.lastPathComponent : @"VideoWallpaper";

    NSString *preview = VRStringValue(json[@"preview"]);
    return [[self alloc] initWithDirectory:directory
                                     title:title
                                   preview:preview.length ? preview : nil
                                 videoFile:videoFile
                                  videoURL:[NSURL fileURLWithPath:videoPath isDirectory:NO]
                            userProperties:VRManifestUserProperties(json)];
}

- (instancetype)initWithDirectory:(NSString *)directory
                            title:(NSString *)title
                          preview:(NSString *)preview
                        videoFile:(NSString *)videoFile
                         videoURL:(NSURL *)videoURL
                   userProperties:(NSDictionary<NSString *,id> *)userProperties {
    self = [super init];
    if (self) {
        _wallpaperDirectory = [directory copy];
        _title = [title copy];
        _preview = [preview copy];
        _videoFile = [videoFile copy];
        _videoURL = videoURL;
        _userProperties = [userProperties copy] ?: @{};
    }
    return self;
}

- (NSString *)wallpaperDirectory { return _wallpaperDirectory; }
- (NSString *)title { return _title; }
- (NSString *)preview { return _preview; }
- (NSString *)videoFile { return _videoFile; }
- (NSURL *)videoURL { return _videoURL; }
- (NSDictionary<NSString *,id> *)userProperties { return _userProperties; }

@end
