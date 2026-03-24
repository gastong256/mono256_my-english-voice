# Windows Baseline

Date: 2026-03-24

## Status

The benchmark workflow for Windows is implemented, but this repository does not check in machine-specific latency numbers.

Machine-specific benchmark numbers should stay local or be attached as workflow artifacts.
The checked-in contract is stored in `benchmarks/regression_thresholds.json`.

Generate a local baseline with:

```powershell
.\scripts\windows\benchmark-latency.ps1 -BuildFirst
```

or from WSL2:

```bash
./scripts/wsl/windows-benchmark.sh --build-first
```

## Required Profiles

- `interactive_preview`: `config/pipeline.windows.preview.toml`
- `interactive_balanced`: `config/pipeline.windows.toml`

## Acceptance Targets

- `TTFA_audio_p50 <= 450 ms` in `interactive_preview`
- `TTFA_audio_p50 <= 650 ms` in `interactive_balanced`

## Notes

- `gpu_busy_time_ms` is still reported as `null` until a dedicated GPU occupancy probe is added.
- The benchmark script writes local artifacts under `artifacts/benchmarks/`.
- Compare a measured run with `python3 scripts/check_realtime_regressions.py --latency-summary ... --latency-thresholds benchmarks/regression_thresholds.json`.
