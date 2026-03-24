# Windows Benchmarking

Phase 6 introduces the official latency benchmark workflow.

## Components

- `benchmark_pipeline_latency`: synthetic scheduler benchmark with JSON output
- `scripts/windows/benchmark-latency.ps1`: Windows end-to-end benchmark driver
- `scripts/wsl/windows-benchmark.sh`: WSL2 wrapper for the Windows driver

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

## Output

Each run produces:

- `summary.json`
- `summary.md`
- per-mode `run.log`
- per-mode `self-test.log`
- synthetic benchmark JSON files

Current metrics include TTFA p50/p95, ASR p50/p95, TTS p50/p95, clause latency, jitter, underruns, queue drops, and stale cancellations.
