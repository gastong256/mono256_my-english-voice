# my-english-voice

Local real-time voice pipeline in C++20: captures Spanish speech from a microphone,
translates it to English with Whisper, synthesizes audio with Piper TTS, and routes
it to a virtual microphone device for use in video calls.

**Target OS:** Linux (PipeWire / ALSA loopback) and Windows 11 (VB-Cable / WASAPI).
macOS (BlackHole) is planned.

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
│    VAD chunking (libfvad) + fixed-window fallback                        │
│    Resamples mic→16kHz via libsamplerate (when enabled)                  │
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
│    Resamples TTS→output_rate via libsamplerate (when enabled)            │
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

| Library | Purpose | Flag | Status |
|---------|---------|------|--------|
| [toml++](https://github.com/marzer/tomlplusplus) | Config parser | always on (FetchContent) | Active |
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | ASR inference | `MEV_ENABLE_WHISPER_CPP` | FetchContent v1.7.4 |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Piper TTS | `MEV_ENABLE_ONNXRUNTIME` | Pre-built binary |
| [Piper TTS](https://github.com/rhasspy/piper) | Primary TTS backend | `MEV_ENABLE_ONNXRUNTIME` | Stub until ONNX linked |
| [eSpeak-ng](https://github.com/espeak-ng/espeak-ng) | TTS fallback | `MEV_ENABLE_ESPEAK` | Real synthesis |
| [PortAudio](http://www.portaudio.com/) | Real audio I/O | `MEV_ENABLE_PORTAUDIO` | Real I/O |
| [libsamplerate](https://libsndfile.github.io/libsamplerate/) | ASR/TTS resampling | `MEV_ENABLE_LIBSAMPLERATE` | Real resampling |
| [libfvad](https://github.com/dpirch/libfvad) | Voice activity detection | `MEV_ENABLE_WEBRTCVAD` | FetchContent |

C++ standard: **C++20** (requires GCC ≥ 13 or Clang ≥ 16).

---

## System dependencies

### Linux — Required

```bash
# Ubuntu/Debian
sudo apt install cmake ninja-build g++-13 libpthread-stubs0-dev
```

### Linux — Optional backends

```bash
# Audio I/O (for -DMEV_ENABLE_PORTAUDIO=ON)
sudo apt install libportaudio2 portaudio19-dev

# TTS fallback (for -DMEV_ENABLE_ESPEAK=ON)
sudo apt install libespeak-ng-dev

# Resampling (for -DMEV_ENABLE_LIBSAMPLERATE=ON)
sudo apt install libsamplerate0-dev

# VAD — libfvad is fetched automatically via FetchContent when
# -DMEV_ENABLE_WEBRTCVAD=ON; no apt package needed.

# whisper.cpp — fetched automatically via FetchContent when
# -DMEV_ENABLE_WHISPER_CPP=ON; no apt package needed.
```

### Linux — ONNX Runtime (for Piper TTS with -DMEV_ENABLE_ONNXRUNTIME=ON)

```bash
wget https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-linux-x64-gpu-1.17.3.tgz
tar xzf onnxruntime-linux-x64-gpu-1.17.3.tgz
export ONNXRUNTIME_ROOT=$(pwd)/onnxruntime-linux-x64-gpu-1.17.3
```

### Windows — Required

- **Visual Studio 2022** (MSVC v143) or later, with the **C++ CMake tools** workload
- **CMake ≥ 3.25** and **Ninja** (both bundled with VS2022)
- **vcpkg** (recommended for native libs):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
# Integrate with MSBuild/CMake
C:\vcpkg\vcpkg integrate install
```

### Windows — Optional backends via vcpkg

```powershell
# Audio I/O
vcpkg install portaudio:x64-windows

# TTS fallback
vcpkg install espeak-ng:x64-windows

# Resampling
vcpkg install libsamplerate:x64-windows
```

### Windows — ONNX Runtime

Download the pre-built package from GitHub Releases:

```powershell
# GPU build (CUDA 12)
Invoke-WebRequest -Uri https://github.com/microsoft/onnxruntime/releases/download/v1.17.3/onnxruntime-win-x64-gpu-1.17.3.zip -OutFile onnxruntime.zip
Expand-Archive onnxruntime.zip -DestinationPath .
$env:ONNXRUNTIME_ROOT = "$PWD\onnxruntime-win-x64-gpu-1.17.3"
```

---

## Build

### Minimal build (stubs only, no external libs required)

```bash
cmake --preset debug
cmake --build --preset debug -j$(nproc)
ctest --preset debug --output-on-failure
```

### Full build — Linux (all real backends)

```bash
cmake -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DMEV_ENABLE_GPU=ON \
  -DMEV_ENABLE_PORTAUDIO=ON \
  -DMEV_ENABLE_WHISPER_CPP=ON \
  -DMEV_ENABLE_ONNXRUNTIME=ON \
  -DMEV_ENABLE_ESPEAK=ON \
  -DMEV_ENABLE_LIBSAMPLERATE=ON \
  -DMEV_ENABLE_WEBRTCVAD=ON \
  -DONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}
cmake --build build/release -j$(nproc)
```

### Full build — Windows (MSVC + vcpkg)

Open a **Developer PowerShell for VS 2022**, then:

```powershell
cmake -B build\release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DMEV_ENABLE_GPU=ON `
  -DMEV_ENABLE_PORTAUDIO=ON `
  -DMEV_ENABLE_WHISPER_CPP=ON `
  -DMEV_ENABLE_ONNXRUNTIME=ON `
  -DMEV_ENABLE_ESPEAK=ON `
  -DMEV_ENABLE_LIBSAMPLERATE=ON `
  -DMEV_ENABLE_WEBRTCVAD=ON `
  -DONNXRUNTIME_ROOT="$env:ONNXRUNTIME_ROOT"
cmake --build build\release
```

`onnxruntime.dll` is copied automatically next to the executable.

### Selective feature build

```bash
# PortAudio + eSpeak only (no GPU)
cmake -B build/audio \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMEV_ENABLE_PORTAUDIO=ON \
  -DMEV_ENABLE_ESPEAK=ON \
  -DMEV_ENABLE_LIBSAMPLERATE=ON
cmake --build build/audio -j$(nproc)
```

### Enable profiling macros

```bash
cmake --preset debug -DMEV_ENABLE_PROFILING=ON
```

### Tests

```bash
ctest --preset debug --output-on-failure

# Individual test binaries (for debugging)
./build/debug/tests/test_toml_config
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

## Virtual microphone setup

### Windows — VB-Cable (recommended)

1. Download and install [VB-Audio Virtual Cable](https://vb-audio.com/Cable/).
2. In `config/pipeline.toml`, set:
   ```toml
   [audio]
   output_device = "CABLE Input"   # substring match against PortAudio device name
   ```
3. In Google Meet / Zoom / Teams, select **CABLE Output** as the microphone.

> **Tip:** Use `--audio.output_device "CABLE Input"` on the CLI to override without editing the TOML.

### Linux — PipeWire (recommended)

```bash
# Create a virtual audio sink that appears as a microphone in apps
pw-loopback \
  --capture-props='media.class=Audio/Sink node.name=VoicePipeline node.description=VoicePipeline' &

# In config/pipeline.toml:
#   output_device = "VoicePipeline"

# In Google Meet / Zoom: select "VoicePipeline" as the microphone input
```

### Linux — PulseAudio null-sink (WSL2 / older systems)

```bash
# Load a null sink and expose its monitor as a source
pactl load-module module-null-sink sink_name=VoicePipeline \
    sink_properties=device.description=VoicePipeline
pactl load-module module-virtual-source source_name=VoicePipelineMic \
    master=VoicePipeline.monitor

# In config/pipeline.toml:
#   output_device = "VoicePipeline"
# In Google Meet / Zoom: select "VoicePipelineMic"
```

### Linux — ALSA loopback

```bash
sudo modprobe snd-aloop

# Write synthesized audio to:  hw:Loopback,0,0
# Read (virtual mic) from:     hw:Loopback,1,0
# Set output_device = "hw:Loopback,0,0" in config/pipeline.toml
```

---

## Run

```bash
# Basic run with default config
./build/debug/apps/voice_mic/mev_voice_mic

# Specify config file
./build/debug/apps/voice_mic/mev_voice_mic --config config/pipeline.toml

# Override individual fields after TOML load
./build/debug/apps/voice_mic/mev_voice_mic \
  --config config/pipeline.toml \
  --tts.engine espeak \
  --runtime.run_duration_seconds 60

# Print help
./build/debug/apps/voice_mic/mev_voice_mic --help
```

The pipeline starts in **NORMAL** mode, runs warmup (dummy ASR + TTS inference),
then opens audio I/O. The `[HEALTH]` log line is emitted every
`telemetry.report_interval_ms` (default 5 s):

```
[INFO]  [HEALTH] mode=NORMAL asr_q=1/128 tts_q=0/8 overruns=0 underruns=3 drops=0 stale=0 asr=42 tts=38
[INFO]  [METRICS] utterance_id=42 total_ms=723.4 asr_ms=412.1 tts_ms=203.5 state=COMPLETED
```

Press **Ctrl+C** to stop cleanly (SIGINT triggers graceful shutdown).

---

## Configuration

Main config: `config/pipeline.toml` — parsed by toml++ (always linked via FetchContent).

Domain vocabulary: `config/tech_glossary.toml`
- `[corrections]` — ASR post-processing (Whisper mis-transcriptions → correct term)
- `[pronunciation]` — TTS pre-processing (term → how to speak it)
- `[domain_terms]` — Terms injected into Whisper `initial_prompt`

Pronunciation overrides: `config/pronunciation_hints.toml`

---

## CLI overrides

Any config field can be overridden after TOML load with `--<section>.<key> <value>`:

```bash
--audio.input_device "HDA Intel PCH"
--audio.sample_rate_hz 48000
--asr.model_path models/ggml-tiny.bin
--tts.engine espeak
--tts.enable_gpu false
--vad.engine webrtcvad
--runtime.run_duration_seconds 120
--logging.level debug
```

---

## Roadmap

| Item | Notes |
|------|-------|
| XTTSv2 | When stable ONNX export is available (`ITTSEngine` interface ready) |
| macOS support | BlackHole virtual mic + CoreAudio backend |
| Partial hypotheses | Streaming text output from Whisper for lower perceived latency |
| piper-phonemize | Accurate text-to-phoneme conversion for Piper TTS (currently ASCII placeholder) |
