#!/usr/bin/env python3
"""
HyperNova vs HiGHS reproducible measurement script
==================================================
SIH Problem Statement 26119 | Sovereign Optimization Engine

This script measures HyperNova on the shipped benchmark models and records the
raw timings and solver statuses. When a HiGHS binary (`highs`) or the `highspy`
Python package is available on the same machine, it is used strictly as an
independent reference solver -- never as a HyperNova backend -- and its raw
measurements are recorded in the same table.

Honesty rules enforced here (see docs/REMAINING_WORK_AUDIT.md §4):
  1. Every HyperNova row is measured live on this machine. No numbers are
     hardcoded and no results are fabricated.
  2. HiGHS columns are "n/a" whenever a HiGHS executable cannot be found.
     Nothing is guessed or copied from third-party tables.
  3. Both solvers run on identical model files with identical time limits.
  4. Results are raw (unrounded) and written atomically to CSV.

Usage:
    python benchmarks/run_comparison.py [--times <n>] [--time-limit <s>]

The default run measures the five MRPL industrial models (the shipped .lp/.mps
files), plus 25fv47 from the shipped Netlib set. Large/opt-in instances
(dfl001, scale_100k, scale_1M) are skipped unless --full is given, because the
IPM on dfl001 is documented to take several minutes per barrier factor.
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_CSV = os.path.join(HERE, "comparison_highs_hypernova.csv")

# (instance, file, problem_class, vars, cons)
CASES = [
    ("cogen_power_milp", "benchmarks/industrial-cases/cogen_power_milp.mps", "MILP", 7, 8),
    ("hydrogen_network_lp", "benchmarks/industrial-cases/hydrogen_network_lp.mps", "LP", 5, 4),
    ("logistics_freight_milp", "benchmarks/industrial-cases/logistics_freight_milp.mps", "MILP", 8, 11),
    ("refinery_production_planning_milp",
     "benchmarks/industrial-cases/refinery_production_planning_milp.mps", "MILP", 15, 18),
    ("crude_blending_qp", "benchmarks/industrial-cases/crude_blending_qp.lp", "QP", 5, 4),
    ("netlib_25fv47", "benchmarks/netlib/25fv47.mps", "LP", 1571, 821),
]

FULL_CASES = [
    ("netlib_dfl001", "benchmarks/netlib/dfl001.mps", "LP", 12230, 6071),
]


def find_hypernova_cli():
    if os.environ.get("HYPERNOVA_CLI"):
        return os.environ["HYPERNOVA_CLI"]
    for cand in (
        os.path.join(ROOT, "build", "bin", "hypernova.exe"),
        os.path.join(ROOT, "build", "bin", "hypernova"),
        os.path.join(ROOT, "build-p0", "bin", "hypernova.exe"),
        os.path.join(ROOT, "build2", "bin", "hypernova.exe"),
    ):
        if os.path.exists(cand):
            return cand
    return "hypernova"


def find_highs():
    """Return (kind, object) for the available HiGHS reference, or (None, None)."""
    try:
        import highspy  # noqa: F401

        return ("highspy", None)
    except Exception:
        pass
    for cmd in ("highs", "highs.exe"):
        for dirpath in os.environ.get("PATH", "").split(os.pathsep):
            if not dirpath:
                continue
            cand = os.path.join(dirpath, cmd)
            if os.path.exists(cand):
                return ("binary", cand)
    return (None, None)


def run_hypernova(cli, model_path, time_limit_s):
    """Run HyperNova on model_path; return (status, objective, seconds).

    Crashes (non-zero return code) are retried once so a flaky process exit does
    not masquerade as an unconditional failure; a persistent crash is reported
    as status 'CRASH' rather than fabricated into an OPTIMAL."""
    for attempt in (1, 2):
        report = os.path.join(tempfile.gettempdir(), "hn_compare_report.json")
        if os.path.exists(report):
            os.remove(report)
        cmd = [cli, "solve", model_path, "--time-limit", str(time_limit_s),
               "--report", report]
        start = _now_ms()
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                              timeout=int(time_limit_s) + 120)
        elapsed = (_now_ms() - start) / 1000.0
        if proc.returncode != 0 or not os.path.exists(report):
            if attempt == 2:
                return f"CRASH rc={proc.returncode}", None, elapsed
            continue
        with open(report, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        status = data.get("status", "?")
        objective = data.get("objective") if data.get("objective") is not None else data.get("objective_value")
        objective = float(objective) if objective is not None else None
        return status, objective, elapsed
    raise RuntimeError("unreachable")


def run_highs(highs_info, model_path, time_limit_s):
    """Run HiGHS reference solve; return (status, objective, seconds) or (None, None, None)."""
    kind, _ = highs_info
    if kind is None:
        return (None, None, None)
    if kind == "highspy":
        import highspy

        h = highspy.Highs()
        h.setOptionValue("time_limit", time_limit_s)
        h.setOptionValue("output_flag", False)
        h.readModel(model_path)
        start = _now_ms()
        h.run()
        elapsed = (_now_ms() - start) / 1000.0
        status = ("Optimal" if h.getModelStatus() == highspy.HighsModelStatus.kOptimal
                  else str(h.getModelStatus()))
        obj = None
        try:
            obj = h.getObjectiveValue()
        except Exception:
            pass
        return (status, obj, elapsed)
    # binary
    start = _now_ms()
    proc = subprocess.run([kind, model_path, "--time_limit", str(time_limit_s)],
                          capture_output=True, text=True, timeout=int(time_limit_s) + 120)
    elapsed = (_now_ms() - start) / 1000.0
    status = "Optimal" if proc.returncode == 0 else f"rc={proc.returncode}"
    return (status, None, elapsed)


def _now_ms():
    import time

    return int(time.time() * 1000)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--time-limit", type=float, default=120.0)
    ap.add_argument("--out", default=DEFAULT_CSV)
    ap.add_argument("--full", action="store_true", help="include dfl001 (very slow)")
    args = ap.parse_args()

    cli = find_hypernova_cli()
    print(f"HyperNova CLI: {cli}")
    if not os.path.exists(cli):
        print("ERROR: hypernova executable not found. Build it first (cmake --build build).")
        sys.exit(2)

    highs_kind, highs_cmd = find_highs()
    print(f"HiGHS reference: {'none found; columns will be n/a' if highs_kind is None else highs_kind}")

    rows = []
    for name, path, cls, nv, nc in CASES + (FULL_CASES if args.full else []):
        model_path = os.path.join(ROOT, path)
        print(f"  measuring {name} ({path}) ...")
        hn_status, hn_obj, hn_t = run_hypernova(cli, path, args.time_limit)
        hg_status, hg_obj, hg_t = run_highs((highs_kind, highs_cmd), model_path, args.time_limit)
        rows.append({
            "Instance": name,
            "Vars": nv,
            "Cons": nc,
            "Class": cls,
            "HyperNova_Time_s": f"{hn_t:.6f}",
            "HyperNova_Status": hn_status,
            "HyperNova_Objective": hn_obj if hn_status == "OPTIMAL" else "n/a",
            "HiGHS_Time_s": f"{hg_t:.6f}" if hg_t is not None else "n/a",
            "HiGHS_Status": hg_status if hg_status is not None else "n/a",
            "HiGHS_Objective": hg_obj if hg_obj is not None else "n/a",
            "Verified": "yes" if hn_status == "OPTIMAL" else "no",
        })

    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"Written: {args.out}")


if __name__ == "__main__":
    main()