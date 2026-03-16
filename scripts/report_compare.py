#!/usr/bin/env python3
"""Placeholder report aggregation script."""

from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    print(f"report compare placeholder: {root}")


if __name__ == "__main__":
    main()
