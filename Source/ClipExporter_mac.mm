#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreText/CoreText.h>
#import <AppKit/AppKit.h>

#include "ClipExporter.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <mutex>

#if ! __has_feature(objc_arc)
 #error "ClipExporter_mac.mm must be compiled with -fobjc-arc (see CMakeLists.txt)"
#endif

// The synchronous AVAsset accessors and the block-based export API are
// deprecated in favour of async/await variants that have no C++ equivalent;
// this runs on a background queue, so blocking calls are fine.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

struct ClipExporter::Impl
{
    std::atomic<bool> running { false };
    std::mutex sessionMutex;
    AVAssetExportSession* session = nil;
    dispatch_queue_t queue = dispatch_queue_create ("roadie.export", DISPATCH_QUEUE_SERIAL);
};

namespace
{
NSURL* toURL (const juce::File& f)
{
    return [NSURL fileURLWithPath: (NSString* _Nonnull) [NSString stringWithUTF8String: f.getFullPathName().toRawUTF8()]];
}

juce::String toJuce (NSString* s)
{
    return s != nil ? juce::String::fromUTF8 (s.UTF8String) : juce::String();
}

// The title is rendered to a bitmap with CoreText and handed to the export as
// a plain image layer: CATextLayer doesn't draw reliably through
// AVVideoCompositionCoreAnimationTool on macOS (it exports blank).
CALayer* makeTitleLayer (const juce::String& title, CGSize renderSize)
{
    const CGFloat fontSize = std::round (std::min (renderSize.width, renderSize.height) * 0.045);
    const CGFloat margin = std::round (std::min (renderSize.width, renderSize.height) * 0.06);
    const size_t width = (size_t) (renderSize.width - 2.0 * margin);
    const size_t height = (size_t) std::ceil (fontSize * 1.8);

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate (nullptr, width, height, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease (space);
    if (context == nullptr)
        return [CALayer layer];

    CTFontRef font = CTFontCreateWithName (CFSTR ("AvenirNext-DemiBold"), fontSize, nullptr);
    CGColorRef colour = CGColorCreateGenericRGB (0.96, 0.95, 0.93, 1.0);
    CGColorRef shadow = CGColorCreateGenericRGB (0.0, 0.0, 0.0, 0.65);
    NSDictionary* attributes = @{ (id) kCTFontAttributeName: (__bridge id) font,
                                  (id) kCTForegroundColorAttributeName: (__bridge id) colour };
    NSString* text = [NSString stringWithUTF8String: title.toRawUTF8()] ?: @"";
    NSAttributedString* string = [[NSAttributedString alloc] initWithString: text attributes: attributes];
    CTLineRef line = CTLineCreateWithAttributedString ((__bridge CFAttributedStringRef) string);

    CGContextSetShadowWithColor (context, CGSizeMake (0.0, -2.0), 8.0, shadow);
    CGContextSetTextPosition (context, 6.0, fontSize * 0.5);
    CTLineDraw (line, context);

    CGImageRef image = CGBitmapContextCreateImage (context);
    CALayer* layer = [CALayer layer];
    layer.contents = (__bridge id) image;
    // Layer coordinates have their origin at the bottom-left of the frame.
    layer.frame = CGRectMake (margin, margin, (CGFloat) width, (CGFloat) height);

    CGImageRelease (image);
    CFRelease (line);
    CGColorRelease (shadow);
    CGColorRelease (colour);
    CFRelease (font);
    CGContextRelease (context);
    return layer;
}
} // namespace

ClipExporter::ClipExporter() : impl (std::make_shared<Impl>()) {}

ClipExporter::~ClipExporter()
{
    cancel();
}

bool ClipExporter::isVideoExportSupported() { return true; }
bool ClipExporter::isRunning() const { return impl->running.load(); }

float ClipExporter::getProgress() const
{
    const std::lock_guard<std::mutex> lock (impl->sessionMutex);
    return impl->session != nil ? impl->session.progress : 0.0f;
}

void ClipExporter::cancel()
{
    const std::lock_guard<std::mutex> lock (impl->sessionMutex);
    if (impl->session != nil)
        [impl->session cancelExport];
}

void ClipExporter::start (const ClipExportSettings& settings, std::function<void (bool, juce::String)> onDone)
{
    if (impl->running.exchange (true))
        return;

    auto state = impl;
    const ClipExportSettings settingsCopy = settings; // blocks would capture a reference parameter by reference

    auto finish = [state, onDone] (bool ok, juce::String message)
    {
        {
            const std::lock_guard<std::mutex> lock (state->sessionMutex);
            state->session = nil;
        }
        state->running.store (false);
        juce::MessageManager::callAsync ([onDone, ok, message] { onDone (ok, message); });
    };

    dispatch_async (state->queue, ^{
        @autoreleasepool
        {
            NSDictionary* precise = @{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES };
            AVURLAsset* audioAsset = [AVURLAsset URLAssetWithURL: toURL (settingsCopy.audioFile) options: precise];
            AVAssetTrack* sourceAudio = [[audioAsset tracksWithMediaType: AVMediaTypeAudio] firstObject];
            if (sourceAudio == nil)
                return finish (false, juce::String::fromUTF8 ("Audio du clip illisible."));

            const CMTimeScale scale = 48000;
            const CMTime clipDuration = audioAsset.duration;
            if (CMTimeGetSeconds (clipDuration) < 0.1)
                return finish (false, juce::String::fromUTF8 ("D\xc3\xa9" "coupe trop courte."));

            AVMutableComposition* composition = [AVMutableComposition composition];
            NSError* error = nil;

            AVMutableCompositionTrack* audioTrack = [composition addMutableTrackWithMediaType: AVMediaTypeAudio
                                                                             preferredTrackID: kCMPersistentTrackID_Invalid];
            [audioTrack insertTimeRange: CMTimeRangeMake (kCMTimeZero, clipDuration) ofTrack: sourceAudio atTime: kCMTimeZero error: &error];

            AVMutableCompositionTrack* videoTrack = [composition addMutableTrackWithMediaType: AVMediaTypeVideo
                                                                             preferredTrackID: kCMPersistentTrackID_Invalid];
            AVMutableVideoCompositionLayerInstruction* layer = [AVMutableVideoCompositionLayerInstruction videoCompositionLayerInstructionWithAssetTrack: videoTrack];
            const CGSize renderSize = settingsCopy.vertical ? CGSizeMake (1080, 1920) : CGSizeMake (1920, 1080);

            // Each segment is a piece of a camera file slid to its place on
            // the clip; the regions were already synced by the caller.
            int inserted = 0;
            for (const auto& segment : settingsCopy.segments)
            {
                AVURLAsset* videoAsset = [AVURLAsset URLAssetWithURL: toURL (segment.file) options: precise];
                AVAssetTrack* sourceVideo = [[videoAsset tracksWithMediaType: AVMediaTypeVideo] firstObject];
                if (sourceVideo == nil)
                    continue;

                // The movie may start after the segment does: skip the head.
                double source = segment.sourceStartSeconds, at = segment.atSeconds, length = segment.durationSeconds;
                if (source < 0.0)
                {
                    at -= source;
                    length += source;
                    source = 0.0;
                }
                length = std::min (length, CMTimeGetSeconds (videoAsset.duration) - source);
                length = std::min (length, CMTimeGetSeconds (clipDuration) - at);
                if (length < 1.0 / 60.0)
                    continue;

                const CMTime insertAt = CMTimeMakeWithSeconds (at, scale);
                if (! [videoTrack insertTimeRange: CMTimeRangeMake (CMTimeMakeWithSeconds (source, scale), CMTimeMakeWithSeconds (length, scale))
                                          ofTrack: sourceVideo
                                           atTime: insertAt
                                            error: &error])
                    return finish (false, "Composition impossible : " + toJuce (error.localizedDescription));

                // Centre-crop the camera frame to the target aspect ratio.
                const CGSize natural = CGSizeApplyAffineTransform (sourceVideo.naturalSize, sourceVideo.preferredTransform);
                const CGFloat srcW = std::abs (natural.width), srcH = std::abs (natural.height);
                const CGFloat fill = std::max (renderSize.width / srcW, renderSize.height / srcH);

                CGAffineTransform transform = sourceVideo.preferredTransform;
                if (settingsCopy.mirror)
                    transform = CGAffineTransformConcat (transform, CGAffineTransformMake (-1, 0, 0, 1, srcW, 0));
                transform = CGAffineTransformConcat (transform, CGAffineTransformMakeScale (fill, fill));
                transform = CGAffineTransformConcat (transform, CGAffineTransformMakeTranslation ((renderSize.width - srcW * fill) * 0.5,
                                                                                                 (renderSize.height - srcH * fill) * 0.5));
                [layer setTransform: transform atTime: insertAt];
                ++inserted;
            }

            if (inserted == 0)
                return finish (false, juce::String::fromUTF8 ("Pas de vid\xc3\xa9o dans cette plage \xe2\x80\x94 exporte l'audio seul."));

            AVMutableVideoCompositionInstruction* instruction = [AVMutableVideoCompositionInstruction videoCompositionInstruction];
            instruction.timeRange = CMTimeRangeMake (kCMTimeZero, clipDuration);
            instruction.layerInstructions = @[ layer ];
            CGColorRef background = CGColorCreateGenericRGB (0.0, 0.0, 0.0, 1.0); // where no region has video
            instruction.backgroundColor = background;
            CGColorRelease (background);

            AVMutableVideoComposition* videoComposition = [AVMutableVideoComposition videoComposition];
            videoComposition.renderSize = renderSize;
            videoComposition.frameDuration = CMTimeMake (1, 30);
            videoComposition.instructions = @[ instruction ];

            if (settingsCopy.burnInTitle && settingsCopy.title.isNotEmpty())
            {
                CALayer* parent = [CALayer layer];
                CALayer* video = [CALayer layer];
                parent.frame = CGRectMake (0, 0, renderSize.width, renderSize.height);
                video.frame = parent.frame;
                [parent addSublayer: video];
                [parent addSublayer: makeTitleLayer (settingsCopy.title, renderSize)];
                videoComposition.animationTool = [AVVideoCompositionCoreAnimationTool videoCompositionCoreAnimationToolWithPostProcessingAsVideoLayer: video
                                                                                                                                              inLayer: parent];
            }

            AVAssetExportSession* exportSession = [[AVAssetExportSession alloc] initWithAsset: composition
                                                                                   presetName: AVAssetExportPresetHighestQuality];
            if (exportSession == nil)
                return finish (false, juce::String::fromUTF8 ("Encodeur vid\xc3\xa9o indisponible."));

            settingsCopy.outputFile.deleteFile();
            exportSession.outputURL = toURL (settingsCopy.outputFile);
            exportSession.outputFileType = AVFileTypeMPEG4;
            exportSession.videoComposition = videoComposition;
            exportSession.shouldOptimizeForNetworkUse = YES;

            if (settingsCopy.title.isNotEmpty())
            {
                AVMutableMetadataItem* titleItem = [AVMutableMetadataItem metadataItem];
                titleItem.identifier = AVMetadataCommonIdentifierTitle;
                titleItem.value = [NSString stringWithUTF8String: settingsCopy.title.toRawUTF8()];
                exportSession.metadata = @[ titleItem ];
            }

            {
                const std::lock_guard<std::mutex> lock (state->sessionMutex);
                state->session = exportSession;
            }

            [exportSession exportAsynchronouslyWithCompletionHandler: ^{
                switch (exportSession.status)
                {
                    case AVAssetExportSessionStatusCompleted:
                        finish (true, {});
                        break;
                    case AVAssetExportSessionStatusCancelled:
                        finish (false, juce::String::fromUTF8 ("Export annul\xc3\xa9."));
                        break;
                    case AVAssetExportSessionStatusUnknown:
                    case AVAssetExportSessionStatusWaiting:
                    case AVAssetExportSessionStatusExporting:
                    case AVAssetExportSessionStatusFailed:
                    default:
                        finish (false, "Export impossible : " + toJuce (exportSession.error.localizedDescription));
                        break;
                }
            }];
        }
    });
}

#pragma clang diagnostic pop
