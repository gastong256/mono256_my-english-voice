# Windows 11 Runbook

Operational guide for setting up, validating, and running `my-english-voice` on Windows 11.

Covers: fresh checkout → dependency bootstrap → build → self-test → first runs → CUDA
validation → latency benchmarking → conversational quality → end-to-end VB-Cable session.

---

## 1. Prerequisites

Complete all of these before running any repo script.

### 1.1 Toolchain

**Visual Studio 2022** (Community, Professional, or Build Tools) with the following workload
components:

- MSVC v143 (C++ compiler)
- C++ CMake tools for Windows
- Ninja build system
- Windows 10/11 SDK

Verify in a **Developer PowerShell for VS 2022** (or any shell with `vcvars64.bat` sourced):

```powershell
cmake --version    # must be >= 3.25
ninja --version
cl               # MSVC compiler
git --version
```

**Git for Windows** — required for repo operations and vcpkg bootstrap.

### 1.2 PowerShell execution policy

Allow local project scripts to run:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

### 1.3 NVIDIA GPU driver

Required only for the CUDA validation path. Skip if running CPU-only.

```powershell
nvidia-smi
```

Expected output includes driver version and CUDA version. ONNX Runtime v1.22.0 requires
**CUDA 12.x** driver support. If `nvidia-smi` fails, fix the driver before attempting any
CUDA validation step.

### 1.4 VB-Audio Virtual Cable

Download from [vb-audio.com/Cable](https://vb-audio.com/Cable/) and install. Reboot if the
installer requests it.

After reboot, open **Sound Settings → Manage sound devices** and confirm these devices exist:

- `CABLE Input` (playback device — the pipeline writes audio here)
- `CABLE Output` (recording device — your video call app reads from here)

Do not proceed to real-audio runs until both devices are visible.

### 1.5 Python 3.8+

Required only for benchmark regression checks and conversational quality evaluation.
Not needed for the build or runtime.

---

## 2. Repo Checkout Location

The repo **must** live on a Windows-visible path. The bootstrap scripts validate this and
will fail on unsupported paths.

Recommended:

```text
C:\dev\my-english-voice
```

If developing from WSL2, check out under a Windows-mounted drive and open via the mount:

```text
WSL2: /mnt/c/dev/my-english-voice
```

Avoid:
- Paths inside `C:\Users\<you>\Downloads\` or similar transient locations
- OneDrive-synced folders (aggressive sync interferes with build artifacts)
- Any path only visible from Linux (`/home/...`) when using WSL2→Windows wrappers

---

## 3. Bootstrap

From the repo root in **PowerShell**:

```powershell
.\scripts\windows\setup-dev.ps1
```

This script:

1. Clones (or reuses) `vcpkg` into `.local\vcpkg`
2. Installs pinned vcpkg packages: `portaudio:x64-windows`, `libsamplerate:x64-windows`,
   and optionally `espeak-ng:x64-windows`
3. Downloads pinned ONNX Runtime (v1.22.0, GPU build) into `.local\onnxruntime\`
4. Downloads pinned model weights into `models\`:
   - `ggml-small.bin` — Whisper small FP16 (GPU inference)
   - `ggml-small-q5_1.bin` — Whisper small Q5\_1 (CPU baseline, ~2x faster than FP16 on CPU)
   - `ggml-tiny.bin` — Whisper tiny FP16 (GPU fallback)
   - `ggml-tiny-q5_1.bin` — Whisper tiny Q5\_1 (MINIMAL degradation mode on CPU)
   - `en_US-lessac-medium.onnx` + `.onnx.json` — Piper TTS voice model
5. Writes `.local\windows-dev-env.ps1` with `VCPKG_ROOT` and `ONNXRUNTIME_ROOT`

Source the environment helper after setup completes:

```powershell
. .\.local\windows-dev-env.ps1
```

**eSpeak note:** `espeak-ng:x64-windows` is optional in the vcpkg manifest. If the active
registry does not provide it, bootstrap continues and leaves the rest of the host ready.
To enable eSpeak manually:

1. Download the installer from [espeak-ng releases](https://github.com/espeak-ng/espeak-ng/releases)
2. Install to the default path (`C:\Program Files\eSpeak NG`)
3. Before building: `$env:ESPEAK_ROOT = "C:\Program Files\eSpeak NG"`

---

## 4. Build

### Full build (all backends, recommended for all real-hardware runs)

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-full
```

Enables: PortAudio, Whisper, ONNX Runtime/Piper, eSpeak, libsamplerate, WebRTC VAD, GPU.

### GPU debug build (for diagnosing CUDA issues)

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-gpu-debug
```

Same backends as `windows-msvc-full` but compiled in Debug mode. Use when `--self-test`
shows unexpected CPU fallback and you need to trace the failure path.

### Smoke build (lightweight, no ONNX Runtime)

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-smoke
```

Useful for isolating audio and ASR problems without the Piper/ONNX dependency.

### CI build (reproduces GitHub Actions contract)

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-ci
```

Whisper + benchmarks enabled, no eSpeak dependency. Matches what the CI pipeline runs.

---

## 5. Validation Sequence

Execute in this exact order. Do not skip steps.

### 5.1 Unit and integration tests

```powershell
.\scripts\windows\test.ps1 -Preset windows-msvc-full
```

All tests must pass. A failure here indicates a build or dependency issue that will
manifest at runtime. Fix before continuing.

### 5.2 Device enumeration

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.toml `
  -AppArgs @('--list-devices','both')
```

Confirm:
- Your physical microphone appears in the input device list
- A device matching `CABLE Input` appears in the output device list

If `CABLE Input` is absent, reinstall VB-Cable and reboot before proceeding.

### 5.3 Self-test — CPU baseline

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.preview.toml
```

Expected: config valid, models found, audio backend selected, ASR (Whisper stub or real)
and TTS (eSpeak) initialize without error.

### 5.4 Self-test — balanced (Piper TTS)

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.toml
```

Expected: Piper TTS initializes with the ONNX Runtime CPU provider. If initialization
fails, the error message will identify whether the model file is missing, the ONNX Runtime
root is unset, or the session fails to load.

### 5.5 Self-test — CUDA placement validation

```powershell
.\scripts\windows\self-test.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.cuda.toml
```

Look for these lines in the output:

```
[INFO] ASR requested_device=cuda effective=cuda
[INFO] TTS requested_device=cuda effective=cuda
[INFO] CUDA driver visible: true
[INFO] onnxruntime_providers_cuda.dll visible: true
```

If either backend shows `effective=cpu` with a reason, address it before running the CUDA
production config. The most common causes are listed in the troubleshooting section.

---

## 6. First Runs

Progress from least risk to most risk.

### 6.1 Preview mode (eSpeak TTS, lowest latency)

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.preview.toml `
  -AppArgs @('--runtime.run_duration_seconds','20')
```

- ASR: Whisper small Q5\_1 on CPU
- TTS: eSpeak (synthesizes immediately, no ONNX overhead)
- Expected latency: TTFA ≤ 450 ms p50

Observe in the console during the 20-second run:

```
[INFO] pipeline running — input=portaudio_input asr=whisper.cpp tts=espeak mode=NORMAL
[INFO] [HEALTH] mode=NORMAL asr_q=1/32 tts_q=0/8 overruns=0 underruns=0 drops=0
[INFO] [METRICS] utterance_id=3 total_ms=380.1 asr_ms=290.4 tts_ms=62.1 speech_chunks=2
```

Flag as failing if: crash, persistent `output_underruns` > 200, `queue_drops` > 0.

### 6.2 Balanced mode (Piper TTS, production quality)

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.toml `
  -AppArgs @('--runtime.run_duration_seconds','20')
```

- ASR: Whisper small Q5\_1 on CPU
- TTS: Piper ONNX on CPU, eSpeak fallback when primary budget exceeded
- Expected latency: TTFA ≤ 650 ms p50

### 6.3 CUDA mode (GPU acceleration)

Only run after 6.1 and 6.2 complete cleanly.

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.cuda.toml `
  -AppArgs @('--runtime.run_duration_seconds','20')
```

- ASR: Whisper small FP16 on CUDA
- TTS: Piper ONNX on CUDA
- Expected: `mode=NORMAL`, `asr_ms` significantly lower than CPU baseline

Confirm GPU is active in logs:

```
[INFO] pipeline running — input=portaudio_input asr=whisper.cpp[cuda:0] tts=piper[cuda:0]
```

### 6.4 Production runs (infinite runtime)

After all short runs pass, switch to the production configs which run until `Ctrl+C`:

```powershell
# CPU production
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.production.toml

# CUDA production
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.cuda.production.toml
```

Stop with `Ctrl+C`. Expect:

```
[INFO] SIGINT/SIGTERM received — shutting down
[INFO] shutdown metrics queue_drops=0 output_underruns=... stale_cancelled=...
```

---

## 7. Latency Benchmarking

### 7.1 Run the benchmark

```powershell
.\scripts\windows\benchmark-latency.ps1 -BuildFirst
```

Artifacts are written to `artifacts\benchmarks\<timestamp>\`. The benchmark runs:

1. A synthetic scheduler simulation (`benchmark_pipeline_latency`)
2. A short real-app session for `interactive_preview` mode
3. A short real-app session for `interactive_balanced` mode

### 7.2 With simulated audio (isolates device variability)

```powershell
.\scripts\windows\benchmark-latency.ps1 `
  -AppArgs @('--runtime.use_simulated_audio','true')
```

Use this when comparing across machines or when device jitter is suspected.

### 7.3 Validate against guardrails

```powershell
python .\scripts\check_realtime_regressions.py `
  --latency-summary artifacts\benchmarks\<timestamp>\summary.json `
  --latency-thresholds benchmarks\regression_thresholds.json
```

Current acceptance thresholds:

| Mode | TTFA audio p50 |
|------|---------------|
| `interactive_preview` | ≤ 450 ms |
| `interactive_balanced` | ≤ 650 ms |

Exit code 0 means all guardrails pass. Archive the accepted `summary.json` as the first
validated benchmark:

```powershell
mkdir benchmarks\validated -Force
cp artifacts\benchmarks\<timestamp>\summary.json benchmarks\validated\windows-first-run.json
```

---

## 8. Conversational Quality Evaluation

### 8.1 Collect predictions

Run a session where you speak each Spanish sentence from `eval/domain_realtime_set.jsonl`
into the microphone. Capture the `translated_text` field from log lines matching
`[METRICS] utterance_id=...` and write them to:

```text
eval\predictions_v1.jsonl
```

Format per line: `{"id": "<sample_id>", "prediction_en": "<translated text>"}`

### 8.2 Score and generate review CSV

```powershell
python .\eval\score_domain_eval.py `
  --dataset .\eval\domain_realtime_set.jsonl `
  --predictions .\eval\predictions_v1.jsonl `
  --emit-review-csv .\eval\manual_review_v1.csv
```

### 8.3 Manual labeling

Open `eval\manual_review_v1.csv` and rate each row following `eval\LABELING.md`.
Quality labels: `A` (native-equivalent), `B2` (good), `B1` (acceptable), `B0` (broken).

### 8.4 Compute summary and validate

```powershell
python .\eval\score_domain_eval.py `
  --dataset .\eval\domain_realtime_set.jsonl `
  --predictions .\eval\predictions_v1.jsonl `
  --manual-labels .\eval\manual_review_v1.csv `
  --summary-out .\eval\manual_review_summary_v1.json

python .\scripts\check_realtime_regressions.py `
  --quality-summary .\eval\manual_review_summary_v1.json `
  --quality-thresholds .\eval\conversational_thresholds.json
```

Acceptance criteria:

| Metric | Threshold |
|--------|-----------|
| `manual_b1_or_better_rate` | ≥ 0.85 |
| `manual_b2_or_better_rate` | ≥ 0.60 |

Archive the accepted summary:

```powershell
cp .\eval\manual_review_summary_v1.json .\eval\baseline_v1.json
```

---

## 9. End-to-End VB-Cable Validation

### 9.1 Pipeline configuration

All production configs have `output_device = "CABLE Input"` pre-set. Confirm with:

```powershell
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.cuda.production.toml `
  -AppArgs @('--list-devices','both')
```

The device name is matched by substring. If your VB-Cable appears as `CABLE Input (VB-Audio
Virtual Cable)`, the substring `CABLE Input` will still match.

### 9.2 Video call application setup

In Google Meet / Zoom / Microsoft Teams:

1. Open audio settings
2. Set the **microphone** to **CABLE Output** (not your physical mic)
3. Keep your physical mic as the input **inside the pipeline app**

Flow: physical mic → pipeline → VB-Cable Input → VB-Cable Output → video call app.

### 9.3 Test procedure

1. Start a test call (self-call, Google Meet preview, or Zoom test)
2. Launch the pipeline:
   ```powershell
   .\scripts\windows\run.ps1 -Preset windows-msvc-full `
     -ConfigPath config/pipeline.windows.cuda.production.toml
   ```
3. Speak a Spanish sentence
4. Confirm the call receives synthesized English audio (not your raw voice)
5. Observe pipeline logs for `[METRICS]` lines confirming completed utterances

---

## 10. Troubleshooting

### `RepoRoot must be a Windows path`

The bootstrap script detected a non-Windows path. Move the repo to `C:\dev\my-english-voice`
and re-run from there.

### `ASR model path not found`

Models are missing from `models\`. Re-run:

```powershell
.\scripts\windows\setup-dev.ps1
```

### `Executable not found`

The requested preset has not been built. Build it first:

```powershell
.\scripts\windows\build.ps1 -Preset windows-msvc-full
```

### `CABLE Input` not visible in device list

VB-Cable is not installed or the device is not yet registered by Windows.

1. Reinstall VB-Cable
2. Reboot
3. Re-run `--list-devices both`

### Self-test reports `effective=cpu` for CUDA backends

Ordered diagnostic steps:

1. `nvidia-smi` — confirms driver is functional and CUDA version is 12.x+
2. Check `ONNXRUNTIME_ROOT` points to the correct directory:
   ```powershell
   ls $env:ONNXRUNTIME_ROOT\lib\onnxruntime_providers_cuda.dll
   ```
3. Update NVIDIA driver to the latest Game Ready or Studio release
4. Confirm Visual C++ 2022 Redistributable is installed
5. Re-run self-test with the debug build for a more detailed error trace:
   ```powershell
   .\scripts\windows\self-test.ps1 -Preset windows-msvc-gpu-debug `
     -ConfigPath config/pipeline.windows.cuda.toml
   ```

The pipeline falls back to CPU automatically via `gpu_failure_action = "fallback_cpu"`.
CUDA is not required for the pipeline to function — only for meeting the low-latency targets.

### Audio glitches or gaps

Isolate device vs. pipeline:

```powershell
# Run with simulated audio to remove device from the equation
.\scripts\windows\run.ps1 -Preset windows-msvc-full `
  -ConfigPath config/pipeline.windows.production.toml `
  -AppArgs @('--runtime.use_simulated_audio','true','--runtime.run_duration_seconds','30')
```

If simulated audio is clean, the issue is device-side:

- Disable audio enhancements in Windows Sound Properties for both input and output devices
- Set VB-Cable sample rate to 16000 Hz or 44100 Hz (match the pipeline config)
- Check `output_underruns` in `[HEALTH]` logs — high values indicate the TTS queue is not
  being filled fast enough (CPU too slow → use CUDA config)

---

## 11. Success Criteria

The Windows host is production-ready when all of the following hold:

| Check | Command | Expected result |
|-------|---------|-----------------|
| Bootstrap | `setup-dev.ps1` | Completes without error |
| Build | `build.ps1 -Preset windows-msvc-full` | Exit 0 |
| Tests | `test.ps1 -Preset windows-msvc-full` | 100% pass |
| Devices | `--list-devices both` | Physical mic + CABLE Input visible |
| Self-test CPU | `self-test.ps1 ... preview.toml` | PASS |
| Self-test balanced | `self-test.ps1 ... windows.toml` | PASS |
| Self-test CUDA | `self-test.ps1 ... cuda.toml` | Both backends `effective=cuda` |
| Preview run | 20s run with preview config | No crash, TTFA visible in logs |
| Balanced run | 20s run with windows.toml | `speech_chunks` in METRICS logs |
| CUDA run | 20s run with cuda.toml | `asr=whisper.cpp[cuda:0]` in logs |
| Benchmark | `benchmark-latency.ps1` | `summary.json` produced |
| Guardrails | `check_realtime_regressions.py` | Exit 0 |
| VB-Cable | Video call receives audio | Synthesized English, not raw voice |
