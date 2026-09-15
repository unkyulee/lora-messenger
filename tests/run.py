"""Run host tests with a pinned Zig C++ compiler: uv run --with ziglang==0.13.0 tests/run.py"""
from pathlib import Path
import argparse
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--compile-only", action="store_true", help="Compile without executing the native test program")
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
build = root / "build"
build.mkdir(exist_ok=True)
exe = build / ("chat-tests.exe" if sys.platform == "win32" else "chat-tests")
subprocess.run([
    sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "include"), "-I", str(root / "tests/fakes"),
    str(root / "tests/test_chat.cpp"), "-o", str(exe),
], check=True, cwd=root)
if args.compile_only:
    print(f"Tests compiled successfully: {exe} (execution not requested)")
else:
    subprocess.run([str(exe)], check=True, cwd=root)
