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
BASELINE_TIMING_MODEL = 0


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


def parse_exit_tick(output: str) -> int:
    tick_match = EXIT_TICK_RE.search(output)
    if not tick_match:
        raise RuntimeError("Could not find exit tick in gem5 output.")
    return int(tick_match.group(1))


def compile_macroworkloads(macroworkloads_dir: Path, jobs: int) -> None:
    code, output = run_cmd(["make", f"-j{jobs}"], macroworkloads_dir)
    if code != 0:
        raise RuntimeError(f"Failed to compile macroworkloads\n{output}")


def discover_workload_pairs(macroworkloads_dir: Path) -> dict[str, dict[str, Path]]:
    pairs: dict[str, dict[str, Path]] = {}
    for baseline_src in sorted(macroworkloads_dir.glob("*-baseline.c")):
        pair_name = baseline_src.stem.removesuffix("-baseline")
        pim_src = macroworkloads_dir / f"{pair_name}-plus.c"
        if not pim_src.exists():
            continue

        baseline_exe = macroworkloads_dir / f"{pair_name}-baseline.exe"
        pim_exe = macroworkloads_dir / f"{pair_name}-plus.exe"
        pairs[pair_name] = {"baseline": baseline_exe, "pim": pim_exe}

    if not pairs:
        raise RuntimeError("No baseline/plus macroworkload pairs were found.")

    return pairs


def ensure_executable(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Missing executable: {path}")


def build_run_specs(
    pairs: dict[str, dict[str, Path]],
    widths: list[int],
    sizes: list[int],
    timing_models: list[int],
) -> list[dict[str, int | str | Path]]:
    specs: list[dict[str, int | str | Path]] = []
    for pair_name, pair in pairs.items():
        for width, size_exp in itertools.product(widths, sizes):
            specs.append(
                {
                    "pair_name": pair_name,
                    "variant": "baseline",
                    "exe_path": pair["baseline"],
                    "width": width,
                    "size_exp": size_exp,
                    "timing_model": BASELINE_TIMING_MODEL,
                }
            )
            for timing_model in timing_models:
                specs.append(
                    {
                        "pair_name": pair_name,
                        "variant": "pim",
                        "exe_path": pair["pim"],
                        "width": width,
                        "size_exp": size_exp,
                        "timing_model": timing_model,
                    }
                )
    return specs


def run_gem5(
    gem5_dir: Path,
    exe_path: Path,
    run_output_dir: Path,
    width: int,
    size_exp: int,
    cpu_type: str,
    use_noncaching_cpu: bool,
    mem_type: str,
    mem_size: str,
    timing_model: int,
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
        f"--timing-model={timing_model}",
        "-c",
        str(exe_path),
        "-o",
        f"{width} {size_exp}",
    ]
    if not use_noncaching_cpu:
        cmd.insert(5, "--l2cache")
        cmd.insert(5, "--caches")

    code, output = run_cmd(cmd, gem5_dir)
    stdout_path = run_output_dir / "gem5_stdout.txt"
    stdout_path.write_text(output)

    if code != 0:
        raise RuntimeError(f"gem5 run failed for {exe_path.name}\n{output}")

    return {
        "ticks": parse_exit_tick(output),
        "stdout_path": str(stdout_path),
    }


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
        / str(spec["pair_name"])
        / "run_logs"
        / f"timing_model_{int(spec['timing_model'])}"
        / str(spec["variant"])
        / f"w{int(spec['width'])}_s{int(spec['size_exp'])}"
    )
    result = run_gem5(
        gem5_dir,
        Path(spec["exe_path"]),
        run_output_dir,
        int(spec["width"]),
        int(spec["size_exp"]),
        cpu_type,
        use_noncaching_cpu,
        mem_type,
        mem_size,
        int(spec["timing_model"]),
    )
    result.update(
        {
            "pair_name": str(spec["pair_name"]),
            "variant": str(spec["variant"]),
            "width": int(spec["width"]),
            "size_exp": int(spec["size_exp"]),
            "timing_model": int(spec["timing_model"]),
        }
    )
    return result


def write_pair_summary(
    pair_dir: Path, rows: list[dict[str, int | float | str]]
) -> None:
    csv_path = pair_dir / "summary.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "pair_name",
                "width",
                "size_exp",
                "timing_model",
                "baseline_timing_model",
                "baseline_ticks",
                "pim_ticks",
                "speedup",
                "baseline_stdout",
                "pim_stdout",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def plot_pair_speedup(
    pair_name: str,
    rows: list[dict[str, int | float | str]],
    widths: list[int],
    sizes: list[int],
    timing_models: list[int],
    pair_dir: Path,
) -> None:
    for timing_model in timing_models:
        fig, ax = plt.subplots(figsize=(8, 5))

        for width in widths:
            xs = []
            ys = []
            for size_exp in sizes:
                subset = [
                    row
                    for row in rows
                    if int(row["timing_model"]) == timing_model
                    and int(row["width"]) == width
                    and int(row["size_exp"]) == size_exp
                ]
                if subset:
                    xs.append(1024 << size_exp)
                    ys.append(float(subset[0]["speedup"]))

            if xs:
                ax.plot(xs, ys, marker="o", label=f"{width}-bit")

        ax.set_title(
            f"{pair_name.replace('_', ' ').title()} Speedup "
            f"(baseline tm={BASELINE_TIMING_MODEL}, pim tm={timing_model})"
        )
        ax.set_xlabel("num_vals")
        ax.set_ylabel("Speedup (baseline / PIM)")
        ax.grid(True)
        if ax.lines:
            ax.legend()
        fig.tight_layout()
        fig.savefig(pair_dir / f"timing_model_{timing_model}_speedup.png", dpi=200)
        plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Compile and run macroworkload baseline/PIM pairs across timing models."
        )
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
    parser.add_argument("--sizes", default="0,1,2,3")
    parser.add_argument("--timing-models", default="0,1,2")
    parser.add_argument("--cpu-type", default="X86O3CPU")
    parser.add_argument(
        "--use-noncaching-cpu",
        action="store_true",
        help="Use X86TimingSimpleCPU without --caches/--l2cache",
    )
    parser.add_argument("--mem-type", default="DDR4_2400_8x8")
    parser.add_argument("--mem-size", default="8192MB")
    parser.add_argument("--jobs", type=int, default=24)
    parser.add_argument("--output-dir", default="benchmark_results")
    args = parser.parse_args()

    project_root = Path(args.project_root).resolve()
    gem5_dir = (
        Path(args.gem5_dir).resolve()
        if args.gem5_dir
        else (project_root / "gem5").resolve()
    )
    macroworkloads_dir = project_root / "macroworkloads"
    output_dir = project_root / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    widths = [int(x) for x in args.widths.split(",") if x]
    sizes = [int(x) for x in args.sizes.split(",") if x]
    timing_models = [int(x) for x in args.timing_models.split(",") if x]

    if args.jobs < 1:
        raise ValueError("--jobs must be at least 1")

    compile_macroworkloads(macroworkloads_dir, args.jobs)
    pairs = discover_workload_pairs(macroworkloads_dir)
    for pair in pairs.values():
        ensure_executable(pair["baseline"])
        ensure_executable(pair["pim"])

    run_specs = build_run_specs(pairs, widths, sizes, timing_models)
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
                str(result["pair_name"]),
                int(result["width"]),
                int(result["size_exp"]),
                int(result["timing_model"]),
                str(result["variant"]),
            )
            completed_runs[run_key] = result

    rows_by_pair: dict[str, list[dict[str, int | float | str]]] = {
        pair_name: [] for pair_name in pairs
    }
    for pair_name, width, size_exp, timing_model in itertools.product(
        pairs.keys(), widths, sizes, timing_models
    ):
        baseline = completed_runs[
            (pair_name, width, size_exp, BASELINE_TIMING_MODEL, "baseline")
        ]
        pim = completed_runs[(pair_name, width, size_exp, timing_model, "pim")]
        rows_by_pair[pair_name].append(
            {
                "pair_name": pair_name,
                "width": width,
                "size_exp": size_exp,
                "timing_model": timing_model,
                "baseline_timing_model": BASELINE_TIMING_MODEL,
                "baseline_ticks": int(baseline["ticks"]),
                "pim_ticks": int(pim["ticks"]),
                "speedup": int(baseline["ticks"]) / int(pim["ticks"]),
                "baseline_stdout": str(baseline["stdout_path"]),
                "pim_stdout": str(pim["stdout_path"]),
            }
        )

    for pair_name, rows in rows_by_pair.items():
        rows.sort(
            key=lambda row: (
                int(row["timing_model"]),
                int(row["width"]),
                int(row["size_exp"]),
            )
        )
        pair_dir = output_dir / pair_name
        pair_dir.mkdir(parents=True, exist_ok=True)
        write_pair_summary(pair_dir, rows)
        plot_pair_speedup(pair_name, rows, widths, sizes, timing_models, pair_dir)

    print(f"Wrote benchmark results to {output_dir}")


if __name__ == "__main__":
    main()
