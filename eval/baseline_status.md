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

## Phase 7 Conversational Acceptance

For the realtime-perceived track, acceptance is tied to conversational usefulness, not
perfect grammar. The practical targets are:

- `B1 acceptable`: minimum useful floor for constrained live dialog
- `B2 good`: stable target for the interactive modes
- `C1 very good in-domain`: aspirational, not required globally

When a real Windows baseline is produced, the review summary should explicitly report:

- `manual_b1_or_better_rate`
- `manual_b2_or_better_rate`
- `manual_c1_rate`
- `manual_error_counts`
- `manual_fix_bucket_counts`

The first candidate to call the experiment usable in real dialog should satisfy:

- `manual_b1_or_better_rate >= 0.85`
- `manual_b2_or_better_rate >= 0.60`

Any dominant error category should be linked back to a concrete fix bucket before the
next latency or quality iteration is planned.

The checked-in quality guardrails live in `eval/conversational_thresholds.json` and are
intended to be enforced with `scripts/check_realtime_regressions.py` once a reviewed
summary exists.
