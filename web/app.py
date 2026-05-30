import os
import pathlib
import subprocess
import tempfile

from flask import Flask, jsonify, request, send_from_directory

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
CLANG = str(PROJECT_ROOT / "build" / "numerical-clang")
WEB_DIR = str(pathlib.Path(__file__).resolve().parent)

app = Flask(__name__, static_folder=WEB_DIR, static_url_path="")


@app.route("/")
def index():
    return send_from_directory(WEB_DIR, "index.html")


@app.route("/run", methods=["POST"])
def run_code():
    data = request.get_json(force=True) or {}
    code = data.get("code", "")

    if not code.strip():
        return jsonify({"status": "error", "message": "No code provided."}), 400
    if len(code) > 65536:
        return jsonify({"status": "error", "message": "Code exceeds 64 KB limit."}), 400

    with tempfile.TemporaryDirectory(prefix="nsan_web_") as tmpdir:
        src = os.path.join(tmpdir, "input.c")
        binary = os.path.join(tmpdir, "out")

        with open(src, "w") as f:
            f.write(code)

        # ── Compile ──────────────────────────────────────────
        try:
            cp = subprocess.run(
                [CLANG, "-fsanitize=numerical", "-O1", "-g", src, "-o", binary, "-lm"],
                capture_output=True,
                text=True,
                timeout=30,
            )
        except subprocess.TimeoutExpired:
            return jsonify({
                "status": "compile_error",
                "compile_stderr": "Compilation timed out (30 s limit).",
                "stdout": "",
                "stderr": "",
            })
        except FileNotFoundError:
            return jsonify({
                "status": "compile_error",
                "compile_stderr": (
                    f"Compiler not found at:\n  {CLANG}\n\n"
                    "Run  ./build.sh  from the project root first."
                ),
                "stdout": "",
                "stderr": "",
            })

        # Strip temp path from messages so output is clean
        def clean(s):
            return s.replace(tmpdir + "/", "")

        if cp.returncode != 0:
            return jsonify({
                "status": "compile_error",
                "compile_stderr": clean(cp.stderr or cp.stdout),
                "stdout": "",
                "stderr": "",
            })

        compile_warnings = clean(cp.stderr)

        # ── Run ──────────────────────────────────────────────
        env = os.environ.copy()
        env.pop("NSAN_REPORT_FORMAT", None)  # force text mode

        try:
            rp = subprocess.run(
                [binary],
                capture_output=True,
                text=True,
                timeout=15,
                env=env,
            )
        except subprocess.TimeoutExpired:
            return jsonify({
                "status": "runtime_error",
                "compile_stderr": compile_warnings,
                "stdout": "",
                "stderr": "Program timed out (15 s limit).",
                "returncode": -1,
            })

        return jsonify({
            "status": "success",
            "compile_stderr": compile_warnings,
            "stdout": clean(rp.stdout),
            "stderr": clean(rp.stderr),
            "returncode": rp.returncode,
        })


if __name__ == "__main__":
    print(f"  Project root : {PROJECT_ROOT}")
    print(f"  Compiler     : {CLANG}")
    print(f"  Frontend     : http://127.0.0.1:5000")
    app.run(debug=False, port=5000)
