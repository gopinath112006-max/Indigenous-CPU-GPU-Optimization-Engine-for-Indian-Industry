#!/usr/bin/env python3
"""
HyperNova Web Console & REST Cloud API Server
=============================================
SIH Problem Statement 26119 | Sovereign Optimization Engine

Provides a REST API and Web Console UI for model upload, solve execution,
capability inspection, solution verification, and telemetry reporting.
"""

import http.server
import socketserver
import json
import os
import subprocess
import tempfile
import urllib.parse
import sys

PORT = 8080
HYPERNOVA_CLI = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "bin", "hypernova.exe"))
if not os.path.exists(HYPERNOVA_CLI):
    # Try current directory build path
    HYPERNOVA_CLI = "hypernova"

CONSOLE_DIR = os.path.dirname(os.path.abspath(__file__))

class HyperNovaAPIHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=CONSOLE_DIR, **kwargs)

    def _set_json_headers(self, status=200):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_OPTIONS(self):
        self._set_json_headers(200)

    def do_GET(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path

        if path == "/api/v1/capabilities":
            self.handle_capabilities()
        elif path == "/api/v1/health":
            self.handle_health()
        else:
            # Fallback to static file serving
            if path == "/":
                self.path = "/index.html"
            super().do_GET()

    def do_POST(self):
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path

        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length)

        if path == "/api/v1/solve":
            self.handle_solve(post_data)
        elif path == "/api/v1/inspect":
            self.handle_inspect(post_data)
        else:
            self._set_json_headers(404)
            self.wfile.write(json.dumps({"error": "Endpoint not found"}).encode("utf-8"))

    def handle_health(self):
        self._set_json_headers(200)
        self.wfile.write(json.dumps({
            "status": "online",
            "solver_version": "0.1.0",
            "cli_path": HYPERNOVA_CLI,
            "cli_exists": os.path.exists(HYPERNOVA_CLI)
        }).encode("utf-8"))

    def handle_capabilities(self):
        try:
            cmd = [HYPERNOVA_CLI, "capabilities"]
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            self._set_json_headers(200)
            self.wfile.write(json.dumps({
                "output": proc.stdout,
                "engines": ["AUTO", "PRIMAL_SIMPLEX", "DUAL_SIMPLEX", "INTERIOR_POINT", "BRANCH_AND_BOUND", "BRANCH_AND_CUT", "QP_ACTIVE_SET", "QP_INTERIOR_POINT"],
                "backends": ["CPU", "CUDA", "HIP", "SYCL"]
            }).encode("utf-8"))
        except Exception as e:
            self._set_json_headers(500)
            self.wfile.write(json.dumps({"error": str(e)}).encode("utf-8"))

    def handle_solve(self, post_data):
        try:
            body = json.loads(post_data.decode("utf-8"))
            model_content = body.get("model_content", "")
            file_format = body.get("format", "mps").lower()
            engine = body.get("engine", "auto").lower()
            time_limit = str(body.get("time_limit", 3600))
            threads = str(body.get("threads", 0))

            with tempfile.TemporaryDirectory() as tmpdir:
                model_filename = os.path.join(tmpdir, f"model.{file_format}")
                sol_filename = os.path.join(tmpdir, "solution.sol")
                report_filename = os.path.join(tmpdir, "report.json")

                with open(model_filename, "w", encoding="utf-8") as f:
                    f.write(model_content)

                cmd = [
                    HYPERNOVA_CLI, "solve", model_filename,
                    "--engine", engine,
                    "--time-limit", time_limit,
                    "--out", sol_filename,
                    "--report", report_filename
                ]
                if threads != "0":
                    cmd.extend(["--threads", threads])

                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)

                solution_data = {}
                report_data = {}

                if os.path.exists(sol_filename):
                    with open(sol_filename, "r", encoding="utf-8") as sf:
                        solution_data = json.load(sf)

                if os.path.exists(report_filename):
                    with open(report_filename, "r", encoding="utf-8") as rf:
                        report_data = json.load(rf)

                self._set_json_headers(200)
                self.wfile.write(json.dumps({
                    "stdout": proc.stdout,
                    "stderr": proc.stderr,
                    "exit_code": proc.returncode,
                    "solution": solution_data,
                    "report": report_data
                }).encode("utf-8"))
        except Exception as e:
            self._set_json_headers(500)
            self.wfile.write(json.dumps({"error": str(e)}).encode("utf-8"))

    def handle_inspect(self, post_data):
        try:
            body = json.loads(post_data.decode("utf-8"))
            model_content = body.get("model_content", "")
            file_format = body.get("format", "mps").lower()

            with tempfile.TemporaryDirectory() as tmpdir:
                model_filename = os.path.join(tmpdir, f"model.{file_format}")
                with open(model_filename, "w", encoding="utf-8") as f:
                    f.write(model_content)

                cmd = [HYPERNOVA_CLI, "inspect", model_filename]
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10)

                self._set_json_headers(200)
                self.wfile.write(json.dumps({
                    "inspect_output": proc.stdout,
                    "exit_code": proc.returncode
                }).encode("utf-8"))
        except Exception as e:
            self._set_json_headers(500)
            self.wfile.write(json.dumps({"error": str(e)}).encode("utf-8"))

def run_server():
    print("================================================================================")
    print(" HyperNova Sovereign Optimization Engine — Web Console & Cloud REST Server")
    print(f" Server running at http://localhost:{PORT}")
    print(f" Using HyperNova CLI binary: {HYPERNOVA_CLI}")
    print("================================================================================")
    with socketserver.TCPServer(("", PORT), HyperNovaAPIHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")

if __name__ == "__main__":
    run_server()
