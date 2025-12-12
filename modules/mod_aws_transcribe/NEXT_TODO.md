# Next Todo Items (2025-12-11)

## Pending Optimizations
- Integrate AwsClientManager in AwsInternalPipe::connect to reuse thread-local Transcribe clients and eliminate per-session client creation.
- Extend the object pool to cover AwsInternalPipe instances and recycle WorkerJob objects to restore the zero-allocation hot path promised in the architecture docs.
- Honor MOD_AWS_WORKER_THREADS and reintroduce adaptive backoff tuning (10µs–1ms window) in worker_thread.cpp so operational behavior matches the documented guidance.
- Expose real memory-pool metrics in uuid_aws_transcribe ... stats output and wire startOnVad/metadata options into the AWS request lifecycle for feature completeness.

## Still Present
- Lock-free SPSC ring buffer with zero-copy producer writes remains the backbone of the audio pipeline.
- Async HTTP/Pusher C port retains MD5/HMAC signing and proper JSON escaping for webhook delivery.
- Session-start tracking via SIP Call-ID keeps event parity with the Deepgram implementation.
- Delayed teardown in worker_thread keeps AWS callbacks safe during disconnects.

## Verification Steps
1. Apply each code change behind feature flags where practical to allow phased rollout.
2. Re-run module load/unload and live call tests after integrating the client manager to confirm no regression in AWS authentication.
3. Stress-test with synthetic load to validate that adaptive backoff and pooled structures reduce CPU usage and allocation churn as expected.
