#!/usr/bin/env python3
import argparse
import pathlib
import statistics
import subprocess
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parent


def log(msg):
    print(msg, flush=True)


# Smaller workload sizes for the dashboard "snapshot" demo. These keep the
# instrumented runs to roughly 1-3 seconds each so the demo feels live without
# losing the demonstrative overhead ratio.
QUICK_SIZES = {
    "dot_product.c": 500,
    "harmonic.c": 500,
    "matmul.c": 8,
}


def build(clang, source, output, instrumented, define_n=None):
    cmd = [clang, "-O2", "-g", str(source), "-o", str(output)]
    if define_n is not None:
        cmd.insert(1, f"-DN={define_n}")
    if instrumented:
        cmd.insert(1, "-fsanitize=numerical")
    subprocess.run(cmd, check=True)


def time_binary(binary, iterations):
    times = []
    for _ in range(iterations):
        start = time.perf_counter()
        subprocess.run(
            [str(binary)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        times.append(time.perf_counter() - start)
    return statistics.median(times)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress progress lines; only print the CSV table.",
    )
    parser.add_argument(
        "--quick",
        action="store_true",
        help="Use smaller workload sizes (dashboard snapshot mode).",
    )
    args = parser.parse_args()

    programs = sorted(ROOT.glob("*.c"))
    with tempfile.TemporaryDirectory(prefix="nsan-bench-") as tmp:
        out = pathlib.Path(tmp)
        if not args.quiet:
            mode = "quick" if args.quick else "full"
            log(
                f"# {len(programs)} programs, {args.iterations} iteration(s) "
                f"each, mode={mode}"
            )
        log("program,baseline_s,instrumented_s,overhead")
        for program in programs:
            base = out / f"{program.stem}.base"
            inst = out / f"{program.stem}.nsan"
            n = QUICK_SIZES.get(program.name) if args.quick else None
            if not args.quiet:
                suffix = f" (N={n})" if n is not None else ""
                log(f"# [{program.name}] compiling baseline{suffix}...")
            build(args.clang, program, base, instrumented=False, define_n=n)
            if not args.quiet:
                log(f"# [{program.name}] compiling instrumented...")
            build(args.clang, program, inst, instrumented=True, define_n=n)
            if not args.quiet:
                log(f"# [{program.name}] timing baseline...")
            base_time = time_binary(base, args.iterations)
            if not args.quiet:
                log(f"# [{program.name}] timing instrumented...")
            inst_time = time_binary(inst, args.iterations)
            log(
                f"{program.name},{base_time:.6f},{inst_time:.6f},"
                f"{inst_time / base_time:.2f}x"
            )


if __name__ == "__main__":
    main()
