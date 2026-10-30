#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include "CameraCapture.h"
#include "I18n.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <mutex>

#if ! __has_feature(objc_arc)
 #error "CameraCapture_mac.mm must be compiled with -fobjc-arc (see CMakeLists.txt)"
#endif

namespace
{
juce::String permissionDeniedText()
{
    return tr ("Camera access denied — allow Roadie in System Settings › Privacy & Security › Camera.");
}

juce::String micDeniedText()
{
    return tr ("Microphone access denied — allow Roadie in System Settings › Privacy & Security › Microphone.");
}

constexpr int previewMaxWidth = 480;

NSArray<AVCaptureDevice*>* captureDevices()
{
    NSMutableArray<AVCaptureDeviceType>* types = [NSMutableArray arrayWithObject: AVCaptureDeviceTypeBuiltInWideAngleCamera];

    if (@available (macOS 14.0, *))
    {
        [types addObject: AVCaptureDeviceTypeExternal];
        [types addObject: AVCaptureDeviceTypeContinuityCamera]; // an iPhone as webcam is a great guitar cam
    }
    else
    {
       #pragma clang diagnostic push
       #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject: AVCaptureDeviceTypeExternalUnknown];
       #pragma clang diagnostic pop
    }

    auto* discovery = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes: types
                                                                             mediaType: AVMediaTypeVideo
                                                                              position: AVCaptureDevicePositionUnspecified];
    return discovery.devices;
}

juce::String toJuce (NSString* s)
{
    return s != nil ? juce::String::fromUTF8 (s.UTF8String) : juce::String();
}

template <typename Fn>
void callOnMessageThread (Fn&& fn)
{
    juce::MessageManager::callAsync (std::forward<Fn> (fn));
}
} // namespace

@interface RoadieCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate, AVCaptureAudioDataOutputSampleBufferDelegate>
{
@public
    std::weak_ptr<CameraCapture::Impl> owner;
}
@end

//==============================================================================
struct CameraCapture::Impl
{
    Impl()
    {
        sessionQueue = dispatch_queue_create ("roadie.camera.session", DISPATCH_QUEUE_SERIAL);
        videoQueue = dispatch_queue_create ("roadie.camera.video", DISPATCH_QUEUE_SERIAL);
        audioQueue = dispatch_queue_create ("roadie.camera.audio", DISPATCH_QUEUE_SERIAL);
    }

    ~Impl()
    {
        if (syncClock != nullptr)
            CFRelease (syncClock);
    }

    double hostSeconds (CMTime pts) const
    {
        // Frame and mic timestamps are on the session's clock; bring them to
        // the host clock CoreAudio uses for the guitar audio.
        if (syncClock == nullptr)
            return CMTimeGetSeconds (pts);
        return CMTimeGetSeconds (CMSyncConvertTime (pts, syncClock, CMClockGetHostTimeClock()));
    }

    void handleVideo (CMSampleBufferRef sampleBuffer);
    void handleAudio (CMSampleBufferRef sampleBuffer);
    void startWriter (int w, int h);

    // Session objects: created/destroyed on sessionQueue only.
    AVCaptureSession* session = nil;
    AVCaptureDeviceInput* micInput = nil;
    AVCaptureAudioDataOutput* micOutput = nil;
    RoadieCaptureDelegate* delegate = nil;
    CMClockRef syncClock = nullptr;

    dispatch_queue_t sessionQueue;
    dispatch_queue_t videoQueue;
    dispatch_queue_t audioQueue;

    std::atomic<bool> isOpen { false };
    juce::String deviceName; // message thread
    std::atomic<int> width { 0 }, height { 0 };

    mutable std::mutex frameMutex;
    juce::Image latestFrame;
    std::atomic<int> frameCounter { 0 };

    // Recording: writer state lives on videoQueue only, except the atomics.
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* writerInput = nil;
    NSURL* pendingURL = nil;
    bool writerSessionStarted = false;
    double lastThumbnailHost = 0.0;
    double thumbnailInterval = 1.0; // seconds; doubles when the list is full, so a long take keeps a bounded number
    std::atomic<bool> recording { false };
    std::atomic<double> firstFrameHost { 0.0 };
    std::atomic<double> lastFrameHost { 0.0 };

    mutable std::mutex thumbnailMutex;
    std::vector<juce::Image> thumbnails;

    // Calibration mic.
    mutable std::mutex micMutex;
    std::vector<float> micSamples;
    double micWantedSeconds = 0.0;
    size_t micTarget = 0;
    double micFirstHost = 0.0;
    double micRate = 0.0;
};

@implementation RoadieCaptureDelegate

- (void) captureOutput: (AVCaptureOutput*) output
 didOutputSampleBuffer: (CMSampleBufferRef) sampleBuffer
        fromConnection: (AVCaptureConnection*) connection
{
    juce::ignoreUnused (connection);

    if (auto o = owner.lock())
    {
        if ([output isKindOfClass: [AVCaptureVideoDataOutput class]])
            o->handleVideo (sampleBuffer);
        else
            o->handleAudio (sampleBuffer);
    }
}

@end

//==============================================================================
void CameraCapture::Impl::startWriter (int w, int h)
{
    NSError* error = nil;
    AVAssetWriter* newWriter = [[AVAssetWriter alloc] initWithURL: pendingURL
                                                         fileType: AVFileTypeQuickTimeMovie
                                                            error: &error];
    pendingURL = nil;
    if (newWriter == nil)
        return;

    NSDictionary* settings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(w),
        AVVideoHeightKey: @(h),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(std::max (4000000, w * h * 8)),
            AVVideoMaxKeyFrameIntervalKey: @60,
        },
    };

    AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                                                                   outputSettings: settings];
    input.expectsMediaDataInRealTime = YES;

    if (! [newWriter canAddInput: input])
        return;

    [newWriter addInput: input];
    if (! [newWriter startWriting])
        return;

    writer = newWriter;
    writerInput = input;
    writerSessionStarted = false;
}

void CameraCapture::Impl::handleVideo (CMSampleBufferRef sampleBuffer)
{
    CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer (sampleBuffer);
    if (pixelBuffer == nullptr)
        return;

    const CMTime pts = CMSampleBufferGetPresentationTimeStamp (sampleBuffer);
    const double host = hostSeconds (pts);

    CVPixelBufferLockBaseAddress (pixelBuffer, kCVPixelBufferLock_ReadOnly);
    const int w = (int) CVPixelBufferGetWidth (pixelBuffer);
    const int h = (int) CVPixelBufferGetHeight (pixelBuffer);
    const size_t stride = CVPixelBufferGetBytesPerRow (pixelBuffer);
    const auto* base = static_cast<const uint8_t*> (CVPixelBufferGetBaseAddress (pixelBuffer));

    width.store (w, std::memory_order_relaxed);
    height.store (h, std::memory_order_relaxed);

    // BGRA from the camera is exactly JUCE's little-endian ARGB layout, so a
    // nearest-neighbour downscale is a plain 32-bit copy per pixel.
    const int tw = std::min (previewMaxWidth, w);
    const int th = std::max (1, h * tw / std::max (1, w));
    juce::Image frame (juce::Image::ARGB, tw, th, false, juce::SoftwareImageType());

    if (base != nullptr)
    {
        juce::Image::BitmapData data (frame, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < th; ++y)
        {
            auto* dst = reinterpret_cast<uint32_t*> (data.getLinePointer (y));
            const auto* src = reinterpret_cast<const uint32_t*> (base + (size_t) (y * h / th) * stride);
            for (int x = 0; x < tw; ++x)
                dst[x] = src[x * w / tw] | 0xff000000u;
        }
    }
    CVPixelBufferUnlockBaseAddress (pixelBuffer, kCVPixelBufferLock_ReadOnly);

    {
        const std::lock_guard<std::mutex> lock (frameMutex);
        latestFrame = frame;
    }
    frameCounter.fetch_add (1, std::memory_order_release);

    if (! recording.load (std::memory_order_acquire))
        return;

    if (writer == nil && pendingURL != nil)
        startWriter (w, h);

    if (writer == nil || writer.status != AVAssetWriterStatusWriting)
        return;

    if (! writerSessionStarted)
    {
        [writer startSessionAtSourceTime: pts];
        writerSessionStarted = true;
        firstFrameHost.store (host, std::memory_order_release);
        lastThumbnailHost = -1.0e9;
    }

    if (writerInput.readyForMoreMediaData)
        [writerInput appendSampleBuffer: sampleBuffer];

    lastFrameHost.store (host, std::memory_order_release);

    if (host - lastThumbnailHost >= thumbnailInterval)
    {
        lastThumbnailHost = host;
        const std::lock_guard<std::mutex> lock (thumbnailMutex);
        thumbnails.push_back (frame);
        if (thumbnails.size() >= 96)
        {
            // Keep every other one: the project only saves a couple of dozen anyway.
            for (size_t i = 1; i * 2 < thumbnails.size(); ++i)
                thumbnails[i] = thumbnails[i * 2];
            thumbnails.resize ((thumbnails.size() + 1) / 2);
            thumbnailInterval *= 2.0;
        }
    }
}

void CameraCapture::Impl::handleAudio (CMSampleBufferRef sampleBuffer)
{
    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription (sampleBuffer);
    const AudioStreamBasicDescription* asbd = format != nullptr ? CMAudioFormatDescriptionGetStreamBasicDescription (format) : nullptr;
    if (asbd == nullptr)
        return;

    size_t listSize = 0;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (sampleBuffer, &listSize, nullptr, 0, nullptr, nullptr, 0, nullptr) != noErr)
        return;

    juce::HeapBlock<char> listStorage (listSize);
    auto* list = reinterpret_cast<AudioBufferList*> (listStorage.get());
    CMBlockBufferRef block = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (sampleBuffer, nullptr, list, listSize, nullptr, nullptr,
                                                                 kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr)
        return;

    const auto numFrames = (size_t) CMSampleBufferGetNumSamples (sampleBuffer);
    const bool isFloat = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    const bool nonInterleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    const size_t stride = nonInterleaved ? 1 : std::max<size_t> (1, asbd->mChannelsPerFrame);
    if (list->mNumberBuffers < 1 || list->mBuffers[0].mData == nullptr)
    {
        if (block != nullptr)
            CFRelease (block);
        return;
    }
    const AudioBuffer& first = list->mBuffers[0];
    // Never read past what the buffer holds, whatever the frame count says.
    const size_t bytesPerSample = asbd->mBitsPerChannel / 8;
    const size_t available = bytesPerSample > 0 ? first.mDataByteSize / (bytesPerSample * stride) : 0;

    {
        const std::lock_guard<std::mutex> lock (micMutex);

        if (micWantedSeconds > 0.0 && micSamples.empty())
        {
            micRate = asbd->mSampleRate;
            micTarget = (size_t) (micWantedSeconds * micRate);
            micFirstHost = hostSeconds (CMSampleBufferGetPresentationTimeStamp (sampleBuffer));
        }

        // Only the first channel: one clean transient is all the analysis needs.
        for (size_t i = 0; i < std::min (numFrames, available) && micSamples.size() < micTarget; ++i)
        {
            float value = 0.0f;
            if (isFloat && asbd->mBitsPerChannel == 32)
                value = static_cast<const float*> (first.mData)[i * stride];
            else if (asbd->mBitsPerChannel == 16)
                value = static_cast<const int16_t*> (first.mData)[i * stride] / 32768.0f;
            micSamples.push_back (value);
        }
    }

    if (block != nullptr)
        CFRelease (block);
}

//==============================================================================
CameraCapture::CameraCapture() : impl (std::make_shared<Impl>())
{
    impl->delegate = [[RoadieCaptureDelegate alloc] init];
    impl->delegate->owner = impl;
}

CameraCapture::~CameraCapture()
{
    close();
}

bool CameraCapture::isSupported() { return true; }

juce::StringArray CameraCapture::getDeviceNames()
{
    juce::StringArray names;
    for (AVCaptureDevice* device in captureDevices())
        names.add (toJuce (device.localizedName));
    return names;
}

void CameraCapture::open (const juce::String& wantedName, std::function<void (bool, juce::String)> onDone)
{
    close();

    std::weak_ptr<Impl> weak = impl;

    auto proceed = [weak, wantedName, onDone]
    {
        auto self = weak.lock();
        if (self == nullptr)
            return;

        dispatch_async (self->sessionQueue, ^{
            auto s = weak.lock();
            if (s == nullptr)
                return;

            AVCaptureDevice* device = nil;
            for (AVCaptureDevice* candidate in captureDevices())
                if (toJuce (candidate.localizedName) == wantedName)
                    device = candidate;
            if (device == nil)
                device = [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];

            if (device == nil)
            {
                callOnMessageThread ([onDone] { onDone (false, tr ("No camera found.")); });
                return;
            }

            AVCaptureSession* session = [[AVCaptureSession alloc] init];
            [session beginConfiguration];
            session.sessionPreset = [session canSetSessionPreset: AVCaptureSessionPreset1280x720] ? AVCaptureSessionPreset1280x720
                                                                                                    : AVCaptureSessionPresetHigh;

            NSError* error = nil;
            AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice: device error: &error];
            if (input == nil || ! [session canAddInput: input])
            {
                [session commitConfiguration];
                auto message = tr ("Couldn't open the camera") + (error != nil ? ": " + toJuce (error.localizedDescription) : juce::String());
                callOnMessageThread ([onDone, message] { onDone (false, message); });
                return;
            }
            [session addInput: input];

            AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
            output.videoSettings = @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) };
            output.alwaysDiscardsLateVideoFrames = YES;
            [output setSampleBufferDelegate: s->delegate queue: s->videoQueue];
            if ([session canAddOutput: output])
                [session addOutput: output];
            [session commitConfiguration];

            CMClockRef clock = nullptr;
            if (@available (macOS 12.3, *))
                clock = session.synchronizationClock;
            else
            {
               #pragma clang diagnostic push
               #pragma clang diagnostic ignored "-Wdeprecated-declarations"
                clock = session.masterClock;
               #pragma clang diagnostic pop
            }
            if (s->syncClock != nullptr)
                CFRelease (s->syncClock);
            s->syncClock = clock != nullptr ? (CMClockRef) CFRetain (clock) : nullptr;

            [session startRunning];
            s->session = session;
            s->isOpen.store (true, std::memory_order_release);

            const auto name = toJuce (device.localizedName);
            callOnMessageThread ([weak, onDone, name]
            {
                if (auto m = weak.lock())
                    m->deviceName = name;
                onDone (true, {});
            });
        });
    };

    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeVideo])
    {
        case AVAuthorizationStatusAuthorized:
            proceed();
            break;

        case AVAuthorizationStatusNotDetermined:
        {
                [AVCaptureDevice requestAccessForMediaType: AVMediaTypeVideo
                                         completionHandler: ^(BOOL granted) {
                                             callOnMessageThread ([proceed, onDone, granted]
                                             {
                                                 if (granted)
                                                     proceed();
                                                 else
                                                     onDone (false, permissionDeniedText());
                                             });
                                         }];
                break;
        }

        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default:
            onDone (false, permissionDeniedText());
            break;
    }
}

void CameraCapture::close()
{
    auto s = impl;
    if (s == nullptr)
        return;

    s->recording.store (false, std::memory_order_release);

    dispatch_sync (s->sessionQueue, ^{
        if (s->session != nil)
        {
            [s->session stopRunning];
            s->session = nil;
        }
        s->micInput = nil;
        s->micOutput = nil;
        s->isOpen.store (false, std::memory_order_release);
    });

    // Let in-flight frame callbacks finish before the writer is dropped.
    dispatch_sync (s->videoQueue, ^{
        if (s->writer != nil)
            [s->writer cancelWriting];
        s->writer = nil;
        s->writerInput = nil;
        s->pendingURL = nil;
    });

    s->deviceName = {};
}

bool CameraCapture::isOpen() const { return impl->isOpen.load (std::memory_order_acquire); }
juce::String CameraCapture::getDeviceName() const { return impl->deviceName; }
int CameraCapture::getFrameWidth() const { return impl->width.load (std::memory_order_relaxed); }
int CameraCapture::getFrameHeight() const { return impl->height.load (std::memory_order_relaxed); }
int CameraCapture::getFrameCounter() const { return impl->frameCounter.load (std::memory_order_acquire); }

juce::Image CameraCapture::getLatestFrame() const
{
    const std::lock_guard<std::mutex> lock (impl->frameMutex);
    return impl->latestFrame;
}

bool CameraCapture::startRecording (const juce::File& movFile)
{
    if (! isOpen())
        return false;

    movFile.getParentDirectory().createDirectory();
    movFile.deleteFile();

    auto s = impl;
    NSURL* url = [NSURL fileURLWithPath: (NSString* _Nonnull) [NSString stringWithUTF8String: movFile.getFullPathName().toRawUTF8()]];

    {
        const std::lock_guard<std::mutex> lock (s->thumbnailMutex);
        s->thumbnails.clear();
    }

    dispatch_async (s->videoQueue, ^{
        s->writer = nil;
        s->writerInput = nil;
        s->pendingURL = url;
        s->firstFrameHost.store (0.0, std::memory_order_relaxed);
        s->lastFrameHost.store (0.0, std::memory_order_relaxed);
        s->thumbnailInterval = 1.0;
        s->recording.store (true, std::memory_order_release);
    });
    return true;
}

void CameraCapture::stopRecording (std::function<void (bool, double)> onFinished)
{
    auto s = impl;

    dispatch_async (s->videoQueue, ^{
        s->recording.store (false, std::memory_order_release);

        AVAssetWriter* finishingWriter = s->writer;
        AVAssetWriterInput* finishingInput = s->writerInput;
        const bool started = s->writerSessionStarted;
        const double first = s->firstFrameHost.load (std::memory_order_acquire);

        s->writer = nil;
        s->writerInput = nil;
        s->pendingURL = nil;
        s->writerSessionStarted = false;

        if (finishingWriter == nil || ! started)
        {
            if (finishingWriter != nil)
                [finishingWriter cancelWriting];
            callOnMessageThread ([onFinished] { onFinished (false, 0.0); });
            return;
        }

        [finishingInput markAsFinished];
        [finishingWriter finishWritingWithCompletionHandler: ^{
            const bool ok = finishingWriter.status == AVAssetWriterStatusCompleted;
            callOnMessageThread ([onFinished, ok, first] { onFinished (ok, first); });
        }];
    });
}

bool CameraCapture::isRecording() const { return impl->recording.load (std::memory_order_acquire); }

double CameraCapture::getRecordedSeconds() const
{
    const double first = impl->firstFrameHost.load (std::memory_order_acquire);
    const double last = impl->lastFrameHost.load (std::memory_order_acquire);
    return first > 0.0 && last > first ? last - first : 0.0;
}

std::vector<juce::Image> CameraCapture::getRecordingThumbnails() const
{
    const std::lock_guard<std::mutex> lock (impl->thumbnailMutex);
    return impl->thumbnails;
}

void CameraCapture::startMicCapture (double seconds, std::function<void (bool, juce::String)> onStarted)
{
    auto s = impl;
    std::weak_ptr<Impl> weak = impl;

    auto proceed = [weak, seconds, onStarted]
    {
        auto self = weak.lock();
        if (self == nullptr)
            return;

        {
            const std::lock_guard<std::mutex> lock (self->micMutex);
            self->micSamples.clear();
            self->micSamples.reserve ((size_t) (seconds * 96000.0));
            self->micTarget = 0;
            self->micWantedSeconds = seconds;
        }

        dispatch_async (self->sessionQueue, ^{
            auto s2 = weak.lock();
            if (s2 == nullptr)
                return;

            if (s2->session == nil)
            {
                callOnMessageThread ([onStarted] { onStarted (false, tr ("Open the camera first.")); });
                return;
            }

            if (s2->micOutput == nil)
            {
                AVCaptureDevice* mic = [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeAudio];
                NSError* error = nil;
                AVCaptureDeviceInput* input = mic != nil ? [AVCaptureDeviceInput deviceInputWithDevice: mic error: &error] : nil;

                AVCaptureAudioDataOutput* output = [[AVCaptureAudioDataOutput alloc] init];
                output.audioSettings = @{
                    AVFormatIDKey: @(kAudioFormatLinearPCM),
                    AVLinearPCMBitDepthKey: @32,
                    AVLinearPCMIsFloatKey: @YES,
                    AVLinearPCMIsNonInterleaved: @NO,
                    AVLinearPCMIsBigEndianKey: @NO,
                    AVNumberOfChannelsKey: @1,
                };
                [output setSampleBufferDelegate: s2->delegate queue: s2->audioQueue];

                [s2->session beginConfiguration];
                const bool ok = input != nil && [s2->session canAddInput: input] && [s2->session canAddOutput: output];
                if (ok)
                {
                    [s2->session addInput: input];
                    [s2->session addOutput: output];
                    s2->micInput = input;
                    s2->micOutput = output;
                }
                [s2->session commitConfiguration];

                if (! ok)
                {
                    callOnMessageThread ([onStarted] { onStarted (false, tr ("The camera's microphone is unavailable.")); });
                    return;
                }
            }

            callOnMessageThread ([onStarted] { onStarted (true, {}); });
        });
    };

    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:
            proceed();
            break;

        case AVAuthorizationStatusNotDetermined:
        {
                [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                                         completionHandler: ^(BOOL granted) {
                                             callOnMessageThread ([proceed, onStarted, granted]
                                             {
                                                 if (granted)
                                                     proceed();
                                                 else
                                                     onStarted (false, micDeniedText());
                                             });
                                         }];
                break;
        }

        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default:
            onStarted (false, micDeniedText());
            break;
    }
}

bool CameraCapture::isMicCaptureComplete() const
{
    const std::lock_guard<std::mutex> lock (impl->micMutex);
    return impl->micTarget > 0 && impl->micSamples.size() >= impl->micTarget;
}

std::vector<float> CameraCapture::getMicCapture (double& firstSampleHostSeconds, double& sampleRate) const
{
    const std::lock_guard<std::mutex> lock (impl->micMutex);
    firstSampleHostSeconds = impl->micFirstHost;
    sampleRate = impl->micRate;
    return impl->micSamples;
}

void CameraCapture::stopMicCapture()
{
    auto s = impl;

    {
        const std::lock_guard<std::mutex> lock (s->micMutex);
        s->micWantedSeconds = 0.0;
    }

    dispatch_async (s->sessionQueue, ^{
        if (s->session == nil || s->micOutput == nil)
            return;

        [s->session beginConfiguration];
        [s->session removeOutput: s->micOutput];
        [s->session removeInput: s->micInput];
        [s->session commitConfiguration];
        s->micOutput = nil;
        s->micInput = nil;
    });
}
