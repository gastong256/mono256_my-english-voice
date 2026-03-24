# Domain Realtime Labeling Guide

Use this rubric for manual review of `eval/domain_realtime_set.jsonl`.

## Goal

Judge whether the English output is good enough for a constrained technical dialog, not whether it is native-level or perfect.

## Labels

- `A`: meaning preserved, immediately understandable, technical terms consistent
- `B`: understandable with minor grammar or wording errors, still usable live
- `C`: partially understandable, listener would need to infer too much
- `D`: misleading, broken, or missing the important meaning

## Review Rules

- Prefer meaning over elegance.
- Do not punish non-native phrasing if the sentence is clear.
- Technical term preservation matters more than style.
- Short, simple English is preferred over longer polished English.
- If a sentence drops a required technical term, it should not receive `A`.

## Minimum Acceptance Target

- `A/B >= 85%`
- `A >= 60%`

## Suggested Workflow

1. Generate or collect predictions in JSONL with `id` and `prediction_en`.
2. Run `python3 eval/score_domain_eval.py --dataset eval/domain_realtime_set.jsonl --predictions your_predictions.jsonl --emit-review-csv eval/manual_review.csv`.
3. Fill `label` and `notes` in the emitted CSV.
4. Re-run with `--manual-labels eval/manual_review.csv`.
