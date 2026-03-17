#!/usr/bin/env python3
"""Run backend/mode/scenario combinations with a shared CLI shape."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


DEFAULT_BACKENDS = ("socket", "af_xdp", "dpdk")
DEFAULT_MODES = ("rx_only", "parse", "spsc")
DEFAULT_SCENARIOS = (
    "scenarios/single_face_steady.yaml",
    "scenarios/three_face_steady.yaml",
    "scenarios/single_face_burst.yaml",
    "scenarios/three_face_burst.yaml",
    "scenarios/real_pktmix.yaml",
)


def parse_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def main() -> None:
    parser = argparse.ArgumentParser(description="Run rx_tech_demo benchmark matrix")
    parser.add_argument("--build-dir", default="build/src/apps", help="directory containing rxbench binaries")
    parser.add_argument("--output-root", default="results/matrix", help="root directory for matrix results")
    parser.add_argument("--backends", default=",".join(DEFAULT_BACKENDS))
    parser.add_argument("--modes", default=",".join(DEFAULT_MODES))
    parser.add_argument("--scenarios", default=",".join(DEFAULT_SCENARIOS))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = root / args.build_dir
    output_root = root / args.output_root
    output_root.mkdir(parents=True, exist_ok=True)

    backend_to_binary = {
        "socket": build_dir / "rxbench_socket",
        "af_xdp": build_dir / "rxbench_xdp",
        "dpdk": build_dir / "rxbench_dpdk",
    }
    backend_to_config = {
        "socket": root / "configs/socket_loopback.conf",
        "af_xdp": root / "configs/af_xdp_single_face.conf",
        "dpdk": root / "configs/dpdk_single_face.conf",
    }

    for backend in parse_csv(args.backends):
        binary = backend_to_binary[backend]
        config_path = backend_to_config[backend]
        for mode in parse_csv(args.modes):
            for scenario in parse_csv(args.scenarios):
                scenario_name = Path(scenario).stem
                output_dir = output_root / backend / mode / scenario_name
                output_dir.mkdir(parents=True, exist_ok=True)
                cmd = [
                    str(binary),
                    "--config",
                    str(config_path),
                    "--mode",
                    mode,
                    "--scenario",
                    str(root / scenario),
                    "--output",
                    str(output_dir),
                ]
                print(" ".join(cmd))
                if not args.dry_run:
                    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
