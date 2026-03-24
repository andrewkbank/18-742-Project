#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import itertools
import re
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt


EXIT_TICK_RE = re.compile(r"Exiting @ tick (\d+)")
SUMMARY_RE = re.compile(
    r"(shift_[^ ]+) pattern=(\d+) repeats=(\d+) checksum=(\d+) mismatches=(\d+)"
)


def run_cmd(cmd: list[str], cwd: Path) -> tuple[int, str]:
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    return proc.returncode, proc.stdout


def parse_run_output(output: str) -> dict[str, int | str]:
    tick_match = EXIT_TICK_RE.search(output)
    summary_match = SUMMARY_RE.search(output)

    if not tick_match:
        raise RuntimeError("Could not find exit tick in gem5 output.")
    if not summary_match:
        raise RuntimeError("Could not find benchmark summary line in gem5 output.")

    return {
        "ticks": int(tick_match.group(1)),
        "name": summary_match.group(1),
        "pattern": int(summary_match.group(2)),
        "repeats": int(summary_match.group(3)),
        "checksum": int(summary_match.group(4)),
        "mismatches": int(summary_match.group(5)),
    }


def run_gem5(
    gem5_dir: Path,
    exe_path: Path,
    width: int,
    size_exp: int,
    pattern: int,
    repeats: int,
    cpu_type: str,
    mem_type: str,
    mem_size: str,
) -> dict[str, int | str]:
    cmd = [
        "./build/X86/gem5.opt",
        "configs/deprecated/example/se.py",
        f"--cpu-type={cpu_type}",
        "--caches",
        "--l2cache",
        f"--mem-type={mem_type}",
        f"--mem-size={mem_size}",
        "-c",
        str(exe_path),
        "-o",
        f"{width} {size_exp} {pattern} {repeats}",
    ]
    code, output = run_cmd(cmd, gem5_dir)
    if code != 0:
        raise RuntimeError(f"gem5 run failed for {exe_path.name}\n{output}")

    parsed = parse_run_output(output)
    parsed["raw_output"] = output
    return parsed


def ensure_executable(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Missing executable: {path}")


def plot_direction(
    results: list[dict[str, int | float | str]],
    direction: str,
    widths: list[int],
    sizes: list[int],
    pattern: int,
    output_dir: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    for width in widths:
        xs = []
        ys = []
        for size_exp in sizes:
            subset = [
                r
                for r in results
                if r["direction"] == direction
                and r["width"] == width
                and r["pattern"] == pattern
                and r["size_exp"] == size_exp
            ]
            if subset:
                xs.append(1024 << size_exp)
                ys.append(float(subset[0]["speedup"]))

        if xs:
            ax.plot(xs, ys, marker="o", label=f"{width}-bit")

    ax.set_title(f"{direction.capitalize()} Shift PIM Speedup")
    ax.set_xlabel("num_vals")
    ax.set_ylabel("Speedup (baseline / PIM)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(output_dir / f"{direction}_shift_speedup.png", dpi=200)
    plt.close(fig)


def plot_chain(
    results: list[dict[str, int | float | str]],
    chain_width: int,
    sizes: list[int],
    repeats_list: list[int],
    pattern: int,
    output_dir: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    for size_exp in sizes:
        xs = []
        ys = []
        for repeats in repeats_list:
            subset = [
                r
                for r in results
                if r["direction"] == "chain"
                and r["width"] == chain_width
                and r["pattern"] == pattern
                and r["size_exp"] == size_exp
                and r["repeats"] == repeats
            ]
            if subset:
                xs.append(repeats)
                ys.append(float(subset[0]["speedup"]))

        if xs:
            ax.plot(xs, ys, marker="o", label=f"size_exp={size_exp}")

    ax.set_title(f"Chain Shift PIM Speedup ({chain_width}-bit)")
    ax.set_xlabel("repeats")
    ax.set_ylabel("Speedup (baseline / PIM)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(output_dir / "chain_shift_speedup.png", dpi=200)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run shift-by-1 baseline/PIM pairs and plot speedup."
    )
    parser.add_argument(
        "--project-root",
        default=str(Path(__file__).resolve().parent),
        help="Path to 18-742-Project",
    )
    parser.add_argument(
        "--gem5-dir",
        default=None,
        help="Path to gem5 repo (defaults to ./gem5 under project root)",
    )
    parser.add_argument("--widths", default="8,16,32,64")
    parser.add_argument(
        "--sizes", default="0,1,2,3", help="Comma-separated size exponents"
    )
    parser.add_argument("--patterns", default="0", help="Comma-separated pattern ids")
    parser.add_argument(
        "--repeats",
        default="1,2,4,8,16",
        help="Comma-separated repeat counts for chain benchmark",
    )
    parser.add_argument(
        "--chain-width",
        type=int,
        default=32,
        help="Column width used for chain benchmark sweep",
    )
    parser.add_argument("--cpu-type", default="X86O3CPU")
    parser.add_argument("--mem-type", default="DDR4_2400_8x8")
    parser.add_argument("--mem-size", default="8192MB")
    parser.add_argument("--output-dir", default="shift_results")
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    gem5_dir = (
        Path(args.gem5_dir).resolve()
        if args.gem5_dir
        else (project_root / "gem5").resolve()
    )
    microworkloads = project_root / "microworkloads"
    output_dir = project_root / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    widths = [int(x) for x in args.widths.split(",") if x]
    sizes = [int(x) for x in args.sizes.split(",") if x]
    patterns = [int(x) for x in args.patterns.split(",") if x]
    repeats_list = [int(x) for x in args.repeats.split(",") if x]

    workloads = {
        "left": {
            "baseline": microworkloads / "93_shift_left_1-baseline.exe",
            "pim": microworkloads / "93_shift_left_1-plus.exe",
        },
        "right": {
            "baseline": microworkloads / "94_shift_right_1-baseline.exe",
            "pim": microworkloads / "94_shift_right_1-plus.exe",
        },
        "chain": {
            "baseline": microworkloads / "95_shift_chain_1-baseline.exe",
            "pim": microworkloads / "95_shift_chain_1-plus.exe",
        },
    }

    for direction in workloads.values():
        ensure_executable(direction["baseline"])
        ensure_executable(direction["pim"])

    results: list[dict[str, int | float | str]] = []

    for direction, pair in workloads.items():
        run_widths = widths if direction != "chain" else [args.chain_width]
        run_repeats = [1] if direction != "chain" else repeats_list

        for width, size_exp, pattern, repeats in itertools.product(
            run_widths, sizes, patterns, run_repeats
        ):
            baseline = run_gem5(
                gem5_dir,
                pair["baseline"],
                width,
                size_exp,
                pattern,
                repeats,
                args.cpu_type,
                args.mem_type,
                args.mem_size,
            )
            pim = run_gem5(
                gem5_dir,
                pair["pim"],
                width,
                size_exp,
                pattern,
                repeats,
                args.cpu_type,
                args.mem_type,
                args.mem_size,
            )

            if int(baseline["mismatches"]) != 0 or int(pim["mismatches"]) != 0:
                raise RuntimeError(
                    "Mismatch detected for "
                    f"{direction}, width={width}, size={size_exp}, "
                    f"pattern={pattern}, repeats={repeats}"
                )

            speedup = int(baseline["ticks"]) / int(pim["ticks"])
            results.append(
                {
                    "direction": direction,
                    "width": width,
                    "size_exp": size_exp,
                    "pattern": pattern,
                    "repeats": repeats,
                    "baseline_ticks": int(baseline["ticks"]),
                    "pim_ticks": int(pim["ticks"]),
                    "speedup": speedup,
                    "baseline_checksum": int(baseline["checksum"]),
                    "pim_checksum": int(pim["checksum"]),
                }
            )

    csv_path = output_dir / "shift_speedup.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "direction",
                "width",
                "size_exp",
                "pattern",
                "repeats",
                "baseline_ticks",
                "pim_ticks",
                "speedup",
                "baseline_checksum",
                "pim_checksum",
            ],
        )
        writer.writeheader()
        writer.writerows(results)

    for direction in ("left", "right"):
        plot_direction(results, direction, widths, sizes, patterns[0], output_dir)
    plot_chain(results, args.chain_width, sizes, repeats_list, patterns[0], output_dir)

    print(f"Wrote results to {csv_path}")
    print(f"Plots saved in {output_dir}")


if __name__ == "__main__":
    main()
