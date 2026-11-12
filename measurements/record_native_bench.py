#!/usr/bin/env python3
"""Runs build/engine/jam_bench and saves the result with the conditions it ran
under. Power source and core type change the numbers by up to 4x on Apple
silicon, so they are part of the measurement.

  python3 measurements/record_native_bench.py            # normal priority
  python3 measurements/record_native_bench.py --efficiency-cores
"""
import json
import subprocess
import sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sh(cmd: str) -> str:
    return subprocess.check_output(cmd, shell=True, text=True, cwd=ROOT).strip()


def main() -> None:
    efficiency = "--efficiency-cores" in sys.argv
    bench = str(ROOT / "build/engine/jam_bench")
    cmd = ["taskpolicy", "-b", bench] if efficiency else [bench]
    result = json.loads(subprocess.check_output(cmd, text=True))
    cpu = sh("sysctl -n machdep.cpu.brand_string")
    out = {
        "what": "Engine CPU cost per voice: extra render time of N sustained voices over an idle engine, "
        "best of 5 runs, single thread, native Release build",
        "measured_on": {
            "date": date.today().isoformat(),
            "cpu": cpu,
            "cores": "efficiency cores (taskpolicy -b)" if efficiency else "scheduler's choice at normal priority",
            "power": sh("pmset -g batt | head -1 | sed -E \"s/.*'(.*)'.*/\\1/\""),
            "low_power_mode": sh("pmset -g | awk '/lowpowermode/ {print $2}'") == "1",
            "macos": sh("sw_vers -productVersion"),
            "compiler": sh("clang++ --version | head -1"),
            "engine_commit": sh("git rev-parse --short HEAD"),
        },
        "command": "python3 measurements/record_native_bench.py" + (" --efficiency-cores" if efficiency else ""),
        **result,
    }
    slug = cpu.lower().replace(" ", "-")
    name = f"engine-cpu-{slug}{'-efficiency-cores' if efficiency else ''}.json"
    (ROOT / "measurements" / name).write_text(json.dumps(out, indent=2) + "\n")
    print(f"wrote measurements/{name}")


if __name__ == "__main__":
    main()
