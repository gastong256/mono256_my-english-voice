# Domain Eval

This directory contains the domain evaluation set, lightweight scoring tools, and the
manual review workflow used to validate conversational usefulness for the realtime path.

## Files

- `domain_realtime_set.jsonl`: constrained ES->EN meeting dataset
- `generate_domain_realtime_set.py`: deterministic generator for the dataset
- `score_domain_eval.py`: local scoring helper
- `LABELING.md`: manual rubric and thresholds
- `baseline_status.md`: current baseline notes
- `conversational_thresholds.json`: checked-in quality guardrails

## Conversational Goal

The acceptance target for realtime mode is not native-level English. The goal is:

- `B1 acceptable` as the minimum useful floor
- `B2 good` as the stable target
- `C1 very good in-domain` as an aspirational outcome for frequent phrases and controlled contexts

The manual review sheet captures both the coarse quality label (`A/B/C/D`) and the
practical conversational profile (`B1/B2/C1/below_b1`), plus the dominant error class.

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

Re-score after manual review to compute profile and error summaries:

```bash
python3 eval/score_domain_eval.py \
  --dataset eval/domain_realtime_set.jsonl \
  --predictions eval/your_predictions.jsonl \
  --manual-labels eval/manual_review.csv \
  --summary-out eval/manual_review_summary.json
```

Check the reviewed summary against the conversational guardrails:

```bash
python3 scripts/check_realtime_regressions.py \
  --quality-summary eval/manual_review_summary.json \
  --quality-thresholds eval/conversational_thresholds.json
```
