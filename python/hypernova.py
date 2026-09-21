"""
HyperNova Python Client & Binding Interface
Provides native Python access to the HyperNova Sovereign Optimization Solver
via direct C++ shared library ctypes bindings, with CLI fallback.
"""

import os
import sys
import ctypes
import json
import subprocess
from typing import Dict, Any, List, Optional

# Load C API DLL if available
def _load_c_api():
    dll_names = [
        "libhypernova_c_api.dll",
        "hypernova_c_api.dll",
        "libhypernova_c_api.so",
        "libhypernova_c_api.dylib"
    ]
    search_paths = [
        os.path.abspath("build/bin"),
        os.path.abspath("build/bin/Release"),
        os.path.abspath("build/bin/Debug"),
        os.path.dirname(__file__),
        os.path.abspath(".")
    ]
    for p in search_paths:
        for name in dll_names:
            full_path = os.path.join(p, name)
            if os.path.exists(full_path):
                try:
                    lib = ctypes.CDLL(full_path)
                    # Setup function signatures
                    lib.hypernova_problem_create.argtypes = [ctypes.c_char_p]
                    lib.hypernova_problem_create.restype = ctypes.c_void_p
                    lib.hypernova_problem_destroy.argtypes = [ctypes.c_void_p]

                    lib.hypernova_problem_add_variable.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double, ctypes.c_int, ctypes.c_char_p]
                    lib.hypernova_problem_add_variable.restype = ctypes.c_size_t

                    lib.hypernova_problem_add_constraint.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.c_double, ctypes.c_char_p]
                    lib.hypernova_problem_add_constraint.restype = ctypes.c_size_t

                    lib.hypernova_problem_set_objective.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_double), ctypes.c_int]

                    lib.hypernova_problem_add_quadratic_term.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_double]

                    lib.hypernova_options_create.restype = ctypes.c_void_p
                    lib.hypernova_options_destroy.argtypes = [ctypes.c_void_p]
                    lib.hypernova_options_set_time_limit.argtypes = [ctypes.c_void_p, ctypes.c_double]
                    lib.hypernova_options_set_mip_gap.argtypes = [ctypes.c_void_p, ctypes.c_double]
                    lib.hypernova_options_set_threads.argtypes = [ctypes.c_void_p, ctypes.c_int]
                    lib.hypernova_options_set_use_gpu.argtypes = [ctypes.c_void_p, ctypes.c_int]

                    lib.hypernova_solve.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
                    lib.hypernova_solve.restype = ctypes.c_void_p
                    lib.hypernova_solution_destroy.argtypes = [ctypes.c_void_p]

                    lib.hypernova_solution_get_status.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_status.restype = ctypes.c_int
                    lib.hypernova_solution_get_objective.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_objective.restype = ctypes.c_double
                    lib.hypernova_solution_get_best_bound.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_best_bound.restype = ctypes.c_double
                    lib.hypernova_solution_get_gap.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_gap.restype = ctypes.c_double
                    lib.hypernova_solution_get_solve_time_ms.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_solve_time_ms.restype = ctypes.c_double

                    lib.hypernova_solution_get_primal.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double), ctypes.c_size_t]
                    lib.hypernova_solution_get_primal.restype = ctypes.c_size_t

                    lib.hypernova_solution_get_dual.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double), ctypes.c_size_t]
                    lib.hypernova_solution_get_dual.restype = ctypes.c_size_t

                    lib.hypernova_solution_get_reduced_costs.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double), ctypes.c_size_t]
                    lib.hypernova_solution_get_reduced_costs.restype = ctypes.c_size_t

                    lib.hypernova_solution_get_backend_used.argtypes = [ctypes.c_void_p]
                    lib.hypernova_solution_get_backend_used.restype = ctypes.c_char_p

                    return lib
                except Exception:
                    pass
    return None

_c_lib = _load_c_api()

STATUS_MAP = {
    0: "UNKNOWN",
    1: "OPTIMAL",
    2: "INFEASIBLE",
    3: "UNBOUNDED",
    4: "SUBOPTIMAL",
    5: "TIME_LIMIT",
    6: "ITER_LIMIT",
    7: "NUMERICAL_ERROR",
    8: "INTERRUPTED"
}

VARTYPE_MAP = {
    "CONTINUOUS": 0,
    "INTEGER": 1,
    "BINARY": 2,
    "SEMI_CONTINUOUS": 3,
    "SEMI_INTEGER": 4
}

SENSE_MAP = {
    "LE": 0, "<=": 0,
    "GE": 1, ">=": 1,
    "EQ": 2, "=": 2, "==": 2
}

class Problem:
    def __init__(self, name: str = "hypernova_model"):
        self.name = name
        self.obj_sense = "MINIMIZE"
        self.variables = []
        self.constraints = []
        self.quadratic_terms = []

    def add_variable(self, lb: float = 0.0, ub: float = float('inf'), var_type: str = "CONTINUOUS", name: str = "") -> int:
        idx = len(self.variables)
        vname = name if name else f"x{idx}"
        self.variables.append({"index": idx, "name": vname, "lb": lb, "ub": ub, "type": var_type, "obj": 0.0})
        return idx

    def set_objective(self, coeffs: List[tuple], sense: str = "MINIMIZE"):
        self.obj_sense = sense
        for idx, val in coeffs:
            if idx < len(self.variables):
                self.variables[idx]["obj"] = val

    def add_constraint(self, coeffs: List[tuple], sense: str, rhs: float, name: str = "") -> int:
        idx = len(self.constraints)
        cname = name if name else f"c{idx}"
        self.constraints.append({"index": idx, "name": cname, "coeffs": coeffs, "sense": sense, "rhs": rhs})
        return idx

    def add_quadratic_term(self, row: int, col: int, coeff: float):
        self.quadratic_terms.append({"row": row, "col": col, "coeff": coeff})

    def to_lp_string(self) -> str:
        lines = []
        lines.append("Minimize" if self.obj_sense == "MINIMIZE" else "Maximize")
        obj_parts = []
        for v in self.variables:
            if v["obj"] != 0.0:
                obj_parts.append(f"{v['obj']:+} {v['name']}")
        if self.quadratic_terms:
            q_parts = []
            for q in self.quadratic_terms:
                rname = self.variables[q["row"]]["name"]
                cname = self.variables[q["col"]]["name"]
                if q["row"] == q["col"]:
                    q_parts.append(f"{q['coeff']:+} {rname} ^ 2")
                else:
                    q_parts.append(f"{q['coeff']:+} {rname} * {cname}")
            obj_parts.append("+ [ " + " ".join(q_parts) + " ] / 2")
        lines.append(" obj: " + (" ".join(obj_parts) if obj_parts else "0"))

        lines.append("Subject To")
        for c in self.constraints:
            c_parts = [f"{val:+} {self.variables[idx]['name']}" for idx, val in c["coeffs"]]
            s_symbol = "<=" if c["sense"] in ("LE", "<=") else (">=" if c["sense"] in ("GE", ">=") else "=")
            lines.append(f" {c['name']}: " + " ".join(c_parts) + f" {s_symbol} {c['rhs']}")

        lines.append("Bounds")
        for v in self.variables:
            lb_str = f"{v['lb']}" if v['lb'] != float('-inf') else "-inf"
            ub_str = f"{v['ub']}" if v['ub'] != float('inf') else "+inf"
            lines.append(f" {lb_str} <= {v['name']} <= {ub_str}")

        integers = [v['name'] for v in self.variables if v['type'] in ("INTEGER", "BINARY")]
        if integers:
            lines.append("General")
            lines.append(" " + " ".join(integers))

        lines.append("End")
        return "\n".join(lines)


class Solution:
    def __init__(self, data: Dict[str, Any]):
        self.status = data.get("status", "UNKNOWN")
        self.objective_value = data.get("objective_value", 0.0)
        self.best_bound = data.get("best_bound", 0.0)
        self.gap = data.get("gap", 0.0)
        self.primal = data.get("primal", [])
        self.dual = data.get("dual", [])
        self.reduced_costs = data.get("reduced_costs", [])
        self.solve_time_ms = data.get("solve_time_ms", 0.0)
        self.backend_used = data.get("backend_used", "native")

    def is_optimal(self) -> bool:
        return self.status == "OPTIMAL"


class Solver:
    def __init__(self, time_limit: float = 60.0, threads: int = 1, use_gpu: bool = False, presolve: str = "aggressive", scaling: str = "none", compute_target: str = "cpu", executable_path: Optional[str] = None):
        self.time_limit = time_limit
        self.threads = threads
        self.use_gpu = use_gpu
        self.presolve = presolve
        self.scaling = scaling
        self.compute_target = compute_target
        self.exe = executable_path or self._find_executable()

    def _find_executable(self) -> str:
        candidates = [
            "hypernova",
            "build/bin/hypernova",
            "build/bin/Debug/hypernova.exe",
            "build/bin/Release/hypernova.exe",
            "build/bin/hypernova.exe",
            "hypernova.exe"
        ]
        for c in candidates:
            if os.path.exists(c):
                return os.path.abspath(c)
        return "hypernova"

    def solve(self, problem: Problem) -> Solution:
        # Priority 8: If native C API DLL is loaded, solve entirely in-memory!
        if _c_lib is not None:
            return self._solve_native(problem)
        return self._solve_cli(problem)

    def _solve_native(self, problem: Problem) -> Solution:
        c_prob = _c_lib.hypernova_problem_create(problem.name.encode('utf-8'))
        c_opts = _c_lib.hypernova_options_create()
        try:
            # Build problem
            for v in problem.variables:
                vtype = VARTYPE_MAP.get(v["type"].upper(), 0)
                _c_lib.hypernova_problem_add_variable(
                    c_prob, ctypes.c_double(v["lb"]), ctypes.c_double(v["ub"]),
                    ctypes.c_int(vtype), v["name"].encode('utf-8')
                )

            # Objective
            obj_indices = []
            obj_coeffs = []
            for v in problem.variables:
                if v["obj"] != 0.0:
                    obj_indices.append(v["index"])
                    obj_coeffs.append(v["obj"])
            if obj_indices:
                idx_arr = (ctypes.c_size_t * len(obj_indices))(*obj_indices)
                val_arr = (ctypes.c_double * len(obj_coeffs))(*obj_coeffs)
                objsense = 1 if problem.obj_sense == "MAXIMIZE" else 0
                _c_lib.hypernova_problem_set_objective(c_prob, len(obj_indices), idx_arr, val_arr, objsense)

            # Constraints
            for c in problem.constraints:
                c_indices = [t[0] for t in c["coeffs"]]
                c_values = [t[1] for t in c["coeffs"]]
                idx_arr = (ctypes.c_size_t * len(c_indices))(*c_indices)
                val_arr = (ctypes.c_double * len(c_values))(*c_values)
                sense_val = SENSE_MAP.get(c["sense"].upper(), 0)
                _c_lib.hypernova_problem_add_constraint(
                    c_prob, len(c_indices), idx_arr, val_arr,
                    ctypes.c_int(sense_val), ctypes.c_double(c["rhs"]), c["name"].encode('utf-8')
                )

            # Quadratic terms
            for q in problem.quadratic_terms:
                _c_lib.hypernova_problem_add_quadratic_term(
                    c_prob, q["row"], q["col"], ctypes.c_double(q["coeff"])
                )

            # Options
            _c_lib.hypernova_options_set_time_limit(c_opts, ctypes.c_double(self.time_limit))
            _c_lib.hypernova_options_set_threads(c_opts, ctypes.c_int(self.threads))
            _c_lib.hypernova_options_set_use_gpu(c_opts, ctypes.c_int(1 if self.use_gpu else 0))

            # Execute Solve
            c_sol = _c_lib.hypernova_solve(c_prob, c_opts)
            try:
                status_code = _c_lib.hypernova_solution_get_status(c_sol)
                status_str = STATUS_MAP.get(status_code, "UNKNOWN")
                obj_val = _c_lib.hypernova_solution_get_objective(c_sol)
                best_bound = _c_lib.hypernova_solution_get_best_bound(c_sol)
                gap = _c_lib.hypernova_solution_get_gap(c_sol)
                solve_time = _c_lib.hypernova_solution_get_solve_time_ms(c_sol)

                n_vars = len(problem.variables)
                primal_arr = (ctypes.c_double * n_vars)()
                _c_lib.hypernova_solution_get_primal(c_sol, primal_arr, n_vars)

                n_cons = len(problem.constraints)
                dual_arr = (ctypes.c_double * n_cons)()
                _c_lib.hypernova_solution_get_dual(c_sol, dual_arr, n_cons)

                rc_arr = (ctypes.c_double * n_vars)()
                _c_lib.hypernova_solution_get_reduced_costs(c_sol, rc_arr, n_vars)

                backend_raw = _c_lib.hypernova_solution_get_backend_used(c_sol)
                backend_str = backend_raw.decode('utf-8') if isinstance(backend_raw, bytes) else str(backend_raw)

                return Solution({
                    "status": status_str,
                    "objective_value": obj_val,
                    "best_bound": best_bound,
                    "gap": gap,
                    "solve_time_ms": solve_time,
                    "primal": list(primal_arr),
                    "dual": list(dual_arr),
                    "reduced_costs": list(rc_arr),
                    "backend_used": backend_str
                })
            finally:
                _c_lib.hypernova_solution_destroy(c_sol)
        finally:
            _c_lib.hypernova_options_destroy(c_opts)
            _c_lib.hypernova_problem_destroy(c_prob)

    def _solve_cli(self, problem: Problem) -> Solution:
        lp_content = problem.to_lp_string()
        tmp_lp = f"_tmp_{problem.name}.lp"
        tmp_rep = f"_tmp_{problem.name}_report.json"
        try:
            with open(tmp_lp, "w") as f:
                f.write(lp_content)

            cmd = [
                self.exe, "solve", tmp_lp,
                "--time-limit", str(self.time_limit),
                "--threads", str(self.threads),
                "--presolve", self.presolve,
                "--scaling", self.scaling,
                "--compute-target", self.compute_target,
                "--report", tmp_rep
            ]
            if self.use_gpu:
                cmd.extend(["--gpu", "on"])

            proc = subprocess.run(cmd, capture_output=True, text=True)
            if os.path.exists(tmp_rep):
                with open(tmp_rep, "r") as f:
                    data = json.load(f)
                return Solution(data)
            else:
                raise RuntimeError(f"Solver failed to produce a report. Exit code: {proc.returncode}. Stderr: {proc.stderr}")
        finally:
            if os.path.exists(tmp_lp):
                os.remove(tmp_lp)
            if os.path.exists(tmp_rep):
                os.remove(tmp_rep)
