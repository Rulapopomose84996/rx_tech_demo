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
        summaries.append(payload)
    return summaries


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
    json_path = output_dir / "comparison.json"
    csv_path = output_dir / "comparison.csv"

    with json_path.open("w", encoding="utf-8") as handle:
        json.dump(summaries, handle, ensure_ascii=False, indent=2)

    if summaries:
        fieldnames = sorted({key for summary in summaries for key in summary.keys()})
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for summary in summaries:
                writer.writerow(summary)
    else:
        csv_path.write_text("", encoding="utf-8")

    print(f"aggregated {len(summaries)} summaries into {output_dir}")


if __name__ == "__main__":
    main()
