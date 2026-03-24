#!/usr/bin/env python3
"""Score ES->EN domain evaluation predictions with lightweight local metrics."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


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


def load_labels(path: Path | None) -> dict[str, str]:
    if path is None:
        return {}
    labels: dict[str, str] = {}
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            sample_id = row.get("id", "").strip()
            label = row.get("label", "").strip().upper()
            if sample_id and label:
                labels[sample_id] = label
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

    if args.emit_review_csv is not None:
        args.emit_review_csv.parent.mkdir(parents=True, exist_ok=True)
        review_handle = args.emit_review_csv.open("w", encoding="utf-8", newline="")
        writer = csv.DictWriter(
            review_handle,
            fieldnames=["id", "category", "source_es", "reference_en", "prediction_en", "label", "notes"],
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
            label_counts[labels[row["id"]]] += 1

        if writer is not None:
            writer.writerow(
                {
                    "id": row["id"],
                    "category": row["category"],
                    "source_es": row["source_es"],
                    "reference_en": row["reference_en"],
                    "prediction_en": prediction,
                    "label": labels.get(row["id"], ""),
                    "notes": "",
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
    }

    if label_counts:
        ab_count = label_counts.get("A", 0) + label_counts.get("B", 0)
        summary["manual_ab_rate"] = round(ab_count / total, 4)
        summary["manual_a_rate"] = round(label_counts.get("A", 0) / total, 4)

    rendered = json.dumps(summary, indent=2, sort_keys=True)
    print(rendered)
    if args.summary_out is not None:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(rendered + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
