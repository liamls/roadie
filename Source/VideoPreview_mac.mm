#import <AVFoundation/AVFoundation.h>

#include "VideoPreview.h"

#include <atomic>
#include <mutex>

#if ! __has_feature(objc_arc)
 #error "VideoPreview_mac.mm must be compiled with -fobjc-arc (see CMakeLists.txt)"
#endif

// copyCGImageAtTime is deprecated in favour of an async API with no C++
// equivalent; it runs on a background queue here, so blocking is fine.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

struct VideoPreview::Impl
{
    std::mutex lock;
    juce::File file;
    AVAssetImageGenerator* generator = nil;
    double wantedSeconds = 0.0;
    bool hasRequest = false;
    bool decoding = false;
    juce::Image frame;
    std::atomic<int> counter { 0 };
    dispatch_queue_t queue = dispatch_queue_create ("roadie.preview", DISPATCH_QUEUE_SERIAL);
};

namespace
{
juce::Image toJuceImage (CGImageRef image)
{
    const int w = (int) CGImageGetWidth (image), h = (int) CGImageGetHeight (image);
    if (w <= 0 || h <= 0)
        return {};
    juce::Image result (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Image::BitmapData data (result, juce::Image::BitmapData::writeOnly);

    // JUCE's ARGB is premultiplied BGRA in memory on little-endian machines.
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate (data.data, (size_t) w, (size_t) h, 8, (size_t) data.lineStride, space,
                                                  (CGBitmapInfo) kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease (space);
    if (context != nullptr)
    {
        CGContextDrawImage (context, CGRectMake (0, 0, w, h), image);
        CGContextRelease (context);
    }
    return result;
}

void decodeLoop (std::shared_ptr<VideoPreview::Impl> impl)
{
    for (;;)
    {
        AVAssetImageGenerator* generator = nil;
        double seconds = 0.0;
        {
            const std::lock_guard<std::mutex> l (impl->lock);
            if (! impl->hasRequest || impl->generator == nil)
            {
                impl->decoding = false;
                return;
            }
            impl->hasRequest = false;
            generator = impl->generator;
            seconds = impl->wantedSeconds;
        }

        @autoreleasepool
        {
            CGImageRef image = [generator copyCGImageAtTime: CMTimeMakeWithSeconds (std::max (0.0, seconds), 600)
                                                 actualTime: nil
                                                      error: nil];
            if (image != nullptr)
            {
                auto converted = toJuceImage (image);
                CGImageRelease (image);
                const std::lock_guard<std::mutex> l (impl->lock);
                impl->frame = converted;
                impl->counter.fetch_add (1);
            }
        }
    }
}
} // namespace

VideoPreview::VideoPreview() : impl (std::make_shared<Impl>()) {}
VideoPreview::~VideoPreview() = default;

void VideoPreview::request (const juce::File& movie, double seconds)
{
    bool start = false;
    {
        const std::lock_guard<std::mutex> l (impl->lock);
        if (movie != impl->file)
        {
            impl->file = movie;
            NSURL* url = [NSURL fileURLWithPath: (NSString* _Nonnull) [NSString stringWithUTF8String: movie.getFullPathName().toRawUTF8()]];
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
            AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset: asset];
            generator.appliesPreferredTrackTransform = YES;
            generator.maximumSize = CGSizeMake (480, 480);
            // A little tolerance lets the decoder stop at the nearest frame
            // instead of decoding exactly to the requested instant.
            generator.requestedTimeToleranceBefore = CMTimeMakeWithSeconds (0.03, 600);
            generator.requestedTimeToleranceAfter = CMTimeMakeWithSeconds (0.03, 600);
            impl->generator = generator;
        }

        impl->wantedSeconds = seconds;
        impl->hasRequest = true;
        if (! impl->decoding)
        {
            impl->decoding = true;
            start = true;
        }
    }

    if (start)
    {
        auto state = impl;
        dispatch_async (impl->queue, ^{ decodeLoop (state); });
    }
}

void VideoPreview::clear()
{
    const std::lock_guard<std::mutex> l (impl->lock);
    impl->hasRequest = false;
    impl->frame = {};
    impl->counter.fetch_add (1);
}

juce::Image VideoPreview::getFrame() const
{
    const std::lock_guard<std::mutex> l (impl->lock);
    return impl->frame;
}

int VideoPreview::getFrameCounter() const
{
    return impl->counter.load();
}

#pragma clang diagnostic pop
