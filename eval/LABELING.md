# Domain Realtime Labeling Guide

Use this rubric for manual review of `eval/domain_realtime_set.jsonl`.

## Goal

Judge whether the English output is good enough for a constrained technical dialog, not whether it is native-level or perfect.

## Labels

- `A`: meaning preserved, immediately understandable, technical terms consistent
- `B`: understandable with minor grammar or wording errors, still usable live
- `C`: partially understandable, listener would need to infer too much
- `D`: misleading, broken, or missing the important meaning

## Conversational Profiles

Map each reviewed row to one practical target profile for constrained technical dialog:

- `B1 acceptable`: the listener can keep the conversation moving without asking for a repeat
- `B2 good`: the sentence is clearly usable live and technical intent stays intact
- `C1 very good in-domain`: very natural for this domain, with only tiny non-blocking issues
- `below_b1`: not reliable enough for live use

Default mapping:

- `A -> C1`
- `B -> B2`
- `C -> B1`
- `D -> below_b1`

If a row deserves a different conversational profile than the default label mapping, override it in the CSV.

## Review Rules

- Prefer meaning over elegance.
- Do not punish non-native phrasing if the sentence is clear.
- Technical term preservation matters more than style.
- Short, simple English is preferred over longer polished English.
- If a sentence drops a required technical term, it should not receive `A`.

## Error Taxonomy

Use `primary_error` to tag the main failure mode when the output is not clearly `C1`.

- `term_drift`: the translation changed or lost an important technical term
- `truncation`: the output dropped meaningful content or ended too early
- `over_literal_phrasing`: the output mirrors Spanish structure too closely and becomes awkward or confusing
- `clause_split_bad_timing`: chunking/timing made the output hard to follow even if words are mostly correct

Use `fix_bucket` to point the implementation toward the right correction area:

- `domain_glossary_or_prompt`
- `asr_tts_budget_or_scheduler`
- `translation_preferences_or_domain_adapter`
- `speech_chunker_or_scheduler_tuning`

Recommended default mapping:

- `term_drift -> domain_glossary_or_prompt`
- `truncation -> asr_tts_budget_or_scheduler`
- `over_literal_phrasing -> translation_preferences_or_domain_adapter`
- `clause_split_bad_timing -> speech_chunker_or_scheduler_tuning`

## Minimum Acceptance Target

- `A/B >= 85%`
- `A >= 60%`
- `B1 or better >= 85%`
- `B2 or better >= 60%`

## Suggested Workflow

1. Generate or collect predictions in JSONL with `id` and `prediction_en`.
2. Run `python3 eval/score_domain_eval.py --dataset eval/domain_realtime_set.jsonl --predictions your_predictions.jsonl --emit-review-csv eval/manual_review.csv`.
3. Fill `label`, `conversational_profile`, `primary_error`, `fix_bucket`, and `notes` in the emitted CSV.
4. Re-run with `--manual-labels eval/manual_review.csv`.
