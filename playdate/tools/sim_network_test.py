#!/usr/bin/env python3
"""Automated PlaydateSimulator network + memory telemetry stress test.

Starts a local multi-threaded HTTP test server exercising:
  1. Chunked Transfer-Encoding XML feeds (no Content-Length) via both
     SecureHttpClient and HttpDownloader::fetchUrl
  2. Fixed Content-Length binary downloads (128 KB) verified by SHA-256
  3. Multi-hop 302 redirects
  4. Mid-body TCP disconnect + HTTP 206 Range resume via
     HttpDownloader::downloadToFile (ResumableFetch)
  5. HTTP 500 server error + slow-header timeout handling
  6. 20-request sequential burst + post-network idle reap window

Asserts zero crashes, zero heap allocation failures or bad frees, intact fiber
stack canary, and 100% HTTP connection close + reap completion.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import pty
import shutil
import signal
import socketserver
import subprocess
import sys
import threading
import time
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PDX = REPO_ROOT / "playdate" / "CrossPoint.pdx"
BUNDLE_ID = "com.crosspoint.reader"


def make_deterministic_bytes(size: int, seed: bytes) -> bytes:
    out = bytearray()
    counter = 0
    while len(out) < size:
        digest = hashlib.sha256(seed + counter.to_bytes(4, "little")).digest()
        out.extend(digest)
        counter += 1
    return bytes(out[:size])


FIXED_PAYLOAD = make_deterministic_bytes(128 * 1024, b"crosspoint-fixed-128k")
FIXED_SHA256 = hashlib.sha256(FIXED_PAYLOAD).hexdigest()

RESUME_PAYLOAD = make_deterministic_bytes(96 * 1024, b"crosspoint-resume-96k")
RESUME_CUT_OFFSET = 40 * 1024
RESUME_SHA256 = hashlib.sha256(RESUME_PAYLOAD).hexdigest()

CHUNKED_CHUNKS = [
    b'<?xml version="1.0" encoding="utf-8"?>\n<feed xmlns="http://www.w3.org/2005/Atom">\n',
    b"  <title>CrossPoint Simulator Stress Catalog</title>\n",
    b'  <entry><title>Book One</title><id>urn:cp:1</id></entry>\n' * 16,
    b"</feed>\n",
]
CHUNKED_BODY = b"".join(CHUNKED_CHUNKS)


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class StressServerState:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.flaky_attempts = 0
        self.range_headers_seen: list[str] = []
        self.requests_log: list[tuple[str, str]] = []


def make_handler(state: StressServerState):
    class StressRequestHandler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt: str, *args: Any) -> None:
            return

        def do_GET(self) -> None:
            with state.lock:
                state.requests_log.append((self.command, self.path))

            if self.path == "/opds/chunked.xml":
                self.send_response(200)
                self.send_header("Content-Type", "application/atom+xml; charset=utf-8")
                self.send_header("Transfer-Encoding", "chunked")
                self.send_header("Connection", "close")
                self.end_headers()
                for part in CHUNKED_CHUNKS:
                    header = f"{len(part):X}\r\n".encode("ascii")
                    self.wfile.write(header + part + b"\r\n")
                    self.wfile.flush()
                    time.sleep(0.015)
                self.wfile.write(b"0\r\n\r\n")
                self.wfile.flush()
                self.close_connection = True
                return

            if self.path == "/books/fixed.bin":
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(FIXED_PAYLOAD)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(FIXED_PAYLOAD)
                self.wfile.flush()
                self.close_connection = True
                return

            if self.path == "/redirect/hop1":
                self.send_response(302)
                self.send_header("Location", "/redirect/hop2")
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                return

            if self.path == "/redirect/hop2":
                self.send_response(302)
                self.send_header("Location", "/opds/chunked.xml")
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                return

            if self.path == "/flaky/resume.epub":
                range_hdr = self.headers.get("Range")
                with state.lock:
                    state.flaky_attempts += 1
                    attempt = state.flaky_attempts
                    if range_hdr:
                        state.range_headers_seen.append(range_hdr)

                if attempt == 1 and not range_hdr:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/epub+zip")
                    self.send_header("Accept-Ranges", "bytes")
                    self.send_header("Content-Length", str(len(RESUME_PAYLOAD)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    # Cut the response short mid-body so ResumableFetch retries with Range.
                    self.wfile.write(RESUME_PAYLOAD[:RESUME_CUT_OFFSET])
                    self.wfile.flush()
                    self.close_connection = True
                    return

                if range_hdr and range_hdr.startswith("bytes="):
                    spec = range_hdr[len("bytes=") :].strip()
                    start_s, end_s = spec.split("-", 1)
                    start = int(start_s)
                    end = int(end_s) if end_s else (len(RESUME_PAYLOAD) - 1)
                    end = min(end, len(RESUME_PAYLOAD) - 1)
                    body_slice = RESUME_PAYLOAD[start : end + 1]
                    self.send_response(206)
                    self.send_header("Content-Type", "application/epub+zip")
                    self.send_header("Accept-Ranges", "bytes")
                    self.send_header(
                        "Content-Range", f"bytes {start}-{end}/{len(RESUME_PAYLOAD)}"
                    )
                    self.send_header("Content-Length", str(len(body_slice)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(body_slice)
                    self.wfile.flush()
                    self.close_connection = True
                    return

                self.send_response(200)
                self.send_header("Content-Type", "application/epub+zip")
                self.send_header("Content-Length", str(len(RESUME_PAYLOAD)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(RESUME_PAYLOAD)
                self.wfile.flush()
                self.close_connection = True
                return

            if self.path == "/error/500":
                err_body = b"simulated upstream error\n"
                self.send_response(500)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(err_body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(err_body)
                self.wfile.flush()
                self.close_connection = True
                return

            if self.path == "/slow/timeout":
                time.sleep(2.4)
                try:
                    self.send_response(200)
                    self.send_header("Content-Length", "2")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(b"ok")
                    self.wfile.flush()
                except OSError:
                    pass
                self.close_connection = True
                return

            if self.path.startswith("/burst/"):
                idx = int(self.path.rsplit("/", 1)[-1])
                payload = f"burst-response-{idx:02d}-".encode("ascii") * 64
                if idx % 2 == 0:
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Transfer-Encoding", "chunked")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    mid = len(payload) // 2
                    for chunk in (payload[:mid], payload[mid:]):
                        self.wfile.write(f"{len(chunk):X}\r\n".encode("ascii") + chunk + b"\r\n")
                    self.wfile.write(b"0\r\n\r\n")
                    self.wfile.flush()
                else:
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Content-Length", str(len(payload)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(payload)
                    self.wfile.flush()
                self.close_connection = True
                return

            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True

    return StressRequestHandler


class SimulatorHarness:
    def __init__(self, sdk_path: Path, pdx_path: Path, server_port: int) -> None:
        self.sdk_path = sdk_path
        self.pdx_path = pdx_path
        self.server_port = server_port
        self.data_dir = sdk_path / "Disk" / "Data" / BUNDLE_ID
        self.crosspoint_dir = self.data_dir / ".crosspoint"
        self.permissions_file = self.data_dir / "Permissions"
        self._orig_permissions: bytes | None = None
        self.proc: subprocess.Popen[bytes] | None = None
        self._pty_master_fd: int | None = None
        self.lines: list[str] = []
        self._lines_cond = threading.Condition()
        self._reader_thread: threading.Thread | None = None

    def _prepare_permissions(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.crosspoint_dir.mkdir(parents=True, exist_ok=True)
        if self.permissions_file.exists():
            self._orig_permissions = self.permissions_file.read_bytes()
        perm_text = f"net+\nnet+127.0.0.1:{self.server_port}\n"
        self.permissions_file.write_text(perm_text, encoding="utf-8")

        cmd_file = self.crosspoint_dir / ".sim-cmd.txt"
        if cmd_file.exists():
            cmd_file.unlink()

    def _restore_permissions_and_temp_files(self) -> None:
        for rel in (
            ".crosspoint/.sim-cmd.txt",
            ".crosspoint/.sim-cmd.txt.tmp",
            ".crosspoint/.sim-relay.json",
            "Books/.sim_fixed.bin",
            "Books/.sim_resume.epub",
        ):
            p = self.data_dir / rel
            if p.exists():
                p.unlink()
        if self._orig_permissions is None:
            if self.permissions_file.exists():
                self.permissions_file.unlink()
        else:
            self.permissions_file.write_bytes(self._orig_permissions)

    def _build_simulator_command(self) -> list[str]:
        sim_bin = self.sdk_path / "bin" / "PlaydateSimulator"
        if not sim_bin.exists():
            raise FileNotFoundError(f"PlaydateSimulator not found at {sim_bin}")

        sysroot_ld = Path("/home/antigravity/sysroot/usr/lib64/ld-linux-x86-64.so.2")
        glib_compat = Path("/home/antigravity/sysroot/usr/lib/x86_64-linux-gnu/libglib_compat.so")
        sysroot_lib = Path("/home/antigravity/sysroot/usr/lib/x86_64-linux-gnu")

        if sysroot_ld.exists() and glib_compat.exists():
            inner = [
                str(sysroot_ld),
                "--preload",
                str(glib_compat),
                "--library-path",
                f"{sysroot_lib}:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
                str(sim_bin),
                str(self.pdx_path),
            ]
        else:
            inner = [str(sim_bin), str(self.pdx_path)]

        if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
            return ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24", *inner]
        return inner

    def _stdout_reader(self) -> None:
        assert self._pty_master_fd is not None
        buf = b""
        while True:
            try:
                chunk = os.read(self._pty_master_fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                raw_line, buf = buf.split(b"\n", 1)
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                with self._lines_cond:
                    self.lines.append(line)
                    self._lines_cond.notify_all()

    def start(self) -> None:
        self._prepare_permissions()
        env = os.environ.copy()
        env["PLAYDATE_SDK_PATH"] = str(self.sdk_path)
        cmd = self._build_simulator_command()
        master_fd, slave_fd = pty.openpty()
        self._pty_master_fd = master_fd
        try:
            self.proc = subprocess.Popen(
                cmd,
                env=env,
                stdout=slave_fd,
                stderr=slave_fd,
                start_new_session=True,
            )
        finally:
            os.close(slave_fd)
        self._reader_thread = threading.Thread(target=self._stdout_reader, daemon=True)
        self._reader_thread.start()

    def stop(self) -> None:
        try:
            if self.proc is not None and self.proc.poll() is None:
                try:
                    os.killpg(self.proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(self.proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    self.proc.wait(timeout=3)
            if self._pty_master_fd is not None:
                try:
                    os.close(self._pty_master_fd)
                except OSError:
                    pass
                self._pty_master_fd = None
        finally:
            self._restore_permissions_and_temp_files()

    def assert_alive(self) -> None:
        if self.proc is None:
            raise RuntimeError("Simulator was not started")
        rc = self.proc.poll()
        if rc is not None:
            tail = "\n".join(self.lines[-40:])
            raise AssertionError(f"PlaydateSimulator exited unexpectedly with rc={rc}:\n{tail}")

    def wait_for_log_substring(self, needle: str, timeout: float = 20.0, start_idx: int = 0) -> int:
        deadline = time.monotonic() + timeout
        with self._lines_cond:
            while True:
                for idx in range(start_idx, len(self.lines)):
                    if needle in self.lines[idx]:
                        return idx
                self.assert_alive()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    tail = "\n".join(self.lines[-40:])
                    raise TimeoutError(f"Timed out waiting for '{needle}' in simulator output:\n{tail}")
                self._lines_cond.wait(timeout=min(remaining, 0.25))

    def send_cp_command(self, command_line: str, timeout: float = 25.0) -> tuple[str, dict[str, Any], bytes]:
        self.assert_alive()
        with self._lines_cond:
            start_idx = len(self.lines)

        tmp_path = self.crosspoint_dir / ".sim-cmd.txt.tmp"
        final_path = self.crosspoint_dir / ".sim-cmd.txt"
        tmp_path.write_text(command_line.strip() + "\n", encoding="utf-8")
        os.replace(tmp_path, final_path)

        data_chunks: list[bytes] = []
        deadline = time.monotonic() + timeout
        cursor = start_idx
        with self._lines_cond:
            while True:
                while cursor < len(self.lines):
                    line = self.lines[cursor]
                    cursor += 1
                    cp_pos = line.find("@CP ")
                    if cp_pos < 0:
                        continue
                    cp_msg = line[cp_pos + 4 :]
                    parts = cp_msg.split(" ", 1)
                    kind = parts[0]
                    payload = parts[1] if len(parts) > 1 else ""
                    if kind == "DATA":
                        data_chunks.append(base64.b64decode(payload.strip()))
                    elif kind in ("OK", "ERR"):
                        parsed = json.loads(payload)
                        return kind, parsed, b"".join(data_chunks)
                self.assert_alive()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    tail = "\n".join(self.lines[max(0, start_idx - 10) :])
                    raise TimeoutError(f"Timed out waiting for @CP response to '{command_line}':\n{tail}")
                self._lines_cond.wait(timeout=min(remaining, 0.25))

    def run_relay(self, spec: dict[str, Any], timeout: float = 30.0) -> tuple[str, dict[str, Any], bytes]:
        relay_rel = "/.crosspoint/.sim-relay.json"
        relay_host = self.data_dir / ".crosspoint" / ".sim-relay.json"
        relay_host.write_text(json.dumps(spec), encoding="utf-8")
        return self.send_cp_command(f"!CP RELAY {relay_rel}", timeout=timeout)

    def diag(self, force_reap: bool = False) -> dict[str, Any]:
        cmd = "!CP DIAG force_reap" if force_reap else "!CP DIAG"
        kind, payload, _ = self.send_cp_command(cmd, timeout=10.0)
        assert kind == "OK" and payload.get("ok") is True, f"DIAG failed: {payload}"
        return payload


def resolve_sdk_path(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    env_sdk = os.environ.get("PLAYDATE_SDK_PATH")
    if env_sdk:
        return Path(env_sdk).resolve()
    default_sdk = Path.home() / "PlaydateSDK"
    if default_sdk.exists():
        return default_sdk.resolve()
    raise RuntimeError("PLAYDATE_SDK_PATH is not set and ~/PlaydateSDK does not exist")


def run_stress_suite(sdk_path: Path, pdx_path: Path) -> int:
    if not pdx_path.exists():
        raise FileNotFoundError(f"PDX bundle not found at {pdx_path}; run ./playdate/build_playdate.sh first")

    server_state = StressServerState()
    httpd = ThreadedHTTPServer(("127.0.0.1", 0), make_handler(server_state))
    port = httpd.server_address[1]
    base_url = f"http://127.0.0.1:{port}"
    server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    server_thread.start()

    harness = SimulatorHarness(sdk_path=sdk_path, pdx_path=pdx_path, server_port=port)
    print(f"[TEST] Local HTTP stress server listening on {base_url}")
    try:
        harness.start()
        harness.wait_for_log_substring("reason=boot_done", timeout=25.0)
        kind, ping, _ = harness.send_cp_command("!CP PING", timeout=10.0)
        assert kind == "OK" and ping.get("device") == "Playdate", f"Unexpected PING: {ping}"
        boot_diag = harness.diag()
        print(
            f"[TEST] Boot OK: heapLive={boot_diag['heapLive']} stackPeak={boot_diag['stackPeakUsed']}/"
            f"{boot_diag['stackSize']} canary={boot_diag['stackCanaryOk']}"
        )
        assert boot_diag["stackCanaryOk"] is True
        assert boot_diag["stackSize"] > 0

        # 1. Chunked Transfer-Encoding XML feed (SecureHttpClient + HttpDownloader::fetchUrl)
        kind, res, data = harness.run_relay({"url": f"{base_url}/opds/chunked.xml"})
        assert kind == "OK" and res["status"] == 200, f"Chunked relay failed: {res}"
        assert data == CHUNKED_BODY, f"Chunked body mismatch ({len(data)} vs {len(CHUNKED_BODY)})"

        kind, res, data = harness.run_relay(
            {"url": f"{base_url}/opds/chunked.xml", "mode": "downloader_url"}
        )
        assert kind == "OK" and res["status"] == 200, f"downloader_url chunked failed: {res}"
        assert data == CHUNKED_BODY, "downloader_url chunked payload mismatch"
        print("[TEST] [1/6] Chunked Transfer-Encoding XML feeds passed")

        # 2. Fixed Content-Length 128 KB binary download
        fixed_dest = "/Books/.sim_fixed.bin"
        kind, res, _ = harness.run_relay({"url": f"{base_url}/books/fixed.bin", "dest": fixed_dest})
        assert kind == "OK" and res["status"] == 200 and res["bytes"] == len(FIXED_PAYLOAD), f"Fixed failed: {res}"
        fixed_disk = harness.data_dir / "Books" / ".sim_fixed.bin"
        assert hashlib.sha256(fixed_disk.read_bytes()).hexdigest() == FIXED_SHA256, "Fixed SHA-256 mismatch"
        print("[TEST] [2/6] Fixed Content-Length 128 KB download + SHA-256 passed")

        # 3. Multi-hop 302 redirect chain -> chunked XML
        kind, res, data = harness.run_relay({"url": f"{base_url}/redirect/hop1"})
        assert kind == "OK" and res["status"] == 200, f"Redirect chain failed: {res}"
        assert data == CHUNKED_BODY, "Redirect final body mismatch"
        print("[TEST] [3/6] Multi-hop 302 redirect chain passed")

        # 4. Mid-stream TCP cut + HTTP 206 Range resume via HttpDownloader::downloadToFile
        resume_dest = "/Books/.sim_resume.epub"
        kind, res, _ = harness.run_relay(
            {"url": f"{base_url}/flaky/resume.epub", "dest": resume_dest, "mode": "downloader_file"},
            timeout=35.0,
        )
        assert kind == "OK" and res["bytes"] == len(RESUME_PAYLOAD), f"Resumable download failed: {res}"
        resume_disk = harness.data_dir / "Books" / ".sim_resume.epub"
        assert hashlib.sha256(resume_disk.read_bytes()).hexdigest() == RESUME_SHA256, "Resume SHA-256 mismatch"
        assert server_state.flaky_attempts >= 2 and server_state.range_headers_seen, (
            f"Expected Range retry, got attempts={server_state.flaky_attempts}, "
            f"ranges={server_state.range_headers_seen}"
        )
        print(
            f"[TEST] [4/6] Interrupted download + 206 Range resume passed "
            f"(attempts={server_state.flaky_attempts}, range={server_state.range_headers_seen[0]})"
        )

        # 5. HTTP 500 error + header timeout handling
        kind, res, _ = harness.run_relay({"url": f"{base_url}/error/500"})
        assert kind == "OK" and res["status"] == 500, f"Expected status 500 response, got: {res}"

        kind, res, _ = harness.run_relay({"url": f"{base_url}/slow/timeout", "timeoutMs": 900}, timeout=15.0)
        assert kind == "ERR", f"Expected timeout ERR, got: {kind} {res}"
        mid_diag = harness.diag()
        assert mid_diag["httpActiveConns"] == 0, f"Leaked active connection after error/timeout: {mid_diag}"
        assert mid_diag["httpFailedReqs"] >= 1, f"Expected httpFailedReqs >= 1 after timeout: {mid_diag}"
        print("[TEST] [5/6] HTTP 500 + header timeout cleanup passed")

        # 6. 20-request sequential burst + post-network idle reap verification
        baseline_after_warmup = harness.diag(force_reap=True)
        for i in range(20):
            kind, res, data = harness.run_relay({"url": f"{base_url}/burst/{i}"}, timeout=20.0)
            expected = f"burst-response-{i:02d}-".encode("ascii") * 64
            assert kind == "OK" and res["status"] == 200 and data == expected, f"Burst #{i} failed: {res}"

        burst_diag = harness.diag()
        print(
            f"[TEST] [6/6] 20-request burst finished: totalReqs={burst_diag['httpTotalReqs']} "
            f"active={burst_diag['httpActiveConns']} retired={burst_diag['httpRetiredConns']} "
            f"reaped={burst_diag['httpReapedConns']}"
        )
        assert burst_diag["httpActiveConns"] == 0

        # Idle without issuing any network traffic so runPeriodicDiagnostics() reaps retired
        # connections in the background (simulating reading after network activity).
        deadline = time.monotonic() + 14.0
        settled_diag = burst_diag
        while time.monotonic() < deadline:
            time.sleep(1.0)
            settled_diag = harness.diag()
            if settled_diag["httpRetiredConns"] == 0:
                break

        print(
            f"[TEST] Post-idle DIAG: totalReqs={settled_diag['httpTotalReqs']} "
            f"active={settled_diag['httpActiveConns']} retired={settled_diag['httpRetiredConns']} "
            f"reaped={settled_diag['httpReapedConns']} heapLive={settled_diag['heapLive']} "
            f"stackPeak={settled_diag['stackPeakUsed']}/{settled_diag['stackSize']} "
            f"canary={settled_diag['stackCanaryOk']}"
        )

        assert settled_diag["httpActiveConns"] == 0, f"Active HTTP connections leaked: {settled_diag}"
        assert settled_diag["httpRetiredConns"] == 0, f"Retired HTTP connections never reaped: {settled_diag}"
        assert settled_diag["httpReapedConns"] == settled_diag["httpTotalReqs"], (
            f"Reaped ({settled_diag['httpReapedConns']}) != Total ({settled_diag['httpTotalReqs']})"
        )
        assert settled_diag["stackCanaryOk"] is True, f"Fiber stack canary corrupted: {settled_diag}"
        assert settled_diag["stackFreeMin"] > 64 * 1024, f"Unexpectedly low stack headroom: {settled_diag}"
        assert settled_diag["allocFailures"] == 0, f"Heap allocation failures recorded: {settled_diag}"
        assert settled_diag["badFrees"] == 0, f"Bad/double frees recorded: {settled_diag}"

        heap_delta = settled_diag["heapLive"] - baseline_after_warmup["heapLive"]
        assert heap_delta < 256 * 1024, (
            f"Excessive post-reap heap growth after 20-request burst: delta={heap_delta} bytes "
            f"(before={baseline_after_warmup['heapLive']}, after={settled_diag['heapLive']})"
        )

        # Verify structured [NET] and [MEM] telemetry lines are present and no fault banners fired.
        net_starts = [ln for ln in harness.lines if "[NET] req #" in ln and " START " in ln]
        net_ends = [ln for ln in harness.lines if "[NET] req #" in ln and " END " in ln]
        net_reaps = [ln for ln in harness.lines if "[NET] req #" in ln and " REAP " in ln]
        fault_lines = [
            ln
            for ln in harness.lines
            if any(
                tok in ln
                for tok in (
                    "FIBER STACK CANARY CORRUPTED",
                    "DOUBLE_FREE",
                    "BAD_FREE",
                    "BAD_REALLOC",
                    "OOM_",
                    "Outside range",
                )
            )
        ]
        assert len(net_starts) == settled_diag["httpTotalReqs"], (
            f"Expected {settled_diag['httpTotalReqs']} [NET] START logs, saw {len(net_starts)}"
        )
        assert len(net_ends) == settled_diag["httpTotalReqs"], (
            f"Expected {settled_diag['httpTotalReqs']} [NET] END logs, saw {len(net_ends)}"
        )
        assert len(net_reaps) == settled_diag["httpReapedConns"], (
            f"Expected {settled_diag['httpReapedConns']} [NET] REAP logs, saw {len(net_reaps)}"
        )
        assert not fault_lines, f"Memory fault logs detected:\n" + "\n".join(fault_lines)

        print(
            f"[PASS] All 6 network + memory stress scenarios passed! "
            f"({settled_diag['httpTotalReqs']} requests, {settled_diag['httpReapedConns']} reaped, "
            f"heapDelta={heap_delta:+d}B, stackPeak={settled_diag['stackPeakUsed']}B)"
        )
        return 0
    except Exception:
        print("\n[FAIL] HTTP server requests_log:", server_state.requests_log, file=sys.stderr)
        print("[FAIL] Last 50 Simulator serial lines:", file=sys.stderr)
        for ln in harness.lines[-50:]:
            print("  ", ln, file=sys.stderr)
        raise
    finally:
        harness.stop()
        httpd.shutdown()
        httpd.server_close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Path to PlaydateSDK (defaults to $PLAYDATE_SDK_PATH or ~/PlaydateSDK)")
    parser.add_argument("--pdx", default=str(DEFAULT_PDX), help="Path to CrossPoint.pdx")
    args = parser.parse_args()
    sdk_path = resolve_sdk_path(args.sdk)
    return run_stress_suite(sdk_path=sdk_path, pdx_path=Path(args.pdx).resolve())


if __name__ == "__main__":
    sys.exit(main())
