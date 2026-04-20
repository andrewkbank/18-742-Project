#!/usr/bin/env python3
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import itertools
import os
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
    run_output_dir: Path,
    width: int,
    size_exp: int,
    pattern: int,
    repeats: int,
    cpu_type: str,
    use_noncaching_cpu: bool,
    mem_type: str,
    mem_size: str,
) -> dict[str, int | str]:
    run_output_dir.mkdir(parents=True, exist_ok=True)
    effective_cpu_type = "X86TimingSimpleCPU" if use_noncaching_cpu else cpu_type
    cmd = [
        "./build/X86/gem5.opt",
        "-d",
        str(run_output_dir),
        "configs/deprecated/example/se.py",
        f"--cpu-type={effective_cpu_type}",
        f"--mem-type={mem_type}",
        f"--mem-size={mem_size}",
        "-c",
        str(exe_path),
        "-o",
        f"{width} {size_exp} {pattern} {repeats}",
    ]
    if not use_noncaching_cpu:
        cmd.insert(5, "--l2cache")
        cmd.insert(5, "--caches")
    code, output = run_cmd(cmd, gem5_dir)
    if code != 0:
        raise RuntimeError(f"gem5 run failed for {exe_path.name}\n{output}")

    parsed = parse_run_output(output)
    parsed["raw_output"] = output
    return parsed


def ensure_executable(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Missing executable: {path}")


def build_run_specs(
    workloads: dict[str, dict[str, Path]],
    widths: list[int],
    sizes: list[int],
    patterns: list[int],
    repeats_list: list[int],
    shift_amounts: list[int],
    chain_width: int,
    arbitrary_width: int,
) -> list[dict[str, int | str | Path]]:
    specs: list[dict[str, int | str | Path]] = []

    for direction, pair in workloads.items():
        if direction == "chain":
            run_widths = [chain_width]
            run_repeats = repeats_list
        elif direction in {"left_arbitrary", "right_arbitrary"}:
            run_widths = [arbitrary_width]
            run_repeats = shift_amounts
        elif direction in {"left_8", "right_8"}:
            run_widths = widths
            run_repeats = [8]
        else:
            run_widths = widths
            run_repeats = [1]

        for width, size_exp, pattern, repeats in itertools.product(
            run_widths, sizes, patterns, run_repeats
        ):
            for variant in ("baseline", "pim"):
                specs.append(
                    {
                        "direction": direction,
                        "variant": variant,
                        "exe_path": pair[variant],
                        "width": width,
                        "size_exp": size_exp,
                        "pattern": pattern,
                        "repeats": repeats,
                    }
                )

    return specs


def run_spec(
    spec: dict[str, int | str | Path],
    gem5_dir: Path,
    output_dir: Path,
    cpu_type: str,
    use_noncaching_cpu: bool,
    mem_type: str,
    mem_size: str,
) -> dict[str, int | str]:
    run_output_dir = (
        output_dir
        / "run_logs"
        / str(spec["direction"])
        / str(spec["variant"])
        / (
            f"w{int(spec['width'])}_s{int(spec['size_exp'])}"
            f"_p{int(spec['pattern'])}_r{int(spec['repeats'])}"
        )
    )
    result = run_gem5(
        gem5_dir,
        Path(spec["exe_path"]),
        run_output_dir,
        int(spec["width"]),
        int(spec["size_exp"]),
        int(spec["pattern"]),
        int(spec["repeats"]),
        cpu_type,
        use_noncaching_cpu,
        mem_type,
        mem_size,
    )
    result.update(
        {
            "direction": str(spec["direction"]),
            "variant": str(spec["variant"]),
            "width": int(spec["width"]),
            "size_exp": int(spec["size_exp"]),
            "pattern": int(spec["pattern"]),
            "repeats": int(spec["repeats"]),
        }
    )
    return result


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

    ax.set_title(f"{direction.replace('_', ' ').title()} Shift PIM Speedup")
    ax.set_xlabel("num_vals")
    ax.set_ylabel("Speedup (baseline / PIM)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(output_dir / f"{direction}_shift_speedup.png", dpi=200)
    plt.close(fig)


def plot_amount(
    results: list[dict[str, int | float | str]],
    direction: str,
    width: int,
    sizes: list[int],
    amounts: list[int],
    pattern: int,
    output_dir: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    for size_exp in sizes:
        xs = []
        ys = []
        for amount in amounts:
            subset = [
                r
                for r in results
                if r["direction"] == direction
                and r["width"] == width
                and r["pattern"] == pattern
                and r["size_exp"] == size_exp
                and r["repeats"] == amount
            ]
            if subset:
                xs.append(amount)
                ys.append(float(subset[0]["speedup"]))

        if xs:
            ax.plot(xs, ys, marker="o", label=f"size_exp={size_exp}")

    ax.set_title(f"{direction.replace('_', ' ').title()} PIM Speedup ({width}-bit)")
    ax.set_xlabel("shift amount")
    ax.set_ylabel("Speedup (baseline / PIM)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()
    fig.savefig(output_dir / f"{direction}_speedup.png", dpi=200)
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
        description="Run shift baseline/PIM pairs and plot speedup."
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
    parser.add_argument(
        "--shift-amounts",
        default="1,8,13,24",
        help="Comma-separated arbitrary shift amounts",
    )
    parser.add_argument(
        "--arbitrary-width",
        type=int,
        default=32,
        help="Column width used for arbitrary shift sweep",
    )
    parser.add_argument("--cpu-type", default="X86O3CPU")
    parser.add_argument(
        "--use-noncaching-cpu",
        action="store_true",
        help="Use X86TimingSimpleCPU without --caches/--l2cache",
    )
    parser.add_argument("--mem-type", default="DDR4_2400_8x8")
    parser.add_argument("--mem-size", default="8192MB")
    parser.add_argument("--output-dir", default="shift_results")
    parser.add_argument(
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="Number of worker threads used to run microbenchmarks in parallel",
    )
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
    shift_amounts = [int(x) for x in args.shift_amounts.split(",") if x]

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
        "left_8": {
            "baseline": microworkloads / "96_shift_left_8-baseline.exe",
            "pim": microworkloads / "96_shift_left_8-plus.exe",
        },
        "right_8": {
            "baseline": microworkloads / "97_shift_right_8-baseline.exe",
            "pim": microworkloads / "97_shift_right_8-plus.exe",
        },
        "left_arbitrary": {
            "baseline": microworkloads / "98_shift_left_arbitrary-baseline.exe",
            "pim": microworkloads / "98_shift_left_arbitrary-plus.exe",
        },
        "right_arbitrary": {
            "baseline": microworkloads / "99_shift_right_arbitrary-baseline.exe",
            "pim": microworkloads / "99_shift_right_arbitrary-plus.exe",
        },
    }

    for direction in workloads.values():
        ensure_executable(direction["baseline"])
        ensure_executable(direction["pim"])

    if args.jobs < 1:
        raise ValueError("--jobs must be at least 1")

    run_specs = build_run_specs(
        workloads,
        widths,
        sizes,
        patterns,
        repeats_list,
        shift_amounts,
        args.chain_width,
        args.arbitrary_width,
    )

    completed_runs: dict[tuple[str, int, int, int, str], dict[str, int | str]] = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_to_spec = {
            executor.submit(
                run_spec,
                spec,
                gem5_dir,
                output_dir,
                args.cpu_type,
                args.use_noncaching_cpu,
                args.mem_type,
                args.mem_size,
            ): spec
            for spec in run_specs
        }

        for future in as_completed(future_to_spec):
            result = future.result()
            run_key = (
                str(result["direction"]),
                int(result["width"]),
                int(result["size_exp"]),
                int(result["pattern"]),
                f"{int(result['repeats'])}:{str(result['variant'])}",
            )
            completed_runs[run_key] = result

    results: list[dict[str, int | float | str]] = []

    for spec in run_specs:
        if str(spec["variant"]) != "baseline":
            continue

        key_prefix = (
            str(spec["direction"]),
            int(spec["width"]),
            int(spec["size_exp"]),
            int(spec["pattern"]),
        )
        repeats = int(spec["repeats"])
        baseline = completed_runs[key_prefix + (f"{repeats}:baseline",)]
        pim = completed_runs[key_prefix + (f"{repeats}:pim",)]

        if int(baseline["mismatches"]) != 0 or int(pim["mismatches"]) != 0:
            raise RuntimeError(
                "Mismatch detected for "
                f"{spec['direction']}, width={spec['width']}, size={spec['size_exp']}, "
                f"pattern={spec['pattern']}, repeats={repeats}"
            )

        speedup = int(baseline["ticks"]) / int(pim["ticks"])
        results.append(
            {
                "direction": str(spec["direction"]),
                "width": int(spec["width"]),
                "size_exp": int(spec["size_exp"]),
                "pattern": int(spec["pattern"]),
                "repeats": repeats,
                "baseline_ticks": int(baseline["ticks"]),
                "pim_ticks": int(pim["ticks"]),
                "speedup": speedup,
                "baseline_checksum": int(baseline["checksum"]),
                "pim_checksum": int(pim["checksum"]),
            }
        )

    results.sort(
        key=lambda r: (
            str(r["direction"]),
            int(r["width"]),
            int(r["size_exp"]),
            int(r["pattern"]),
            int(r["repeats"]),
        )
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

    for direction in ("left", "right", "left_8", "right_8"):
        plot_direction(results, direction, widths, sizes, patterns[0], output_dir)
    plot_chain(results, args.chain_width, sizes, repeats_list, patterns[0], output_dir)
    for direction in ("left_arbitrary", "right_arbitrary"):
        plot_amount(
            results,
            direction,
            args.arbitrary_width,
            sizes,
            shift_amounts,
            patterns[0],
            output_dir,
        )

    print(f"Wrote results to {csv_path}")
    print(f"Plots saved in {output_dir}")


if __name__ == "__main__":
    main()
