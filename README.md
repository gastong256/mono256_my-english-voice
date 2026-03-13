# my-english-voice

Local real-time voice pipeline in C++20: captures Spanish speech from a microphone,
translates it to English with Whisper, synthesizes audio with Piper TTS, and routes
it to a virtual microphone device for use in video calls.

**Target OS:** Linux (PipeWire / ALSA loopback). macOS (BlackHole) and Windows
(VB-Cable) are planned; interfaces are designed for it.

---

## Architecture

### Pipeline — 7 threads

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 0 (RT)  Audio Input callback                                     │
│    Only: bounded memcpy → input_ring_   |  NO locks, malloc, or logging  │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ input_ring_ (SPSC, RawAudioBlock)
                             ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 2  Ingest Worker                                                 │
│    VAD chunking (Silero/libfvad) + fixed-window fallback                 │
│    Builds Utterance{source_pcm} → domain prompt → QUEUED_FOR_ASR        │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ ingest_to_asr_ (SPSC, unique_ptr<Utterance>)
                             ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 3  ASR Worker  [GPU priority]                                    │
│    Whisper translate mode (es→en), domain initial_prompt                 │
│    Fills Utterance{translated_text} → COMMITTED                          │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ asr_to_text_ (SPSC, unique_ptr<Utterance>)
                             ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 4  Text Processing Worker                                        │
│    ASR correction → session context update → TTS pronunciation hints     │
│    TTS scheduling: drop_oldest / coalesce / stale detection              │
│    Fills Utterance{normalized_text} → QUEUED_FOR_TTS                    │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ text_to_tts_ (SPSC, unique_ptr<Utterance>)
                             ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 5  TTS Worker  [GPU, fallback CPU on ASR contention]             │
│    Piper TTS (VITS ONNX) → eSpeak fallback                               │
│    Fills Utterance{synth_pcm} → OutputAudioBlocks → tts_to_output_      │
└────────────────────────────┬─────────────────────────────────────────────┘
                             │ tts_to_output_ (SPSC, OutputAudioBlock)
                             ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 1 (RT)  Audio Output callback                                    │
│    Only: bounded dequeue + silence on underrun  |  NO locks or malloc    │
└──────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────┐
│  Thread 6 (low priority)  Supervisor                                     │
│    Watchdog (latency thresholds) · health metrics · degradation trigger  │
│    NOT on the data path                                                  │
└──────────────────────────────────────────────────────────────────────────┘
```

### Degradation chain

| Mode | ASR | TTS | Trigger |
|------|-----|-----|---------|
| **NORMAL** | whisper-small GPU F16 | Piper GPU | Default |
| **DEGRADED** | whisper-small CPU Q5\_1 | Piper CPU | GPU failure or latency > critical_threshold |
| **MINIMAL** | whisper-tiny CPU Q5\_1 | eSpeak-ng | Model load failure |
| **PASSTHROUGH** | — | — | Total failure; raw mic audio routed directly |

### GPU scheduling

ASR has absolute priority over TTS for GPU access (`GpuScheduler`).
When ASR is running, TTS falls back to CPU automatically
(`gpu.tts_cpu_fallback_on_contention = true`).

---

## Dependencies

| Library | Purpose | Status |
|---------|---------|--------|
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | ASR inference | Interface ready; stub active |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Piper TTS / Silero VAD | Interface ready; stub active |
| [Piper TTS](https://github.com/rhasspy/piper) | Primary TTS backend | Stub; enable `MEV_ENABLE_ONNXRUNTIME` |
| [eSpeak-ng](https://github.com/espeak-ng/espeak-ng) | TTS fallback | Stub; enable `MEV_ENABLE_ESPEAK` |
| [PortAudio](http://www.portaudio.com/) | Real audio I/O | Stub; enable `MEV_ENABLE_PORTAUDIO` |
| [libsamplerate](https://libsndfile.github.io/libsamplerate/) | ASR/TTS resampling | TODO; enable `MEV_ENABLE_LIBSAMPLERATE` |
| Silero VAD (ONNX) or [libfvad](https://github.com/dpirch/libfvad) | Voice activity detection | TODO; enable `MEV_ENABLE_WEBRTCVAD` |

C++ standard: **C++20** (requires GCC ≥ 13 or Clang ≥ 16).

---

## Build

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt install cmake ninja-build g++-13 libpthread-stubs0-dev
```

### Configure & build

```bash
# Debug build (tests + benchmarks enabled)
cmake --preset debug
cmake --build --preset debug -j$(nproc)

# Release build with GPU paths enabled
cmake -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DMEV_ENABLE_GPU=ON \
  -DMEV_ENABLE_ONNXRUNTIME=ON \
  -DMEV_ENABLE_PORTAUDIO=ON
cmake --build build/release -j$(nproc)

# Enable profiling macros (adds MEV_PROFILE_SCOPE instrumentation)
cmake --preset debug -DMEV_ENABLE_PROFILING=ON
```

### Tests

```bash
ctest --preset debug --output-on-failure

# Individual test binaries (for debugging)
./build/debug/tests/test_utterance_lifecycle
./build/debug/tests/test_tts_scheduler_policy
./build/debug/tests/test_domain_corrections
./build/debug/tests/test_gpu_scheduler
./build/debug/tests/test_pipeline_shutdown
```

### Benchmarks

```bash
./build/debug/benchmarks/benchmark_queue
./build/debug/benchmarks/benchmark_pipeline_latency
```

---

## Download models

```bash
mkdir -p models

# Whisper small (recommended for MVP)
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin \
     -O models/ggml-small.bin

# Whisper tiny (for MINIMAL degradation mode)
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin \
     -O models/ggml-tiny.bin

# Piper TTS — en-US lessac medium
wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx \
     -O models/en_US-lessac-medium.onnx
wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json \
     -O models/en_US-lessac-medium.onnx.json
```

---

## Virtual microphone setup (Linux)

### Option A — PipeWire (recommended)

```bash
# Create a virtual audio sink that appears as a microphone in apps
pw-loopback \
  --capture-props='media.class=Audio/Sink node.name=VoicePipeline node.description=VoicePipeline' &

# In Google Meet / Zoom: select "VoicePipeline" as the microphone input
```

### Option B — ALSA loopback

```bash
sudo modprobe snd-aloop

# Write synthesized audio to:  hw:Loopback,0,0
# Read (virtual mic) from:     hw:Loopback,1,0
# Set output_device = "hw:Loopback,0,0" in config/pipeline.toml
```

---

## Run

```bash
./build/debug/apps/voice_mic/mev_voice_mic config/pipeline.toml

# Override specific values via config keys in a second file or flags:
./build/debug/apps/voice_mic/mev_voice_mic config/pipeline.toml
# (currently INI-format; full TOML loader planned for v2)
```

The pipeline starts in **NORMAL** mode, runs warmup (dummy ASR + TTS inference),
then opens audio I/O. The `[HEALTH]` log line is emitted every
`telemetry.report_interval_ms` (default 5 s):

```
[INFO]  [HEALTH] mode=NORMAL asr_q=1/128 tts_q=0/8 overruns=0 underruns=3 drops=0 stale=0 asr=42 tts=38
[INFO]  [METRICS] utterance_id=42 total_ms=723.4 asr_ms=412.1 tts_ms=203.5 state=COMPLETED
```

---

## Configuration

Main config: `config/pipeline.toml` — see file comments for all options.

Domain vocabulary: `config/tech_glossary.toml`
- `[corrections]` — ASR post-processing (Whisper mis-transcriptions → correct term)
- `[pronunciation]` — TTS pre-processing (term → how to speak it)
- `[domain_terms]` — Terms injected into Whisper `initial_prompt`

Pronunciation overrides: `config/pronunciation_hints.toml`

---

## Roadmap

| Item | Notes |
|------|-------|
| Real audio I/O | PortAudio backend preserving RT-safe callbacks |
| whisper.cpp ASR | Replace `WhisperAsrStub`; enable partial hypothesis callback |
| Piper TTS | Link ONNX Runtime, implement `PiperTTSEngine::synthesize()` |
| Silero VAD | Replace fixed-window chunking with hybrid VAD + timeout |
| libsamplerate | 16 kHz resampling in Ingest, output resampling in TTS worker |
| XTTSv2 | When stable ONNX export is available (`ITTSEngine` interface ready) |
| macOS support | BlackHole virtual mic + CoreAudio backend |
| Windows support | VB-Cable + WASAPI backend |
| TOML config loader | Replace INI parser with `toml++` FetchContent integration |
| Partial hypotheses | Streaming text output from Whisper for lower perceived latency |
