#!/usr/bin/env python3
"""
GBA Emulator Compilation Builder — local web server (optional but faster)
========================================================================

Runs the Python 3 builder scripts from:
  https://github.com/patters-match/gba-emu-compilation-builders

through a beginner-friendly web interface.

You don't strictly need this server: index.html also works standalone in a
modern browser (the scripts run in-browser via Pyodide). Start the server if
you want faster starts and support for very large CD-ROM images.

No third-party packages are required — standard library only (Python 3.8+).

Usage:
  python3 server.py            # then open http://localhost:8000
"""

import json
import os
import re
import shutil
import sys
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import builder_core
from builder_core import (
    BUILDERS, BUILDERS_BY_ID, ApiError, JOBS_DIR, MAX_UPLOAD_BYTES,
    job_dir, out_dir, uploads_dir, valid_job_id, sanitize_filename,
)

APP_DIR = os.path.dirname(os.path.abspath(__file__))
INDEX_FILE = os.path.join(APP_DIR, "index.html")

HOST = "0.0.0.0"
PORT = int(os.environ.get("PORT", "8000"))

FETCH_TIMEOUT = 120


# --------------------------------------------------------------------------
# Emulator auto-download (server side; the browser mode has its own fetcher)
# --------------------------------------------------------------------------

def download(url, dest, timeout=FETCH_TIMEOUT):
    req = urllib.request.Request(url, headers={"User-Agent": "gba-compilation-builder"})
    with urllib.request.urlopen(req, timeout=timeout) as resp, open(dest, "wb") as fh:
        shutil.copyfileobj(resp, fh)


def fetch_emu(builder, job):
    """Download the recommended emulator binary (or HVCA bin folder).
    Returns {'name': ..., 'size': ...} stored in the job's uploads dir."""
    emu = builder["emu"]
    if not (emu.get("fetch") or emu.get("fetch_folder")):
        raise ApiError("No automatic download available for this emulator — please add the file yourself.")
    updir = uploads_dir(job)
    os.makedirs(updir, exist_ok=True)

    if "fetch_folder" in emu:
        spec = emu["fetch_folder"]
        target = os.path.join(updir, emu["filename"])   # .../uploads/hvca-bin
        os.makedirs(os.path.join(target, spec["subdir"]), exist_ok=True)
        total = 0
        for rel in spec["files"]:
            dest = os.path.join(target, rel)
            download(f"{spec['raw']}/{rel}", dest)
            total += os.path.getsize(dest)
        for name in spec.get("subdir_files", []):
            dest = os.path.join(target, spec["subdir"], name)
            download(f"{spec['raw']}/{spec['subdir']}/{name}", dest)
            total += os.path.getsize(dest)
        return {"name": emu["filename"], "size": total}

    dest = os.path.join(updir, emu["filename"])
    download(emu["fetch"], dest)
    return {"name": emu["filename"], "size": os.path.getsize(dest)}


# --------------------------------------------------------------------------
# HTTP server
# --------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "GBACompilationBuilder/1.0"

    # ---- plumbing ---------------------------------------------------------

    def send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_bytes(self, data, ctype="application/octet-stream", filename=None):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        if filename:
            self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(data)

    def read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_UPLOAD_BYTES:
            raise ApiError("That file is too large.", 413)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            raise ApiError("Invalid request data.")

    def log_message(self, fmt, *args):  # quieter console
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    # ---- routes -----------------------------------------------------------

    def do_GET(self):
        try:
            parsed = urlparse(self.path)
            path = parsed.path

            if path in ("/", "/index.html"):
                with open(INDEX_FILE, "rb") as fh:
                    self.send_bytes(fh.read(), "text/html; charset=utf-8")
                return

            if path == "/api/builders":
                self.send_json(BUILDERS)
                return

            m = re.fullmatch(r"/api/jobs/([A-Za-z0-9_-]+)/out/(.+)", path)
            if m:
                job, name = m.group(1), m.group(2)
                if not valid_job_id(job):
                    raise ApiError("Bad job id.", 404)
                safe = sanitize_filename(name)
                path_on_disk = os.path.join(out_dir(job), safe)
                if not os.path.isfile(path_on_disk):
                    raise ApiError("File not found.", 404)
                with open(path_on_disk, "rb") as fh:
                    data = fh.read()
                self.send_bytes(data, "application/octet-stream", safe)
                return

            raise ApiError("Not found.", 404)
        except ApiError as e:
            self.send_json({"ok": False, "error": str(e)}, e.status)
        except BrokenPipeError:
            pass
        except Exception as e:  # pragma: no cover
            self.send_json({"ok": False, "error": f"Server error: {e}"}, 500)

    def do_POST(self):
        try:
            parsed = urlparse(self.path)
            path = parsed.path
            qs = parse_qs(parsed.query)

            if path == "/api/upload":
                job = (qs.get("job") or [""])[0]
                name = (qs.get("name") or [""])[0]
                if not valid_job_id(job):
                    raise ApiError("Bad job id.")
                if not name:
                    raise ApiError("Missing file name.")
                safe = sanitize_filename(name)
                updir = uploads_dir(job)
                os.makedirs(updir, exist_ok=True)
                dest = os.path.join(updir, safe)
                remaining = int(self.headers.get("Content-Length") or 0)
                if remaining > MAX_UPLOAD_BYTES:
                    raise ApiError("That file is too large.", 413)
                with open(dest, "wb") as fh:
                    while remaining > 0:
                        chunk = self.rfile.read(min(1024 * 1024, remaining))
                        if not chunk:
                            break
                        fh.write(chunk)
                        remaining -= len(chunk)
                self.send_json({"ok": True, "name": safe,
                                "renamed": safe != os.path.basename(name.replace("\\", "/")),
                                "size": os.path.getsize(dest)})
                return

            if path == "/api/fetch-emu":
                data = self.read_json()
                job = data.get("job") or ""
                builder = BUILDERS_BY_ID.get(data.get("builder") or "")
                if not valid_job_id(job):
                    raise ApiError("Bad job id.")
                if not builder:
                    raise ApiError("Unknown builder.")
                result = fetch_emu(builder, job)
                result["ok"] = True
                self.send_json(result)
                return

            if path == "/api/build":
                data = self.read_json()
                job = data.get("job") or ""
                builder = BUILDERS_BY_ID.get(data.get("builder") or "")
                if not valid_job_id(job):
                    raise ApiError("Bad job id.")
                if not builder:
                    raise ApiError("Unknown builder.")
                # resolve automatic emulator download before running
                files = data.setdefault("files", {})
                if files.get("emu") == "__AUTO__":
                    files["emu"] = fetch_emu(builder, job)["name"]
                result = builder_core.run_build(builder, job, data)
                self.send_json(result)
                return

            raise ApiError("Not found.", 404)
        except ApiError as e:
            self.send_json({"ok": False, "error": str(e)}, e.status)
        except BrokenPipeError:
            pass
        except Exception as e:  # pragma: no cover
            self.send_json({"ok": False, "error": f"Server error: {e}"}, 500)


def main():
    if not os.path.isdir(builder_core.REPO_DIR):
        print(f"ERROR: the builder scripts were not found at:\n  {builder_core.REPO_DIR}\n"
              "Clone them next to this folder:\n"
              "  git clone https://github.com/patters-match/gba-emu-compilation-builders")
        sys.exit(1)
    os.makedirs(JOBS_DIR, exist_ok=True)
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print("=" * 64)
    print("  GBA Emulator Compilation Builder")
    print(f"  Open this page in your browser:  http://localhost:{PORT}")
    print("  (You can also just double-click index.html — it works standalone too.)")
    print("  Press Ctrl+C to stop the server.")
    print("=" * 64)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nBye!")


if __name__ == "__main__":
    main()
