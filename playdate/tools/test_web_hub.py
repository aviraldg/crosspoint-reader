#!/usr/bin/env python3
"""
Automated end-to-end test suite for `playdate_web_hub.html`.

Tests in headless Chrome across both `file://` and `http://127.0.0.1` (`http.server`):
  1. Unconnected startup: verifies zero 404s (no unsolicited `/api/status` calls),
     zero console errors, and clean onboarding empty state.
  2. Live USB Serial (`navigator.serial` -> `WebSerialBackend`): simulates the
     exact `!CP` / `@CP` wire protocol of `PlaydateSerialBridge.cpp` with
     multi-line `@CP ITEM` and `@CP DATA` bursts delivered in single USB read
     chunks, verifying plugin discovery, plugin UI rendering, file read/write,
     settings persistence, and `!CP RELAY` CORS fallback.
  3. USB Data Disk (`window.showDirectoryPicker` -> `FileSystemAccessBackend`):
     simulates a mounted `PLAYDATE/Data/user.12345.com.crosspoint.reader`
     hierarchy, verifying `resolveCrossPointDataHandle`, plugin rendering,
     file management, and settings updates.
"""

import base64
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", ".."))
HUB_HTML_PATH = os.path.join(TOOLS_DIR, "playdate_web_hub.html")


def build_seed_vfs() -> dict:
    """Builds a seed virtual filesystem for testing Web Serial and Data Disk backends."""
    sim_plugins = os.path.join(
        REPO_ROOT, "playdate", "SimulatorDisk", "Data", "com.crosspoint.reader", ".crosspoint", "plugins"
    )
    vfs = {}
    if os.path.isdir(sim_plugins):
        for root, _, files in os.walk(sim_plugins):
            for fn in files:
                full = os.path.join(root, fn)
                rel = "/.crosspoint/plugins/" + os.path.relpath(full, sim_plugins).replace("\\", "/")
                with open(full, "rb") as fp:
                    vfs[rel] = base64.b64encode(fp.read()).decode("ascii")

    # Always ensure at least two synthetic test plugins if SimulatorDisk isn't populated
    if "/.crosspoint/plugins/pluginhub/plugin.js" not in vfs:
        vfs["/.crosspoint/plugins/pluginhub/manifest.json"] = base64.b64encode(
            json.dumps({"name": "pluginhub", "title": "Plugin Hub", "version": "0.1.16", "mount": "settings"}).encode()
        ).decode("ascii")
        vfs["/.crosspoint/plugins/pluginhub/plugin.js"] = base64.b64encode(
            b"CrossPoint.registerPlugin(function(el, api){ el.innerHTML = '<h2>Plugin Hub</h2><p>Loaded</p>'; });"
        ).decode("ascii")

    if "/.crosspoint/plugins/sample/plugin.js" not in vfs and "/.crosspoint/plugins/readest/plugin.js" not in vfs:
        vfs["/.crosspoint/plugins/sample/manifest.json"] = base64.b64encode(
            json.dumps({"name": "sample", "title": "Sample Plugin", "version": "1.0.0", "mount": "settings"}).encode()
        ).decode("ascii")
        vfs["/.crosspoint/plugins/sample/plugin.js"] = base64.b64encode(
            b"CrossPoint.registerPlugin(function(el, api){ el.innerHTML = '<h2>Sample Plugin</h2>'; });"
        ).decode("ascii")

    vfs["/.crosspoint/settings.json"] = base64.b64encode(
        json.dumps({"pluginsEnabled": 1, "extraParagraphSpacing": 1}).encode()
    ).decode("ascii")
    vfs["/Books/TestBook.epub"] = base64.b64encode(b"PK\x03\x04mock-epub-bytes").decode("ascii")
    return vfs


HARNESS_JS = r"""
<script>
window.__SEED_VFS_B64 = __SEED_VFS_JSON__;
window.__TEST_MODE = "__TEST_MODE__";

// Stub external GitHub catalog requests in headless browser test environment
// so pluginhub's auto-load doesn't hit external network timeouts in offline CI.
window.__outboundFetch = async function(input, init) {
  const u = typeof input === "string" ? input : (input && (input.url || input.href)) || "";
  if (u.startsWith("https://raw.githubusercontent.com/")) {
    return new Response(JSON.stringify({ lists: [], plugins: [] }), {
      status: 200,
      headers: { "Content-Type": "application/json" }
    });
  }
  throw new TypeError("Failed to fetch");
};

function createInMemoryFsMap() {
  const files = new Map();
  for (const [k, b64] of Object.entries(window.__SEED_VFS_B64)) {
    files.set(normalizeVPath(k), base64ToUint8(b64));
  }
  return files;
}

// 1. Mock `navigator.serial` that speaks the exact `!CP` / `@CP` line protocol
//    and deliberately bursts multi-line responses in a single Uint8Array chunk!
function installMockWebSerial(files) {
  let rxController = null;
  let writeTarget = "";
  let writeChunks = [];

  const sendBurst = (lines) => {
    // Concatenate all lines into a SINGLE Uint8Array chunk to test multi-line buffering!
    const text = lines.join("\r\n") + "\r\n";
    rxController.enqueue(new TextEncoder().encode(text));
  };

  const handleCpCommand = (rawLine) => {
    const m = rawLine.match(/msg\s+!CP\s+(.*)/);
    if (!m) return;
    const cpLine = m[1].trim();
    const sp = cpLine.indexOf(" ");
    const cmd = sp === -1 ? cpLine : cpLine.slice(0, sp);
    const rest = sp === -1 ? "" : cpLine.slice(sp + 1);

    if (cmd === "PING") {
      sendBurst([`@CP OK ${JSON.stringify({ ok: true, device: "Playdate", version: "1.2.0-test", pluginsEnabled: 1 })}`]);
      return;
    }
    if (cmd === "STAT") {
      const p = normalizeVPath(rest);
      if (files.has(p)) {
        sendBurst([`@CP OK ${JSON.stringify({ ok: true, exists: true, isDir: false, size: files.get(p).length })}`]);
        return;
      }
      const prefix = p === "/" ? "/" : p + "/";
      let isDir = (p === "/");
      for (const k of files.keys()) {
        if (k.startsWith(prefix)) { isDir = true; break; }
      }
      sendBurst([`@CP OK ${JSON.stringify({ ok: true, exists: isDir, isDir, size: 0 })}`]);
      return;
    }
    if (cmd === "LIST") {
      const p = normalizeVPath(rest);
      const prefix = p === "/" ? "/" : p + "/";
      const seen = new Map();
      for (const [k, bytes] of files.entries()) {
        if (!k.startsWith(prefix)) continue;
        const sub = k.slice(prefix.length);
        if (!sub) continue;
        const slash = sub.indexOf("/");
        if (slash === -1) {
          seen.set(sub, { name: sub, isDir: false, size: bytes.length });
        } else {
          const d = sub.slice(0, slash);
          if (!seen.has(d)) seen.set(d, { name: d, isDir: true, size: 0 });
        }
      }
      if (seen.size === 0 && p !== "/") {
        sendBurst([`@CP ERR ${JSON.stringify({ ok: false, error: "not a directory" })}`]);
        return;
      }
      const burst = [];
      for (const item of seen.values()) {
        burst.push(`@CP ITEM ${JSON.stringify(item)}`);
      }
      burst.push(`@CP OK ${JSON.stringify({ ok: true, count: seen.size })}`);
      sendBurst(burst);
      return;
    }
    if (cmd === "READ") {
      const p1 = rest.indexOf(" ");
      const p2 = rest.indexOf(" ", p1 + 1);
      const offset = parseInt(rest.slice(0, p1), 10);
      const maxLen = parseInt(rest.slice(p1 + 1, p2), 10);
      const path = normalizeVPath(rest.slice(p2 + 1));
      if (!files.has(path)) {
        sendBurst([`@CP ERR ${JSON.stringify({ ok: false, error: "not found" })}`]);
        return;
      }
      const full = files.get(path);
      const slice = full.subarray(offset, offset + maxLen);
      const burst = [];
      for (let i = 0; i < slice.length; i += 180) {
        burst.push(`@CP DATA ${uint8ToBase64(slice.subarray(i, i + 180))}`);
      }
      burst.push(`@CP OK ${JSON.stringify({ ok: true, bytes: slice.length, size: full.length, eof: offset + slice.length >= full.length })}`);
      sendBurst(burst);
      return;
    }
    if (cmd === "WOPEN") {
      writeTarget = normalizeVPath(rest);
      writeChunks = [];
      sendBurst([`@CP OK {"ok":true}`]);
      return;
    }
    if (cmd === "WDATA") {
      writeChunks.push(base64ToUint8(rest));
      return;
    }
    if (cmd === "WFLUSH") {
      const len = writeChunks.reduce((a, c) => a + c.length, 0);
      sendBurst([`@CP OK ${JSON.stringify({ ok: true, bytes: len })}`]);
      return;
    }
    if (cmd === "WCLOSE") {
      const len = writeChunks.reduce((a, c) => a + c.length, 0);
      const merged = new Uint8Array(len);
      let pos = 0;
      for (const c of writeChunks) { merged.set(c, pos); pos += c.length; }
      files.set(writeTarget, merged);
      writeTarget = "";
      writeChunks = [];
      sendBurst([`@CP OK ${JSON.stringify({ ok: true, bytes: len })}`]);
      return;
    }
    if (cmd === "MKDIR") {
      sendBurst([`@CP OK {"ok":true}`]);
      return;
    }
    if (cmd === "RM") {
      const target = normalizeVPath(rest);
      files.delete(target);
      const prefix = target + "/";
      for (const k of Array.from(files.keys())) {
        if (k.startsWith(prefix)) files.delete(k);
      }
      sendBurst([`@CP OK {"ok":true}`]);
      return;
    }
    if (cmd === "RENAME") {
      const [o, n] = rest.split("\t").map(normalizeVPath);
      if (files.has(o)) {
        files.set(n, files.get(o));
        files.delete(o);
      }
      sendBurst([`@CP OK {"ok":true}`]);
      return;
    }
    if (cmd === "RELAY") {
      const reqPath = normalizeVPath(rest);
      const raw = files.get(reqPath);
      files.delete(reqPath);
      const req = JSON.parse(new TextDecoder().decode(raw));
      const respBytes = new TextEncoder().encode(JSON.stringify({ relayedUrl: req.url, method: req.method, ok: true }));
      if (req.dest) {
        files.set(normalizeVPath(req.dest), respBytes);
        sendBurst([`@CP OK ${JSON.stringify({ ok: true, status: 200, bytes: respBytes.length })}`]);
      } else {
        sendBurst([
          `@CP DATA ${uint8ToBase64(respBytes)}`,
          `@CP OK ${JSON.stringify({ ok: true, status: 201, bytes: respBytes.length })}`
        ]);
      }
      return;
    }
  };

  let txBuf = "";
  const mockPort = {
    async open() {},
    async setSignals() {},
    async close() {},
    readable: new ReadableStream({
      start(controller) { rxController = controller; }
    }),
    writable: new WritableStream({
      write(chunk) {
        txBuf += new TextDecoder().decode(chunk);
        let nl;
        while ((nl = txBuf.indexOf("\n")) !== -1) {
          const line = txBuf.slice(0, nl).replace(/\r$/, "");
          txBuf = txBuf.slice(nl + 1);
          handleCpCommand(line);
        }
      }
    })
  };

  Object.defineProperty(navigator, "serial", {
    configurable: true,
    value: {
      async requestPort() { return mockPort; }
    }
  });
}

// 2. Mock `window.showDirectoryPicker` simulating `PLAYDATE/Data/user.12345.com.crosspoint.reader`
function installMockDirectoryPicker(files) {
  function makeDirHandle(dirName, vprefix) {
    return {
      kind: "directory",
      name: dirName,
      async *entries() {
        const prefix = vprefix === "/" ? "/" : vprefix + "/";
        const seen = new Map();
        for (const [k, bytes] of files.entries()) {
          if (!k.startsWith(prefix)) continue;
          const rest = k.slice(prefix.length);
          if (!rest) continue;
          const slash = rest.indexOf("/");
          if (slash === -1) {
            seen.set(rest, makeFileHandle(rest, k));
          } else {
            const sub = rest.slice(0, slash);
            if (!seen.has(sub)) seen.set(sub, makeDirHandle(sub, normalizeVPath(prefix + sub)));
          }
        }
        for (const entry of seen.entries()) yield entry;
      },
      async getDirectoryHandle(name, opts = {}) {
        const target = normalizeVPath((vprefix === "/" ? "/" : vprefix + "/") + name);
        const prefix = target + "/";
        let exists = false;
        for (const k of files.keys()) {
          if (k.startsWith(prefix)) { exists = true; break; }
        }
        if (!exists && !opts.create) throw new Error("NotFoundError: dir " + target);
        return makeDirHandle(name, target);
      },
      async getFileHandle(name, opts = {}) {
        const target = normalizeVPath((vprefix === "/" ? "/" : vprefix + "/") + name);
        if (!files.has(target)) {
          if (!opts.create) throw new Error("NotFoundError: file " + target);
          files.set(target, new Uint8Array(0));
        }
        return makeFileHandle(name, target);
      },
      async removeEntry(name, opts = {}) {
        const target = normalizeVPath((vprefix === "/" ? "/" : vprefix + "/") + name);
        files.delete(target);
        if (opts.recursive) {
          const prefix = target + "/";
          for (const k of Array.from(files.keys())) {
            if (k.startsWith(prefix)) files.delete(k);
          }
        }
      }
    };
  }

  function makeFileHandle(fileName, fullVPath) {
    return {
      kind: "file",
      name: fileName,
      async getFile() {
        const bytes = files.get(fullVPath) || new Uint8Array(0);
        return {
          size: bytes.byteLength,
          async arrayBuffer() {
            return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
          }
        };
      },
      async createWritable() {
        let buf = new Uint8Array(0);
        return {
          async write(data) {
            buf = data instanceof Uint8Array ? data : new Uint8Array(data);
          },
          async close() {
            files.set(fullVPath, buf);
          }
        };
      }
    };
  }

  const appDirHandle = makeDirHandle("user.12345.com.crosspoint.reader", "/");
  const dataDirHandle = {
    kind: "directory",
    name: "Data",
    async *entries() {
      yield ["user.12345.com.crosspoint.reader", appDirHandle];
    }
  };
  const playdateVolumeHandle = {
    kind: "directory",
    name: "PLAYDATE",
    async *entries() {
      yield ["Data", dataDirHandle];
    }
  };

  window.showDirectoryPicker = async () => playdateVolumeHandle;
}

window.addEventListener("load", async () => {
  const resultEl = document.createElement("pre");
  resultEl.id = "test-suite-result";
  document.body.appendChild(resultEl);

  const report = {
    mode: window.__TEST_MODE,
    protocol: window.location.protocol,
    passed: false,
    checks: {},
    errors: []
  };

  try {
    if (window.__TEST_MODE === "unconnected") {
      const emptyMsg = document.getElementById("empty-state-msg").textContent.trim();
      report.checks.emptyPromptClean = emptyMsg.includes("Connect your Playdate above");
      report.checks.noCouldNotLoadError = !emptyMsg.includes("Could not load plugins");
      report.checks.zeroHubErrors = window.__hubLogs.filter(e => e.level === "error").length === 0;
      report.passed = Object.values(report.checks).every(Boolean);
    } else if (window.__TEST_MODE === "webserial" || window.__TEST_MODE === "dirpicker") {
      const files = createInMemoryFsMap();
      if (window.__TEST_MODE === "webserial") {
        installMockWebSerial(files);
        await connectWebSerial(false);
      } else {
        installMockDirectoryPicker(files);
        await connectDirectoryPicker();
      }

      // Wait briefly for async plugin script blobs to finish rendering
      await new Promise(r => setTimeout(r, 400));

      const pluginCards = Array.from(document.querySelectorAll("#plugin-container .card")).map(c => c.id);
      report.checks.connectedDot = document.getElementById("pdDot").classList.contains("connected");
      report.checks.pluginsRendered = pluginCards.length >= 2;
      report.checks.pluginCards = pluginCards;

      // Test /api/status with ?plugin=readest
      const stRes = await (await fetch("/api/status?plugin=readest")).json();
      report.checks.statusDeviceId = typeof stRes.deviceId === "string" && stRes.deviceId.length === 64;

      // Test /api/settings POST & GET round-trip
      await fetch("/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ koServerUrl: "https://web.readest.com/api/crosspoint", koUsername: "user@example.com", koPassword: "secret-key-123" })
      });
      const settingsArr = await (await fetch("/api/settings")).json();
      const koUrl = settingsArr.find(s => s.key === "koServerUrl");
      const koPw = settingsArr.find(s => s.key === "koPassword");
      report.checks.settingsSaved = koUrl && koUrl.value === "https://web.readest.com/api/crosspoint" && koPw && koPw.hasPassword === true;

      // Test File Manager tab + upload/download
      await switchTab("files");
      const filesList = await (await fetch("/api/files?path=/Books")).json();
      report.checks.filesListed = Array.isArray(filesList) && filesList.some(f => f.name === "TestBook.epub");

      await CrossPoint.writeFile("pluginhub", "/.crosspoint/plugins/pluginhub/state.json", btoa('{"hello":"world"}'));
      const stateDoc = JSON.parse(new TextDecoder().decode(files.get("/.crosspoint/plugins/pluginhub/state.json")));
      report.checks.pluginWriteFile = stateDoc.hello === "world";

      if (window.__TEST_MODE === "webserial") {
        // Test Web Serial HTTP Relay fallback (`!CP RELAY`) when direct browser fetch fails
        const relayResp = await CrossPoint.relay("readest", "POST", "https://nonexistent.invalid/api/device/code", {}, '{"test":1}');
        const relayJson = JSON.parse(relayResp.body);
        report.checks.serialHttpRelay = relayResp.status === 201 && relayJson.relayedUrl === "https://nonexistent.invalid/api/device/code";
      }

      const hubErrors = window.__hubLogs.filter(e => e.level === "error");
      report.checks.zeroHubErrors = hubErrors.length === 0;
      if (hubErrors.length > 0) report.errors = hubErrors;
      report.passed = Object.entries(report.checks)
        .filter(([k]) => k !== "pluginCards")
        .every(([, v]) => v === true);
    }
  } catch (err) {
    report.errors.push(err.stack || err.message || String(err));
    report.passed = false;
  }

  resultEl.textContent = "TEST_REPORT_JSON:" + JSON.stringify(report);
});
</script>
"""


class QuietRecordingHTTPHandler(http.server.SimpleHTTPRequestHandler):
    requests_log = []

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        self.__class__.requests_log.append(("GET", self.path))
        super().do_GET()

    def do_POST(self):
        self.__class__.requests_log.append(("POST", self.path))
        self.send_error(404, "Static server does not support POST")


def run_chrome_test(url: str) -> dict:
    res = subprocess.run(
        [
            "google-chrome",
            "--headless",
            "--disable-gpu",
            "--no-sandbox",
            "--no-proxy-server",
            "--allow-file-access-from-files",
            "--virtual-time-budget=6000",
            "--dump-dom",
            url,
        ],
        capture_output=True,
        text=True,
        timeout=20,
    )
    m = re.search(r"TEST_REPORT_JSON:(\{.*?\})</pre>", res.stdout, re.S)
    if not m:
        raise RuntimeError(f"Did not find TEST_REPORT_JSON in DOM for {url}.\nStderr:\n{res.stderr[:1000]}")
    return json.loads(m.group(1))


def main():
    # Regenerate playdate_web_hub.html first
    subprocess.run([sys.executable, os.path.join(TOOLS_DIR, "build_web_hub.py")], check=True)

    with open(HUB_HTML_PATH, "r", encoding="utf-8") as fp:
        base_html = fp.read()

    seed_vfs = build_seed_vfs()
    tmpdir = tempfile.mkdtemp(prefix="cp_web_hub_test_")

    try:
        modes = ["unconnected", "webserial", "dirpicker"]
        for mode in modes:
            injected = (
                HARNESS_JS.replace("__SEED_VFS_JSON__", json.dumps(seed_vfs))
                .replace("__TEST_MODE__", mode)
            )
            out_file = os.path.join(tmpdir, f"hub_{mode}.html")
            with open(out_file, "w", encoding="utf-8") as fp:
                fp.write(base_html.replace("</body>", injected + "\n</body>"))

        handler_cls = functools.partial(QuietRecordingHTTPHandler, directory=tmpdir)
        httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler_cls)
        port = httpd.server_address[1]
        server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        server_thread.start()

        all_passed = True
        for scheme in ["file", "http"]:
            for mode in modes:
                QuietRecordingHTTPHandler.requests_log.clear()
                if scheme == "file":
                    url = f"file://{os.path.join(tmpdir, f'hub_{mode}.html')}"
                else:
                    url = f"http://127.0.0.1:{port}/hub_{mode}.html"

                report = run_chrome_test(url)
                unexpected_requests = [
                    (m, p)
                    for (m, p) in QuietRecordingHTTPHandler.requests_log
                    if not p.startswith(f"/hub_{mode}.html") and p != "/favicon.ico"
                ]
                if unexpected_requests:
                    report["passed"] = False
                    report["unexpectedHttpRequests"] = unexpected_requests

                status = "PASS" if report.get("passed") else "FAIL"
                print(f"[{status}] scheme={scheme:4s} mode={mode:11s} checks={json.dumps(report.get('checks', {}))}")
                if not report.get("passed"):
                    all_passed = False
                    print("  Errors:", report.get("errors"), report.get("unexpectedHttpRequests"))

        httpd.shutdown()
        if not all_passed:
            sys.exit(1)
        print("\nAll 6 Web Hub end-to-end browser test suites (file:// & http://) PASSED!")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
