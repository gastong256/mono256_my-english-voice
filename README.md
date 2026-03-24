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
| [toml++](https://github.com/marzer/tomlplusplus) | Config parser | always on | `find_package`, local source override, or FetchContent |
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | ASR inference | `MEV_ENABLE_WHISPER_CPP` | Local source override or FetchContent v1.7.4 |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Piper TTS | `MEV_ENABLE_ONNXRUNTIME` | Pre-built binary |
| [Piper TTS](https://github.com/rhasspy/piper) | Primary TTS backend | `MEV_ENABLE_ONNXRUNTIME` | ONNX session-backed synthesis with eSpeak fallback |
| [eSpeak-ng](https://github.com/espeak-ng/espeak-ng) | TTS fallback | `MEV_ENABLE_ESPEAK` | Real synthesis |
| [PortAudio](http://www.portaudio.com/) | Real audio I/O | `MEV_ENABLE_PORTAUDIO` | Real I/O |
| [libsamplerate](https://libsndfile.github.io/libsamplerate/) | ASR/TTS resampling | `MEV_ENABLE_LIBSAMPLERATE` | Real resampling |
| [libfvad](https://github.com/dpirch/libfvad) | Voice activity detection | `MEV_ENABLE_WEBRTCVAD` | Local source override or FetchContent |

C++ standard: **C++20** (requires GCC ≥ 13 or Clang ≥ 16).

Runtime contract today:

- `runtime.use_simulated_audio = true` forces simulated audio I/O and is the safe default for local development.
- `runtime.use_simulated_audio = false` requires a build with `MEV_ENABLE_PORTAUDIO=ON`.
- Supported `vad.engine` values are only `none` and `webrtcvad`.
- `silero` is not exposed as a supported backend until a real implementation exists.

### Dependency modes

- `MEV_FETCH_DEPS=ON`: allows CMake to download fetchable dependencies (`toml++`, `whisper.cpp`, `libfvad`).
- `MEV_FETCH_DEPS=OFF`: forbids downloads. Configure fails fast unless dependencies are already installed or provided locally.
- Local source overrides supported by CMake:
  - `-DMEV_TOMLPLUSPLUS_SOURCE_DIR=/abs/path/to/tomlplusplus`
  - `-DMEV_WHISPER_SOURCE_DIR=/abs/path/to/whisper.cpp`
  - `-DMEV_LIBFVAD_SOURCE_DIR=/abs/path/to/libfvad`

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

# VAD — libfvad can be fetched by CMake when
# -DMEV_ENABLE_WEBRTCVAD=ON -DMEV_FETCH_DEPS=ON; no apt package needed.

# whisper.cpp — can be fetched by CMake when
# -DMEV_ENABLE_WHISPER_CPP=ON -DMEV_FETCH_DEPS=ON; no apt package needed.
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
- A checkout on a Windows-visible path, recommended:
  - `C:\dev\my-english-voice`
  - opened from WSL2 as `/mnt/c/dev/my-english-voice`
- PowerShell execution policy that allows local project scripts:
  - `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`

### Windows — Optional backends via vcpkg

- `portaudio:x64-windows`
- `espeak-ng:x64-windows`
- `libsamplerate:x64-windows`

### Windows — ONNX Runtime

The Windows bootstrap script downloads the pinned ONNX Runtime package into `.local/onnxruntime/`.

---

## Build

### Minimal build (stubs only, no external libs required)

```bash
cmake --preset debug
cmake --build --preset debug -j$(nproc)
ctest --preset debug --output-on-failure
```

These Linux presets default to `MEV_FETCH_DEPS=ON` so a fresh checkout can bootstrap fetchable dependencies.

### Minimal build without downloads

```bash
cmake -S . -B build/offline -G Ninja -DMEV_FETCH_DEPS=OFF
```

If required fetchable dependencies are not installed or provided locally, configure fails with an explicit message instead of attempting network access.

### Full build — Linux (all real backends)

```bash
cmake -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DMEV_FETCH_DEPS=ON \
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

### Windows bootstrap

Open PowerShell in the repo root and run:

```powershell
.\scripts\windows\setup-dev.ps1
.\scripts\windows\build.ps1 -Preset windows-msvc-full
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml
.\scripts\windows\run.ps1 -Preset windows-msvc-full -ConfigPath config/pipeline.windows.toml
```

The setup script:

- clones and bootstraps `vcpkg` into `.local/vcpkg` by default
- installs native Windows packages via `vcpkg`
- downloads the pinned ONNX Runtime package into `.local/onnxruntime/`
- downloads pinned models into `models/`
- writes `.local/windows-dev-env.ps1` with `VCPKG_ROOT` and `ONNXRUNTIME_ROOT`

### Windows presets

- `windows-msvc-debug`: Windows debug build with `vcpkg` toolchain and fetchable deps enabled
- `windows-msvc-release`: Windows release build with `vcpkg` toolchain and fetchable deps enabled
- `windows-msvc-full`: enables all current real backend flags
- `windows-msvc-smoke`: lightweight Windows smoke build with `PortAudio`, `Whisper`, and `eSpeak`

All Windows presets read:

- `VCPKG_ROOT`
- `ONNXRUNTIME_ROOT`

### Enable profiling macros

```bash
cmake --preset debug -DMEV_ENABLE_PROFILING=ON
```

### Official workflow — WSL2 driving Windows

Supported daily flow:

1. Place the repo on Windows, for example `C:\dev\my-english-voice`
2. Open it from WSL2 as `/mnt/c/dev/my-english-voice`
3. Edit and run the Linux base suite from WSL2
4. Build the native Windows binary with a WSL wrapper
5. Run the Windows self-test with a WSL wrapper
6. Run the Windows app with a WSL wrapper

Run these from WSL2 only when the repo lives under `/mnt/<drive>/...`:

```bash
# Linux-side fast feedback
cmake --preset debug
cmake --build --preset debug -j$(nproc)
ctest --preset debug --output-on-failure

# Windows build / validate / run
./scripts/wsl/windows-build.sh --preset windows-msvc-full
./scripts/wsl/windows-test.sh --preset windows-msvc-full
./scripts/wsl/windows-self-test.sh --preset windows-msvc-full --config config/pipeline.windows.toml
./scripts/wsl/windows-run.sh --preset windows-msvc-full --config config/pipeline.windows.toml

# Windows CUDA validation path
./scripts/wsl/windows-self-test.sh --preset windows-msvc-full --config config/pipeline.windows.cuda.toml
```

Official smoke path from WSL2:

```bash
./scripts/wsl/windows-smoke.sh
```

That smoke wrapper performs:

1. Windows build with `windows-msvc-smoke`
2. Windows `ctest`
3. Windows `--self-test`
4. A short Windows run using `config/pipeline.windows.smoke.toml`

The WSL2 wrappers:

- validate the checkout path
- convert repo/config paths with `wslpath`
- invoke PowerShell deterministically
- fail fast if the repo is inside `/home` instead of a Windows-visible mount
- accept explicit `--config` where runtime config matters

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

The Windows bootstrap script already downloads the pinned model set via `scripts/windows/setup-dev.ps1`.

Manual download remains available if you need to refresh individual files:

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
2. In `config/pipeline.windows.toml`, set:
   ```toml
   [audio]
   output_device = "CABLE Input"   # substring match against PortAudio device name
   ```
3. In Google Meet / Zoom / Teams, select **CABLE Output** as the microphone.

> **Tip:** Use `--audio.output_device "CABLE Input"` on the CLI to override without editing the TOML.
> For the real Windows path, use `config/pipeline.windows.toml` as the baseline config.

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

# Validate the selected config, models, audio backend, ASR, and TTS
./build/debug/apps/voice_mic/mev_voice_mic --self-test --config config/pipeline.toml

# Validate the Windows real-audio baseline
./build/debug/apps/voice_mic/mev_voice_mic --self-test --config config/pipeline.windows.toml

# List PortAudio devices
./build/debug/apps/voice_mic/mev_voice_mic --list-devices both

# Specify config file
./build/debug/apps/voice_mic/mev_voice_mic --config config/pipeline.toml

# Windows real-audio config
./build/debug/apps/voice_mic/mev_voice_mic --config config/pipeline.windows.toml

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

Main config: `config/pipeline.toml` — parsed by toml++ (resolved via package, local source override, or FetchContent depending on `MEV_FETCH_DEPS`).

Windows real-audio baseline config: `config/pipeline.windows.toml`

Windows smoke config for the lightweight preset: `config/pipeline.windows.smoke.toml`

Windows CUDA validation config: `config/pipeline.windows.cuda.toml`

- `runtime.use_simulated_audio = false`
- `audio.output_device = "CABLE Input"`
- `asr.model_path = "models/ggml-small.bin"`
- `asr.gpu_enabled = false`
- `asr.quantization = "q5_1"`
- `tts.engine = "piper"`
- `tts.fallback_engine = "espeak"`
- low-latency rolling ASR defaults now use `chunk_ms = 120` and `hop_ms = 60`

ASR partial behavior today:

- `vad.engine = "none"` uses rolling overlapping chunks tuned for lower text TTFT
- the ASR worker emits stable partial deltas before end-of-utterance when overlap confirms them
- final chunks still flush the remaining text with `end_of_utterance = true`

ASR CPU/GPU behavior today:

- CPU is the supported baseline for `config/pipeline.windows.toml`
- GPU is optional
- if `asr.gpu_enabled = true` and ASR warmup fails, the pipeline retries on CPU when `resilience.gpu_failure_action = "fallback_cpu"`

TTS behavior today:

- `piper` is the primary backend for `config/pipeline.windows.toml`
- `espeak` remains the required fallback backend
- if the primary TTS path fails during synthesis, the pipeline degrades and retries with `espeak`
- `config/pipeline.windows.smoke.toml` uses `espeak` as both primary and fallback so the smoke preset does not require ONNX Runtime
- `config/pipeline.windows.cuda.toml` requests GPU for both ASR and Piper so `--self-test` can report effective GPU/CPU placement on Windows

Self-test diagnostics today:

- `--self-test` reports the selected audio backend
- when GPU is requested, it reports CUDA driver runtime visibility
- when Piper GPU is requested on Windows, it reports whether `onnxruntime_providers_cuda.dll` is visible
- ASR and TTS backends report requested vs effective placement (`cuda` or `cpu`)

Current supported VAD values:

- `none`: fixed-window chunking path
- `webrtcvad`: requires `MEV_ENABLE_WEBRTCVAD=ON`

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
--audio.output_device "CABLE Input"
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
| piper-phonemize parity | Optional future upgrade for broader Piper voice compatibility beyond the current eSpeak/text phonemization path |
