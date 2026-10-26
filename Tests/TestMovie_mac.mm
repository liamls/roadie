// Test helpers that need AVFoundation: synthesise a camera-like .mov and
// inspect an exported MP4, so the export pipeline can be checked without a
// camera or a human.

#import <AVFoundation/AVFoundation.h>

#include "TestMovie.h"

#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace roadie::test
{
bool writeTestMovie (const juce::File& file, int width, int height, double seconds)
{
    file.deleteFile();
    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]];

    NSError* error = nil;
    AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL: url fileType: AVFileTypeQuickTimeMovie error: &error];
    if (writer == nil)
        return false;

    AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                                                                   outputSettings: @{ AVVideoCodecKey: AVVideoCodecTypeH264,
                                                                                      AVVideoWidthKey: @(width),
                                                                                      AVVideoHeightKey: @(height) }];
    AVAssetWriterInputPixelBufferAdaptor* adaptor =
        [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput: input
                                                                         sourcePixelBufferAttributes: @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                                                                                         (id) kCVPixelBufferWidthKey: @(width),
                                                                                                         (id) kCVPixelBufferHeightKey: @(height) }];
    [writer addInput: input];
    if (! [writer startWriting])
        return false;
    [writer startSessionAtSourceTime: kCMTimeZero];

    const int frames = (int) (seconds * 30.0);
    for (int f = 0; f < frames; ++f)
    {
        while (! input.readyForMoreMediaData)
            [NSThread sleepForTimeInterval: 0.005];

        CVPixelBufferRef buffer = nullptr;
        CVPixelBufferPoolCreatePixelBuffer (nullptr, adaptor.pixelBufferPool, &buffer);
        if (buffer == nullptr)
            return false;

        CVPixelBufferLockBaseAddress (buffer, 0);
        auto* base = static_cast<uint8_t*> (CVPixelBufferGetBaseAddress (buffer));
        const size_t stride = CVPixelBufferGetBytesPerRow (buffer);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                auto* p = base + (size_t) y * stride + (size_t) x * 4;
                p[0] = (uint8_t) (f * 8);          // B: changes every frame
                p[1] = (uint8_t) (x * 255 / width); // G: horizontal ramp
                p[2] = (uint8_t) (y * 255 / height);
                p[3] = 255;
            }
        CVPixelBufferUnlockBaseAddress (buffer, 0);

        [adaptor appendPixelBuffer: buffer withPresentationTime: CMTimeMake (f, 30)];
        CVPixelBufferRelease (buffer);
    }

    [input markAsFinished];
    dispatch_semaphore_t done = dispatch_semaphore_create (0);
    [writer finishWritingWithCompletionHandler: ^{ dispatch_semaphore_signal (done); }];
    dispatch_semaphore_wait (done, DISPATCH_TIME_FOREVER);
    return writer.status == AVAssetWriterStatusCompleted;
}

MovieInfo inspectMovie (const juce::File& file)
{
    MovieInfo info;
    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
    info.durationSeconds = CMTimeGetSeconds (asset.duration);

    AVAssetTrack* video = [[asset tracksWithMediaType: AVMediaTypeVideo] firstObject];
    if (video != nil)
    {
        info.hasVideo = true;
        info.width = (int) video.naturalSize.width;
        info.height = (int) video.naturalSize.height;
    }
    info.hasAudio = [[asset tracksWithMediaType: AVMediaTypeAudio] count] > 0;
    return info;
}
int countBrightPixels (const juce::File& file, double seconds, juce::Rectangle<float> region)
{
    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
    AVAssetImageGenerator* generator = [AVAssetImageGenerator assetImageGeneratorWithAsset: asset];
    generator.requestedTimeToleranceBefore = kCMTimeZero;
    generator.requestedTimeToleranceAfter = kCMTimeZero;

    CGImageRef image = [generator copyCGImageAtTime: CMTimeMakeWithSeconds (seconds, 600) actualTime: nil error: nil];
    if (image == nullptr)
        return -1;

    const size_t w = CGImageGetWidth (image), h = CGImageGetHeight (image);
    std::vector<uint8_t> pixels (w * h * 4);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate (pixels.data(), w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast);
    CGContextDrawImage (context, CGRectMake (0, 0, (CGFloat) w, (CGFloat) h), image);
    CGContextRelease (context);
    CGColorSpaceRelease (space);
    CGImageRelease (image);

    int count = 0;
    for (size_t y = (size_t) (region.getY() * (float) h); y < (size_t) (region.getBottom() * (float) h); ++y)
        for (size_t x = (size_t) (region.getX() * (float) w); x < (size_t) (region.getRight() * (float) w); ++x)
        {
            const auto* p = &pixels[(y * w + x) * 4];
            if (p[0] > 225 && p[1] > 225 && p[2] > 225)
                ++count;
        }
    return count;
}
} // namespace roadie::test

#pragma clang diagnostic pop
