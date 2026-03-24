# Domain Eval

This directory contains the Phase 3 evaluation set and lightweight scoring tools.

## Files

- `domain_realtime_set.jsonl`: constrained ES->EN meeting dataset
- `generate_domain_realtime_set.py`: deterministic generator for the dataset
- `score_domain_eval.py`: local scoring helper
- `LABELING.md`: manual rubric and thresholds
- `baseline_status.md`: current baseline notes

## Commands

Generate the dataset:

```bash
python3 eval/generate_domain_realtime_set.py
```

Run an oracle sanity check:

```bash
python3 eval/score_domain_eval.py \
  --dataset eval/domain_realtime_set.jsonl \
  --oracle \
  --summary-out eval/oracle_sanity_summary.json
```

Score real predictions and emit a manual review sheet:

```bash
python3 eval/score_domain_eval.py \
  --dataset eval/domain_realtime_set.jsonl \
  --predictions eval/your_predictions.jsonl \
  --emit-review-csv eval/manual_review.csv
```
