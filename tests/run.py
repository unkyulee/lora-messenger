"""Run host tests with a pinned Zig C++ compiler: uv run --with ziglang==0.13.0 tests/run.py"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = root / "build"
build.mkdir(exist_ok=True)
exe = build / ("chat-tests.exe" if sys.platform == "win32" else "chat-tests")
subprocess.run([
    sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "include"), "-I", str(root / "tests/fakes"),
    str(root / "tests/test_chat.cpp"), "-o", str(exe),
], check=True, cwd=root)
subprocess.run([str(exe)], check=True, cwd=root)
