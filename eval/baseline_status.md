# Baseline Status

Date: 2026-03-24

## Oracle Sanity Baseline

The checked-in tooling supports an oracle sanity run where `reference_en` is scored against itself.

Expected oracle metrics:

- exact match rate: `1.0`
- avg token F1: `1.0`
- avg required term coverage: `1.0`

This sanity baseline only validates the dataset and scoring pipeline.

## Real Model Baseline

The representative baseline for Phase 3 still needs to be produced with the real Windows run path:

1. build `windows-msvc-full`
2. run the app or an offline batch harness with Whisper translate enabled
3. save predictions as JSONL
4. score them with `eval/score_domain_eval.py`
5. complete manual labeling using `eval/LABELING.md`

Until that run exists, no checked-in `A/B` production-quality score should be claimed.
