# Audio / video sync

How the guitar (audio interface) and the picture (webcam) stay aligned.

- The guitar goes through the audio interface, the picture through the webcam: two clocks, two latencies. Roadie timestamps every audio block with CoreAudio's host time (`hostTimeNs`, `mach_absolute_time`) and every frame with its AVFoundation PTS converted onto the same host clock (`CMSyncConvertTime` from the session clock). The video's placement in the export follows directly from these two timestamps (`Take::getVideoOffsetSeconds`).
- What the estimate can't see (real converter latency, CoreAudio's safety margin) is **measured** by the calibration: Roadie listens for 5 s to the audio interface's input and the camera's microphone at the same time (timestamped by AVFoundation like the frames); the same attack heard on both sides gives the offset (`SyncAnalysis::measureLag`, accuracy < 1 ms in tests). It is remembered and applied to every take.
- A clap isn't picked up by a guitar input (guitar pickup): hence the instruction "one dry, muted chord", which the audio interface receives through the guitar and the camera's microphone hears acoustically.
