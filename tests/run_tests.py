#!/usr/bin/env python3
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent


def expected_mode(source):
    text = source.read_text()
    for line in text.splitlines()[:5]:
        if "NSAN_EXPECT:" in line:
            return line.split("NSAN_EXPECT:", 1)[1].strip()
    return "detect"


def compile_and_run(clang, source, out_dir):
    binary = out_dir / source.stem
    cmd = [clang, "-fsanitize=numerical", "-O1", "-g", str(source), "-o", str(binary)]
    compile_result = subprocess.run(cmd, text=True, capture_output=True)
    if compile_result.returncode != 0:
        return False, "compile failed", compile_result.stderr

    run_result = subprocess.run([str(binary)], text=True, capture_output=True)
    detected = "NumericalSanitizer" in run_result.stderr
    expect = expected_mode(source)
    output = run_result.stdout + run_result.stderr
    if expect == "no-diagnostic":
        ok = not detected
        return ok, "clean" if ok else "unexpected diagnostic", output
    return detected, "detected" if detected else "no diagnostic", output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    args = parser.parse_args()

    cases = sorted((ROOT / "cases").glob("*.c"))
    if not cases:
        print("no test cases found", file=sys.stderr)
        return 1

    failures = []
    with tempfile.TemporaryDirectory(prefix="nsan-tests-") as tmp:
        out_dir = pathlib.Path(tmp)
        for case in cases:
            ok, status, output = compile_and_run(args.clang, case, out_dir)
            print(f"{case.name}: {status}")
            if not ok:
                failures.append((case.name, output))

    if failures:
        for name, output in failures:
            print(f"\n--- {name} ---")
            print(output[-4000:])
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
