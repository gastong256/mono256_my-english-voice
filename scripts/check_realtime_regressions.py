#!/usr/bin/env python3
"""Check latency and conversational quality summaries against guardrails."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_json(path: Path | None) -> dict:
    if path is None:
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def add_check(checks: list[dict], name: str, status: str, details: dict) -> None:
    checks.append(
        {
            "name": name,
            "status": status,
            "details": details,
        }
    )


def evaluate_latency(
    summary: dict,
    thresholds: dict,
    baseline: dict,
    checks: list[dict],
    violations: list[str],
) -> None:
    sessions = {session["label"]: session["metrics"] for session in summary.get("sessions", [])}
    baseline_sessions = {session["label"]: session["metrics"] for session in baseline.get("sessions", [])}
    acceptance_max = thresholds.get("acceptance_max", {})
    rel_limits = thresholds.get("relative_regression_fraction_max", {})
    non_increasing = thresholds.get("non_increasing_metrics", [])

    for label, metrics in sessions.items():
        for metric_name, max_value in acceptance_max.get(label, {}).items():
            actual = metrics.get(metric_name)
            if actual is None:
                continue
            status = "pass" if actual <= max_value else "fail"
            add_check(
                checks,
                f"latency.acceptance.{label}.{metric_name}",
                status,
                {"actual": actual, "max": max_value},
            )
            if status == "fail":
                violations.append(f"{label}: {metric_name}={actual} exceeded max {max_value}")

        baseline_metrics = baseline_sessions.get(label, {})
        for metric_name, frac_limit in rel_limits.items():
            actual = metrics.get(metric_name)
            baseline_value = baseline_metrics.get(metric_name)
            if actual is None or baseline_value in (None, 0):
                continue
            allowed = baseline_value * (1.0 + frac_limit)
            status = "pass" if actual <= allowed else "fail"
            add_check(
                checks,
                f"latency.regression.{label}.{metric_name}",
                status,
                {"actual": actual, "baseline": baseline_value, "allowed_max": round(allowed, 4)},
            )
            if status == "fail":
                violations.append(
                    f"{label}: {metric_name}={actual} regressed more than {frac_limit:.0%} over baseline {baseline_value}"
                )

        for metric_name in non_increasing:
            actual = metrics.get(metric_name)
            baseline_value = baseline_metrics.get(metric_name)
            if actual is None or baseline_value is None:
                continue
            status = "pass" if actual <= baseline_value else "fail"
            add_check(
                checks,
                f"latency.non_increasing.{label}.{metric_name}",
                status,
                {"actual": actual, "baseline": baseline_value},
            )
            if status == "fail":
                violations.append(
                    f"{label}: {metric_name}={actual} should not increase above baseline {baseline_value}"
                )


def evaluate_quality(
    summary: dict,
    thresholds: dict,
    baseline: dict,
    checks: list[dict],
    violations: list[str],
) -> None:
    min_rates = thresholds.get("minimum_rates", {})
    drop_limits = thresholds.get("relative_drop_fraction_max", {})

    for metric_name, min_value in min_rates.items():
        actual = summary.get(metric_name)
        if actual is None:
            continue
        status = "pass" if actual >= min_value else "fail"
        add_check(
            checks,
            f"quality.minimum.{metric_name}",
            status,
            {"actual": actual, "min": min_value},
        )
        if status == "fail":
            violations.append(f"{metric_name}={actual} fell below minimum {min_value}")

    for metric_name, frac_limit in drop_limits.items():
        actual = summary.get(metric_name)
        baseline_value = baseline.get(metric_name)
        if actual is None or baseline_value in (None, 0):
            continue
        allowed = baseline_value * (1.0 - frac_limit)
        status = "pass" if actual >= allowed else "fail"
        add_check(
            checks,
            f"quality.regression.{metric_name}",
            status,
            {"actual": actual, "baseline": baseline_value, "allowed_min": round(allowed, 4)},
        )
        if status == "fail":
            violations.append(
                f"{metric_name}={actual} dropped more than {frac_limit:.0%} below baseline {baseline_value}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--latency-summary", type=Path)
    parser.add_argument("--latency-thresholds", type=Path)
    parser.add_argument("--latency-baseline", type=Path)
    parser.add_argument("--quality-summary", type=Path)
    parser.add_argument("--quality-thresholds", type=Path)
    parser.add_argument("--quality-baseline", type=Path)
    args = parser.parse_args()

    checks: list[dict] = []
    violations: list[str] = []

    if args.latency_summary and args.latency_thresholds:
        evaluate_latency(
            load_json(args.latency_summary),
            load_json(args.latency_thresholds),
            load_json(args.latency_baseline),
            checks,
            violations,
        )

    if args.quality_summary and args.quality_thresholds:
        evaluate_quality(
            load_json(args.quality_summary),
            load_json(args.quality_thresholds),
            load_json(args.quality_baseline),
            checks,
            violations,
        )

    report = {
        "status": "pass" if not violations else "fail",
        "checks": checks,
        "violations": violations,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if not violations else 1


if __name__ == "__main__":
    sys.exit(main())
