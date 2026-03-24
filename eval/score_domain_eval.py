#!/usr/bin/env python3
"""Score ES->EN domain evaluation predictions with lightweight local metrics."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

PROFILE_BY_LABEL = {
    "A": "C1",
    "B": "B2",
    "C": "B1",
    "D": "below_b1",
}

FIX_BUCKET_BY_ERROR = {
    "term_drift": "domain_glossary_or_prompt",
    "truncation": "asr_tts_budget_or_scheduler",
    "over_literal_phrasing": "translation_preferences_or_domain_adapter",
    "clause_split_bad_timing": "speech_chunker_or_scheduler_tuning",
}


def load_jsonl(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def tokenize(text: str) -> list[str]:
    return [token for token in text.lower().replace("/", " ").replace("-", " ").split() if token]


def token_f1(reference: str, prediction: str) -> float:
    ref = Counter(tokenize(reference))
    pred = Counter(tokenize(prediction))
    overlap = sum((ref & pred).values())
    if not ref and not pred:
        return 1.0
    if overlap == 0:
        return 0.0
    precision = overlap / sum(pred.values())
    recall = overlap / sum(ref.values())
    return 2.0 * precision * recall / (precision + recall)


def term_coverage(prediction: str, required_terms: list[str]) -> float:
    if not required_terms:
        return 1.0
    lowered = prediction.lower()
    covered = sum(1 for term in required_terms if term.lower() in lowered)
    return covered / len(required_terms)


def normalize_manual_field(value: str) -> str:
    return value.strip().lower().replace(" ", "_").replace("-", "_")


def infer_profile(label: str, profile: str) -> str:
    if profile:
        normalized = normalize_manual_field(profile)
        if normalized in {"b1", "b2", "c1", "below_b1"}:
            return normalized
    return PROFILE_BY_LABEL.get(label, "")


def infer_fix_bucket(error_category: str, fix_bucket: str) -> str:
    if fix_bucket:
        return normalize_manual_field(fix_bucket)
    return FIX_BUCKET_BY_ERROR.get(normalize_manual_field(error_category), "")


def load_labels(path: Path | None) -> dict[str, dict[str, str]]:
    if path is None:
        return {}
    labels: dict[str, dict[str, str]] = {}
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            sample_id = row.get("id", "").strip()
            label = row.get("label", "").strip().upper()
            if sample_id and label:
                error_category = normalize_manual_field(row.get("primary_error", ""))
                profile = infer_profile(label, row.get("conversational_profile", ""))
                fix_bucket = infer_fix_bucket(error_category, row.get("fix_bucket", ""))
                labels[sample_id] = {
                    "label": label,
                    "primary_error": error_category,
                    "conversational_profile": profile,
                    "fix_bucket": fix_bucket,
                    "notes": row.get("notes", "").strip(),
                }
    return labels


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--predictions", type=Path)
    parser.add_argument("--manual-labels", type=Path)
    parser.add_argument("--emit-review-csv", type=Path)
    parser.add_argument("--summary-out", type=Path)
    parser.add_argument("--oracle", action="store_true")
    args = parser.parse_args()

    dataset = load_jsonl(args.dataset)
    predictions_by_id: dict[str, str] = {}
    if args.oracle:
        predictions_by_id = {row["id"]: row["reference_en"] for row in dataset}
    elif args.predictions is not None:
        for row in load_jsonl(args.predictions):
            sample_id = row["id"]
            predictions_by_id[sample_id] = row.get("prediction_en", row.get("translated_text_en", ""))
    else:
        raise SystemExit("Pass --oracle or --predictions")

    labels = load_labels(args.manual_labels)

    total = len(dataset)
    exact_matches = 0
    f1_sum = 0.0
    coverage_sum = 0.0
    missing_predictions = 0
    label_counts = Counter()
    profile_counts = Counter()
    error_counts = Counter()
    fix_bucket_counts = Counter()

    if args.emit_review_csv is not None:
        args.emit_review_csv.parent.mkdir(parents=True, exist_ok=True)
        review_handle = args.emit_review_csv.open("w", encoding="utf-8", newline="")
        writer = csv.DictWriter(
            review_handle,
            fieldnames=[
                "id",
                "category",
                "source_es",
                "reference_en",
                "prediction_en",
                "label",
                "conversational_profile",
                "primary_error",
                "fix_bucket",
                "notes",
            ],
        )
        writer.writeheader()
    else:
        review_handle = None
        writer = None

    for row in dataset:
        prediction = predictions_by_id.get(row["id"], "")
        if not prediction:
            missing_predictions += 1
        if prediction.strip() == row["reference_en"].strip():
            exact_matches += 1

        f1_sum += token_f1(row["reference_en"], prediction)
        coverage_sum += term_coverage(prediction, row.get("required_terms", []))

        if row["id"] in labels:
            manual = labels[row["id"]]
            label_counts[manual["label"]] += 1
            if manual["conversational_profile"]:
                profile_counts[manual["conversational_profile"]] += 1
            if manual["primary_error"]:
                error_counts[manual["primary_error"]] += 1
            if manual["fix_bucket"]:
                fix_bucket_counts[manual["fix_bucket"]] += 1

        if writer is not None:
            manual = labels.get(row["id"], {})
            writer.writerow(
                {
                    "id": row["id"],
                    "category": row["category"],
                    "source_es": row["source_es"],
                    "reference_en": row["reference_en"],
                    "prediction_en": prediction,
                    "label": manual.get("label", ""),
                    "conversational_profile": manual.get("conversational_profile", ""),
                    "primary_error": manual.get("primary_error", ""),
                    "fix_bucket": manual.get("fix_bucket", ""),
                    "notes": manual.get("notes", ""),
                }
            )

    if review_handle is not None:
        review_handle.close()

    summary = {
        "count": total,
        "missing_predictions": missing_predictions,
        "exact_match_rate": round(exact_matches / total, 4),
        "avg_token_f1": round(f1_sum / total, 4),
        "avg_required_term_coverage": round(coverage_sum / total, 4),
        "manual_label_counts": dict(label_counts),
        "manual_profile_counts": dict(profile_counts),
        "manual_error_counts": dict(error_counts),
        "manual_fix_bucket_counts": dict(fix_bucket_counts),
    }

    if label_counts:
        ab_count = label_counts.get("A", 0) + label_counts.get("B", 0)
        summary["manual_ab_rate"] = round(ab_count / total, 4)
        summary["manual_a_rate"] = round(label_counts.get("A", 0) / total, 4)
        summary["manual_b1_or_better_rate"] = round(
            (profile_counts.get("b1", 0) + profile_counts.get("b2", 0) + profile_counts.get("c1", 0)) / total,
            4,
        )
        summary["manual_b2_or_better_rate"] = round(
            (profile_counts.get("b2", 0) + profile_counts.get("c1", 0)) / total,
            4,
        )
        summary["manual_c1_rate"] = round(profile_counts.get("c1", 0) / total, 4)

    rendered = json.dumps(summary, indent=2, sort_keys=True)
    print(rendered)
    if args.summary_out is not None:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(rendered + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
