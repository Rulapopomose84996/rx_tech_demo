#!/usr/bin/env python3
"""Aggregate summary.json outputs into CSV and JSON comparison tables."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def load_summaries(results_root: Path) -> list[dict]:
    summaries: list[dict] = []
    for path in results_root.rglob("summary.json"):
        with path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        payload["summary_path"] = str(path)
        payload["run_dir"] = str(path.parent)
        summaries.append(payload)
    return summaries


def load_step_rows(summaries: list[dict]) -> list[dict]:
    step_rows: list[dict] = []
    for summary in summaries:
        steps_path = Path(summary["run_dir"]) / "steps.json"
        if not steps_path.exists():
            continue
        with steps_path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        for step in payload.get("steps", []):
            row = {
                "summary_path": summary["summary_path"],
                "run_dir": summary["run_dir"],
                "backend": summary.get("backend", ""),
                "mode": summary.get("mode", ""),
                "scenario": summary.get("scenario", ""),
                "summary_run_status": summary.get("run_status", ""),
                "backend_status": summary.get("backend_status", ""),
                "backend_reason": summary.get("backend_reason", ""),
            }
            row.update(step)
            step_rows.append(row)
    return step_rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Aggregate rx_tech_demo summaries")
    parser.add_argument("--results-root", default="results", help="root directory containing summary.json files")
    parser.add_argument("--output-dir", default="results/report_compare", help="output directory for aggregated files")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    results_root = root / args.results_root
    output_dir = root / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    summaries = load_summaries(results_root)
    step_rows = load_step_rows(summaries)
    json_path = output_dir / "comparison.json"
    csv_path = output_dir / "comparison.csv"
    steps_json_path = output_dir / "comparison_steps.json"
    steps_csv_path = output_dir / "comparison_steps.csv"

    with json_path.open("w", encoding="utf-8") as handle:
        json.dump(summaries, handle, ensure_ascii=False, indent=2)
    with steps_json_path.open("w", encoding="utf-8") as handle:
        json.dump(step_rows, handle, ensure_ascii=False, indent=2)

    if summaries:
        fieldnames = sorted({key for summary in summaries for key in summary.keys()})
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for summary in summaries:
                writer.writerow(summary)
    else:
        csv_path.write_text("", encoding="utf-8")

    if step_rows:
        fieldnames = sorted({key for row in step_rows for key in row.keys()})
        with steps_csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for row in step_rows:
                writer.writerow(row)
    else:
        steps_csv_path.write_text("", encoding="utf-8")

    print(f"aggregated {len(summaries)} summaries and {len(step_rows)} step rows into {output_dir}")


if __name__ == "__main__":
    main()
