# Windows Benchmarking

Phase 6 introduces the official latency benchmark workflow.

## Components

- `benchmark_pipeline_latency`: synthetic scheduler benchmark with JSON output
- `scripts/windows/benchmark-latency.ps1`: Windows end-to-end benchmark driver
- `scripts/wsl/windows-benchmark.sh`: WSL2 wrapper for the Windows driver
- `benchmarks/regression_thresholds.json`: checked-in latency guardrails
- `scripts/check_realtime_regressions.py`: guardrail checker for local runs and CI

## Artifact Location

Runtime artifacts are written to:

```text
artifacts/benchmarks/<timestamp>/
```

This directory is intentionally ignored by git.

## Recommended Commands

From Windows PowerShell:

```powershell
.\scripts\windows\benchmark-latency.ps1 -BuildFirst
```

From WSL2:

```bash
./scripts/wsl/windows-benchmark.sh --build-first
```

Check a generated benchmark summary against the checked-in latency guardrails:

```bash
python3 scripts/check_realtime_regressions.py \
  --latency-summary artifacts/benchmarks/<timestamp>/summary.json \
  --latency-thresholds benchmarks/regression_thresholds.json
```

## Output

Each run produces:

- `summary.json`
- `summary.md`
- per-mode `run.log`
- per-mode `self-test.log`
- synthetic benchmark JSON files

Current metrics include TTFA p50/p95, ASR p50/p95, TTS p50/p95, clause latency, jitter, underruns, queue drops, and stale cancellations.

## Regression Policy

- `interactive_preview`: `ttfa_audio_p50_ms <= 450`
- `interactive_balanced`: `ttfa_audio_p50_ms <= 650`
- `TTFA p50/p95` should not regress by more than `15%` when a baseline summary is provided
