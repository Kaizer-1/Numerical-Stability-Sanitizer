#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import textwrap


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_CLANG = ROOT / "build" / "numerical-clang"
EXAMPLE = ROOT / "examples" / "terminal_input.c"
TEST_DIR = ROOT / "tests" / "cases"
BENCHMARK_RUNNER = ROOT / "benchmarks" / "run_benchmarks.py"
TEST_RUNNER = ROOT / "tests" / "run_tests.py"
REPORT = ROOT / "docs" / "project_report.md"


class Style:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    CYAN = "\033[36m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    BLUE = "\033[34m"


def use_color() -> bool:
    return sys.stdout.isatty() and os.environ.get("TERM") not in (None, "dumb")


COLOR = use_color()


def color(text: str, code: str) -> str:
    if not COLOR:
        return text
    return f"{code}{text}{Style.RESET}"


def clear_screen() -> None:
    if sys.stdout.isatty():
        sys.stdout.write("\033[2J\033[H")
        sys.stdout.flush()


def hr(width: int = 78, char: str = "=") -> str:
    return char * width


def box(title: str, body: str, width: int = 78) -> str:
    inner = width - 4
    title_line = f"| {title[:inner]:<{inner}} |"
    lines = []
    for raw in body.splitlines() or [""]:
        wrapped = textwrap.wrap(raw, inner) or [""]
        for line in wrapped:
            lines.append(f"| {line:<{inner}} |")
    top = "+" + "-" * (width - 2) + "+"
    return "\n".join([top, title_line, top] + lines + [top])


def pause() -> None:
    input(color("\nPress Enter to continue...", Style.DIM))


def run_command(cmd, *, input_text=None, env=None):
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        cmd,
        text=True,
        input=input_text,
        capture_output=True,
        cwd=ROOT,
        env=merged_env,
    )


def run_command_streaming(cmd, *, env=None):
    """Run cmd and stream its stdout line-by-line to the terminal as it runs.

    Useful for long-running commands so the user can see progress instead of
    waiting on a silent capture. Returns the collected (stdout, stderr, code).
    """
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=ROOT,
        env=merged_env,
        bufsize=1,
    )
    stdout_lines = []
    assert proc.stdout is not None
    for line in proc.stdout:
        stdout_lines.append(line)
        sys.stdout.write(line)
        sys.stdout.flush()
    stderr = proc.stderr.read() if proc.stderr else ""
    proc.wait()
    return "".join(stdout_lines), stderr, proc.returncode


def compile_source(source: pathlib.Path, output: pathlib.Path):
    return run_command(
        [str(BUILD_CLANG), "-fsanitize=numerical", "-g", str(source), "-o", str(output)]
    )


def status_panel() -> str:
    lines = []
    lines.append(
        f"build/numerical-clang: {'ready' if BUILD_CLANG.exists() else 'missing'}"
    )
    lines.append(f"tests available: {len(sorted(TEST_DIR.glob('*.c')))}")
    lines.append(f"project report: {'ready' if REPORT.exists() else 'missing'}")
    return box("Project Status", "\n".join(lines))


def menu_panel() -> str:
    body = "\n".join(
        [
            "[1] Interactive cancellation demo",
            "[2] JSON diagnostic demo",
            "[3] Run a curated test case",
            "[4] Run full test suite",
            "[5] Run benchmark snapshot",
            "[6] Show architecture and scope summary",
            "[7] Open project report path",
            "[q] Quit dashboard",
        ]
    )
    return box("Demo Menu", body)


def dashboard() -> None:
    while True:
        clear_screen()
        title = color("Numerical Stability Sanitizer Presentation Dashboard", Style.BOLD)
        print(title)
        print(color(hr(), Style.CYAN))
        print(status_panel())
        print()
        print(menu_panel())
        choice = input(color("\nSelect an option: ", Style.GREEN)).strip().lower()
        if choice == "1":
            interactive_demo(json_mode=False)
        elif choice == "2":
            interactive_demo(json_mode=True)
        elif choice == "3":
            curated_case_demo()
        elif choice == "4":
            run_full_suite()
        elif choice == "5":
            run_benchmarks()
        elif choice == "6":
            show_summary()
        elif choice == "7":
            show_report_path()
        elif choice == "q":
            clear_screen()
            return


def interactive_demo(*, json_mode: bool) -> None:
    clear_screen()
    mode = "JSON" if json_mode else "Text"
    print(color(f"Interactive Demo ({mode} diagnostics)", Style.BOLD))
    print(color(hr(), Style.CYAN))
    big = input("Enter big value [100000000]: ").strip() or "100000000"
    tiny = input("Enter tiny value [1]: ").strip() or "1"

    with tempfile.TemporaryDirectory(prefix="nsan-dashboard-") as tmp:
        binary = pathlib.Path(tmp) / "terminal_demo"
        compile_result = compile_source(EXAMPLE, binary)
        if compile_result.returncode != 0:
            print(compile_result.stderr)
            pause()
            return

        env = {"NSAN_REPORT_FORMAT": "json"} if json_mode else None
        run_result = run_command([str(binary)], input_text=f"{big} {tiny}\n", env=env)
        print()
        print(box("Command", f"{BUILD_CLANG} -fsanitize=numerical -g {EXAMPLE} -o {binary}"))
        print()
        print(box("Program Output", (run_result.stdout + run_result.stderr).strip() or "<no output>"))
        pause()


def curated_case_demo() -> None:
    cases = sorted(TEST_DIR.glob("*.c"))
    clear_screen()
    print(color("Curated Test Cases", Style.BOLD))
    print(color(hr(), Style.CYAN))
    for index, case in enumerate(cases, start=1):
        print(f"[{index:02d}] {case.name}")
    raw = input("\nChoose case number [1]: ").strip() or "1"
    try:
        selected = cases[max(0, min(len(cases) - 1, int(raw) - 1))]
    except ValueError:
        selected = cases[0]

    with tempfile.TemporaryDirectory(prefix="nsan-dashboard-") as tmp:
        binary = pathlib.Path(tmp) / selected.stem
        compile_result = compile_source(selected, binary)
        if compile_result.returncode != 0:
            print(compile_result.stderr)
            pause()
            return
        run_result = run_command([str(binary)])
        print()
        print(box("Selected Case", str(selected.relative_to(ROOT))))
        print()
        print(box("Program Output", (run_result.stdout + run_result.stderr).strip() or "<no output>"))
        pause()


def run_full_suite() -> None:
    clear_screen()
    print(color("Running Full Test Suite", Style.BOLD))
    print(color(hr(), Style.CYAN))
    result = run_command([sys.executable, str(TEST_RUNNER), "--clang", str(BUILD_CLANG)])
    print(box("Test Runner Output", (result.stdout + result.stderr).strip() or "<no output>"))
    pause()


def run_benchmarks() -> None:
    clear_screen()
    print(color("Running Benchmark Snapshot", Style.BOLD))
    print(color(hr(), Style.CYAN))
    print(
        color(
            "Quick mode: smaller workloads, 1 iteration each, progress streams\n"
            "below. Roughly 5-15 seconds total. For the full-size numbers\n"
            "shown in the README, run:\n"
            "  python3 benchmarks/run_benchmarks.py --clang build/numerical-clang\n",
            Style.DIM,
        )
    )
    stdout, stderr, code = run_command_streaming(
        [
            sys.executable,
            "-u",
            str(BENCHMARK_RUNNER),
            "--clang",
            str(BUILD_CLANG),
            "--iterations",
            "1",
            "--quick",
        ]
    )
    print()
    if code != 0:
        print(box("Benchmark Errors", stderr.strip() or "<no error output>"))
    else:
        csv_lines = [
            line for line in stdout.splitlines() if line and not line.startswith("#")
        ]
        print(box("Final Results", "\n".join(csv_lines) or "<no output>"))
    pause()


def show_summary() -> None:
    clear_screen()
    print(color("Architecture and Scope Summary", Style.BOLD))
    print(color(hr(), Style.CYAN))
    body = "\n".join(
        [
            "1. LLVM pass instruments float operations and computes double shadows.",
            "2. Runtime stores shadows for float memory and reports divergence.",
            "3. Wrapper exposes -fsanitize=numerical using the pass plugin.",
            "4. Current scope includes scalar floats, fixed-width vectors, direct calls, and common math models.",
            "5. Remaining future work is mostly production-grade compiler/runtime engineering.",
        ]
    )
    print(box("Project Summary", body))
    pause()


def show_report_path() -> None:
    clear_screen()
    print(color("Detailed Project Report", Style.BOLD))
    print(color(hr(), Style.CYAN))
    body = f"Open this file during preparation or presentation:\n{REPORT}"
    print(box("Report Path", body))
    pause()


def main() -> int:
    if not shutil.which("python3") and sys.executable is None:
        print("Python is required to run the dashboard.", file=sys.stderr)
        return 1
    if not BUILD_CLANG.exists():
        print(
            f"Expected sanitizer wrapper at {BUILD_CLANG}. Build the project first.",
            file=sys.stderr,
        )
        return 1
    dashboard()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
