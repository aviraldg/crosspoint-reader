#!/usr/bin/env python3
"""
Builds the standalone CrossPoint Playdate Web Hub (`playdate_web_hub.html`),
embedding CrossPoint's official CSS, SVG icons, JSZip, and `plugin-host.js`
together with an in-browser Web Serial (`navigator.serial`) + File System
Access (`window.showDirectoryPicker`) virtual `/api/*` server.
"""

import base64
import os

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HTML_DIR = os.path.join(REPO_ROOT, "src", "network", "html")
OUT_PATH = os.path.join(os.path.dirname(__file__), "playdate_web_hub.html")


def read_text(rel_path: str) -> str:
    with open(os.path.join(HTML_DIR, rel_path), "r", encoding="utf-8") as f:
        return f.read()


def main():
    web_css = read_text("assets/web.css")
    icon_svg = read_text("assets/crosspoint-icon.svg")
    icon_data_uri = "data:image/svg+xml;base64," + base64.b64encode(icon_svg.encode("utf-8")).decode("ascii")
    jszip_js = read_text("js/jszip.min.js").replace("</script>", "<\\/script>")
    plugin_host_js = read_text("js/plugin-host.js").replace("</script>", "<\\/script>")

    # Adapt `plugin-host.js` so `<script src="/plugin?...">` loads via `fetch()` + `Blob` URL
    # when running in standalone Web Serial / File System Access mode!
    old_loader_snippet = """    s.src = '/plugin?name=' + encodeURIComponent(p.name) + '&file=plugin.js';
    document.body.appendChild(s);"""

    new_loader_snippet = """    const pluginScriptUrl = '/plugin?name=' + encodeURIComponent(p.name) + '&file=plugin.js';
    try {
      const resp = await fetch(pluginScriptUrl);
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      const codeText = await resp.text();
      const blob = new Blob([codeText + '\\n//# sourceURL=' + pluginScriptUrl], { type: 'application/javascript' });
      s.src = URL.createObjectURL(blob);
    } catch (err) {
      showFailure();
      continue;
    }
    document.body.appendChild(s);"""

    assert old_loader_snippet in plugin_host_js, "Expected s.src snippet in plugin-host.js"
    plugin_host_js = plugin_host_js.replace(old_loader_snippet, new_loader_snippet, 1)

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>CrossPoint Playdate Web Hub — Plugins, Files, Settings &amp; Fonts</title>
  <link rel="icon" type="image/svg+xml" href="{icon_data_uri}" />
  <style>
{web_css}

    /* Playdate Connection Banner & Hub Styles */
    .pd-conn-bar {{
      background: #1f242b;
      color: #f5f6f8;
      border-bottom: 3px solid #f2c94c;
      padding: 12px 20px;
      font-size: 0.92rem;
    }}
    .pd-conn-inner {{
      max-width: 980px;
      margin: 0 auto;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }}
    .pd-conn-status {{
      display: flex;
      align-items: center;
      gap: 10px;
      flex-wrap: wrap;
    }}
    .pd-dot {{
      width: 11px;
      height: 11px;
      border-radius: 50%;
      background: #95a5a6;
      display: inline-block;
      box-shadow: 0 0 0 3px rgba(149, 165, 166, 0.25);
    }}
    .pd-dot.connected {{
      background: #2ecc71;
      box-shadow: 0 0 0 3px rgba(46, 204, 113, 0.28);
    }}
    .pd-dot.busy {{
      background: #f2c94c;
      box-shadow: 0 0 0 3px rgba(242, 201, 76, 0.3);
    }}
    .pd-conn-actions {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }}
    .pd-btn {{
      background: #f2c94c;
      color: #181a1d;
      border: 1px solid #d4ac2b;
      border-radius: 6px;
      padding: 7px 13px;
      font-weight: 600;
      font-size: 0.88rem;
      cursor: pointer;
      transition: transform 0.08s ease, background 0.15s ease;
    }}
    .pd-btn:hover {{
      background: #ffda63;
    }}
    .pd-btn:disabled {{
      opacity: 0.55;
      cursor: not-allowed;
    }}
    .pd-btn.secondary {{
      background: #2e3540;
      color: #f0f2f5;
      border-color: #454f5e;
    }}
    .pd-btn.secondary:hover {{
      background: #3a4250;
    }}
    .nav-links a.active-tab {{
      font-weight: 700;
      text-decoration: underline;
      text-underline-offset: 5px;
    }}
    .hub-section {{
      display: none;
    }}
    .hub-section.active {{
      display: block;
    }}
    .pd-help-card {{
      border-left: 4px solid #f2c94c;
      background: var(--card-bg, #fff);
    }}
    .pd-quick-row {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      margin-top: 10px;
      align-items: center;
    }}
    .file-table {{
      width: 100%;
      border-collapse: collapse;
    }}
    .file-table th, .file-table td {{
      padding: 10px 8px;
      text-align: left;
      border-bottom: 1px solid var(--border-color, #ddd);
    }}
    .folder-link, .file-link {{
      color: var(--accent-color, #2b579a);
      cursor: pointer;
      text-decoration: none;
      font-weight: 500;
    }}
    .folder-link:hover, .file-link:hover {{
      text-decoration: underline;
    }}
    .item-box {{
      border: 1px solid var(--border-color, #ddd);
      border-radius: 6px;
      padding: 12px;
      margin: 10px 0;
    }}
    .item-actions {{
      display: flex;
      gap: 8px;
      margin-top: 8px;
    }}
  </style>
</head>
<body class="web-ui">
  <!-- Playdate Connection Bar -->
  <div class="pd-conn-bar">
    <div class="pd-conn-inner">
      <div class="pd-conn-status">
        <span id="pdDot" class="pd-dot"></span>
        <div>
          <strong id="pdModeLabel">Not connected to Playdate</strong>
          <div id="pdTargetLabel" style="font-size:0.8rem;color:#b8c0cc;">
            Connect your Playdate over USB (Serial while running CrossPoint, or Data Disk folder)
          </div>
        </div>
      </div>
      <div class="pd-conn-actions">
        <button type="button" class="pd-btn" id="btnConnectSerial" onclick="connectWebSerial(false)">
          &#9889; Connect USB Serial (Live)
        </button>
        <button type="button" class="pd-btn secondary" id="btnOpenFolder" onclick="connectDirectoryPicker()">
          &#128193; Open Playdate Data Folder
        </button>
        <button type="button" class="pd-btn secondary" id="btnRefreshHub" onclick="refreshActiveTab()" title="Reload current tab from Playdate">
          &#8635; Refresh
        </button>
      </div>
    </div>
  </div>

  <!-- Standard CrossPoint Header -->
  <header class="brand">
    <div class="brand-inner">
      <h1>
        <a class="brand-link" href="#plugins" onclick="switchTab('plugins');return false;">
          <img class="brand-logo" src="{icon_data_uri}" alt="" width="32" height="32" />
          CrossPoint <span class="brand-reader">Playdate Hub</span>
        </a>
      </h1>
      <nav class="nav-links desktop-nav">
        <a href="#plugins" data-tab="plugins" class="active-tab" onclick="switchTab('plugins');return false;">Plugins</a>
        <a href="#files" data-tab="files" onclick="switchTab('files');return false;">File Manager</a>
        <a href="#settings" data-tab="settings" onclick="switchTab('settings');return false;">Settings</a>
        <a href="#fonts" data-tab="fonts" onclick="switchTab('fonts');return false;">Fonts</a>
      </nav>
    </div>
  </header>

  <main class="page-content">
    <!-- Connection Onboarding Card -->
    <div class="card pd-help-card" id="onboardingCard">
      <h2 style="margin-top:0;">Playdate Web Plugins &amp; File Hub</h2>
      <p style="margin:6px 0;">
        Manage CrossPoint web plugins, files, settings, and fonts directly in your browser:
      </p>
      <ol style="margin:8px 0 4px 20px;line-height:1.5;">
        <li>
          <b>Live USB Serial Mode (Recommended):</b> Open CrossPoint on your Playdate, plug it into your computer via USB, and click
          <b>&#9889; Connect USB Serial (Live)</b> above. All changes apply immediately without rebooting!
        </li>
        <li>
          <b>USB Data Disk Mode:</b> Or put your Playdate into USB Data Disk mode (<i>Settings &rarr; System &rarr; Reboot to Data Disk</i>), click
          <b>&#128193; Open Playdate Data Folder</b>, and select your mounted <code>PLAYDATE</code> drive (or <code>Data</code> folder).
        </li>
      </ol>
    </div>

    <!-- TAB 1: PLUGINS (/plugins) -->
    <section id="tab-plugins" class="hub-section active">
      <div id="plugin-container"></div>
      <div class="card" id="empty-state" hidden>
        <p class="empty-state" id="empty-state-msg">
          Connect your Playdate above to load installed web plugins.
        </p>
      </div>
    </section>

    <!-- TAB 2: FILE MANAGER (/files) -->
    <section id="tab-files" class="hub-section">
      <div class="card">
        <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;margin-bottom:12px;">
          <div>
            <h2 style="margin:0;display:inline-block;">Files</h2>
            <span id="filesBreadcrumb" style="margin-left:10px;color:var(--label-color);">/</span>
          </div>
          <div style="display:flex;gap:8px;flex-wrap:wrap;">
            <label class="btn-small btn-add" style="cursor:pointer;margin:0;">
              + Upload Files
              <input type="file" id="fileUploadInput" multiple style="display:none" onchange="uploadSelectedFiles(this.files)" />
            </label>
            <button type="button" class="btn-small" onclick="promptCreateFolder()">New Folder</button>
          </div>
        </div>
        <div id="filesTableContainer">
          <p class="empty-state">Connect your Playdate to browse files.</p>
        </div>
      </div>
      <div id="files-plugin-container"></div>
    </section>

    <!-- TAB 3: SETTINGS (/settings) -->
    <section id="tab-settings" class="hub-section">
      <div id="settings-container">
        <div class="card"><p class="empty-state">Connect your Playdate to load settings.</p></div>
      </div>
      <div style="margin:16px 0;display:none;" id="save-container">
        <button class="save-btn" id="saveBtn" onclick="saveSettings()">Save Settings</button>
      </div>
      <div id="opds-container"></div>
    </section>

    <!-- TAB 4: FONTS (/fonts) -->
    <section id="tab-fonts" class="hub-section">
      <div class="card">
        <h2>Installed Fonts</h2>
        <div id="families"><p class="empty-state">Connect your Playdate to view installed fonts.</p></div>
      </div>
      <div class="card">
        <h2>Upload Font Family</h2>
        <p style="color:var(--label-color);font-size:0.9em;margin:0 0 12px;">
          Select a folder or multiple <code>.cpfont</code>, <code>.ttf</code>, or <code>.otf</code> files belonging to one font family.
        </p>
        <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;">
          <input type="file" id="fontFilesInput" multiple />
          <button type="button" class="btn btn-primary" onclick="uploadSelectedFonts()">Upload Font</button>
        </div>
      </div>
    </section>
  </main>

  <!-- Standard CrossPoint Footer + Dialog/Toast -->
  <footer class="site-footer">
    <div class="footer-inner">
      <div class="footer-main">
        <div class="footer-brand">
          <a class="brand-link" href="#plugins" onclick="switchTab('plugins');return false;">
            <img class="brand-logo" src="{icon_data_uri}" alt="" width="28" height="28" />
            CrossPoint <span class="brand-reader">Reader</span>
          </a>
          <p>Playdate Web Serial &amp; Data Disk Companion Hub — works with all CrossPoint web plugins.</p>
        </div>
      </div>
    </div>
  </footer>

  <dialog id="webMessageDialog" class="web-dialog" aria-labelledby="webDialogTitle" aria-describedby="webDialogMessage">
    <form method="dialog">
      <h2 id="webDialogTitle"></h2>
      <p id="webDialogMessage"></p>
      <div class="web-dialog-actions">
        <button id="webDialogCancel" value="cancel">Cancel</button>
        <button id="webDialogConfirm" value="confirm">OK</button>
      </div>
    </form>
  </dialog>
  <div id="webToast" class="toast" role="status" aria-live="polite"></div>

  <!-- Embedded JSZip -->
  <script>
{jszip_js}
  </script>

  <!-- Dialog & Toast Helpers -->
  <script>
    document.cookie = "cpPlugins=1; path=/; SameSite=Strict";
    const webMessageDialog = document.getElementById('webMessageDialog');
    const webDialogTitle = document.getElementById('webDialogTitle');
    const webDialogMessage = document.getElementById('webDialogMessage');
    const webDialogCancel = document.getElementById('webDialogCancel');
    const webDialogConfirm = document.getElementById('webDialogConfirm');
    let webDialogQueue = Promise.resolve();

    function showWebDialog(title, message, confirm) {{
      const result = webDialogQueue.then(() => new Promise(resolve => {{
        webDialogTitle.textContent = title;
        webDialogMessage.textContent = String(message);
        webDialogCancel.hidden = !confirm;
        webDialogConfirm.textContent = confirm ? 'Confirm' : 'OK';
        webMessageDialog.returnValue = '';
        webMessageDialog.addEventListener('close', () => {{
          resolve(webMessageDialog.returnValue === 'confirm');
        }}, {{ once: true }});
        webMessageDialog.showModal();
      }}));
      webDialogQueue = result.then(() => undefined, () => undefined);
      return result;
    }}
    function showWebAlert(message) {{
      return showWebDialog('Notice', message, false).then(() => undefined);
    }}
    function showWebConfirm(message) {{
      return showWebDialog('Confirm action', message, true);
    }}
    const webToast = document.getElementById('webToast');
    let webToastTimeout;
    function showWebToast(message, isError) {{
      clearTimeout(webToastTimeout);
      webToast.textContent = message;
      webToast.className = 'toast' + (isError ? ' error' : '');
      webToast.style.display = 'block';
      webToastTimeout = setTimeout(() => {{ webToast.style.display = 'none'; }}, 4500);
    }}
  </script>

  <!-- Playdate Storage Backends (Web Serial + File System Access + Local Host) & Virtual /api/* Router -->
  <script>
    const nativeFetch = window.fetch.bind(window);
    const PLAYDATE_MAC_KEY = new Uint8Array([0x02, 0x00, 0x50, 0x44, 0x00, 0x01]);
    const DEFAULT_DEVICE_SECRET = new Uint8Array(32);
    for (let i = 0; i < 32; i++) DEFAULT_DEVICE_SECRET[i] = (0x50 + i * 17) & 0xFF;

    const FIRMWARE_FAMILY = "crosspoint";
    const PLUGIN_API_VERSION = 1;
    const PLUGIN_CAPABILITIES =
      "catalog.lists,catalog.notices,catalog.bundle_install,catalog.version_tracking," +
      "catalog.compatibility,catalog.item_details,catalog.encoded_item_fields," +
      "catalog.list_index_mapping,catalog.list_requirements,catalog.server_paths";
    const PLUGIN_ROOTS = ["/.crosspoint/plugins", "/plugins", "/.plugins"];

    function normalizeVPath(p) {{
      if (!p) return "/";
      const parts = [];
      for (const seg of String(p).replace(/\\\\/g, "/").split("/")) {{
        if (!seg || seg === ".") continue;
        if (seg === "..") {{ if (parts.length) parts.pop(); }}
        else parts.push(seg);
      }}
      return "/" + parts.join("/");
    }}

    function uint8ToBase64(bytes) {{
      let bin = "";
      const chunk = 0x8000;
      for (let i = 0; i < bytes.length; i += chunk) {{
        bin += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
      }}
      return btoa(bin);
    }}

    function base64ToUint8(b64) {{
      const bin = atob(b64 || "");
      const out = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
      return out;
    }}

    function obfuscateStr(plain) {{
      if (!plain) return "";
      const raw = new TextEncoder().encode(String(plain));
      const xored = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; i++) {{
        xored[i] = raw[i] ^ PLAYDATE_MAC_KEY[i % PLAYDATE_MAC_KEY.length];
      }}
      return uint8ToBase64(xored);
    }}

    function deobfuscateStr(encoded) {{
      if (!encoded) return "";
      try {{
        const xored = base64ToUint8(encoded);
        const raw = new Uint8Array(xored.length);
        for (let i = 0; i < xored.length; i++) {{
          raw[i] = xored[i] ^ PLAYDATE_MAC_KEY[i % PLAYDATE_MAC_KEY.length];
        }}
        return new TextDecoder().decode(raw);
      }} catch {{
        return "";
      }}
    }}

    function setConnectionStatus(state, title, subtitle) {{
      const dot = document.getElementById("pdDot");
      dot.className = "pd-dot" + (state === "connected" ? " connected" : state === "busy" ? " busy" : "");
      document.getElementById("pdModeLabel").textContent = title;
      document.getElementById("pdTargetLabel").textContent = subtitle;
    }}

    // ---- 1. Web Serial Storage Backend (`navigator.serial` -> `PlaydateSerialBridge`) ----
    class WebSerialBackend {{
      constructor(port) {{
        this.port = port;
        this.modeName = "Live USB Serial";
        this.targetDescription = "Playdate USB CDC Serial (115200 baud)";
        this.rxBuffer = "";
        this.lineWaiters = [];
        this.reader = null;
        this.writer = null;
        this.mutex = Promise.resolve();
      }}

      async open() {{
        await this.port.open({{ baudRate: 115200 }});
        this.writer = this.port.writable.getWriter();
        this._startReadLoop();
        const ok = await this.ping();
        if (!ok) {{
          throw new Error(
            "Connected to serial port, but CrossPoint did not respond to !CP PING. " +
            "Make sure CrossPoint Reader is open on your Playdate!"
          );
        }}
      }}

      _startReadLoop() {{
        const decoder = new TextDecoder();
        this.reader = this.port.readable.getReader();
        (async () => {{
          try {{
            while (true) {{
              const {{ value, done }} = await this.reader.read();
              if (done) break;
              if (value) {{
                this.rxBuffer += decoder.decode(value, {{ stream: true }});
                let nl;
                while ((nl = this.rxBuffer.indexOf("\\n")) !== -1) {{
                  const line = this.rxBuffer.slice(0, nl).replace(/\\r$/, "");
                  this.rxBuffer = this.rxBuffer.slice(nl + 1);
                  if (line.includes("@CP ") && this.lineWaiters.length > 0) {{
                    const w = this.lineWaiters.shift();
                    w.resolve(line);
                  }}
                }}
              }}
            }}
          }} catch (e) {{
            console.warn("WebSerial read loop ended:", e);
            while (this.lineWaiters.length > 0) {{
              this.lineWaiters.shift().reject(e);
            }}
          }}
        }})();
      }}

      _nextCpLine(timeoutMs = 6000) {{
        return new Promise((resolve, reject) => {{
          const timer = setTimeout(() => {{
            const idx = this.lineWaiters.findIndex(w => w.resolve === wrappedResolve);
            if (idx !== -1) this.lineWaiters.splice(idx, 1);
            reject(new Error("Timed out waiting for Playdate USB Serial response"));
          }}, timeoutMs);
          const wrappedResolve = (line) => {{
            clearTimeout(timer);
            resolve(line);
          }};
          const wrappedReject = (err) => {{
            clearTimeout(timer);
            reject(err);
          }};
          this.lineWaiters.push({{ resolve: wrappedResolve, reject: wrappedReject }});
        }});
      }}

      _cmd(cpArgs, timeoutMs = 6000) {{
        const run = async () => {{
          this.lineWaiters.length = 0;
          const payload = new TextEncoder().encode("msg !CP " + cpArgs + "\\n");
          await this.writer.write(payload);
          const items = [];
          const dataChunks = [];
          while (true) {{
            const line = await this._nextCpLine(timeoutMs);
            const idx = line.indexOf("@CP ");
            if (idx === -1) continue;
            const resp = line.slice(idx + 4);
            if (resp.startsWith("ITEM ")) {{
              items.push(resp.slice(5));
            }} else if (resp.startsWith("DATA ")) {{
              dataChunks.push(resp.slice(5));
            }} else if (resp.startsWith("OK")) {{
              return {{ ok: true, rest: resp.slice(2).trim(), items, dataChunks }};
            }} else if (resp.startsWith("ERR")) {{
              return {{ ok: false, rest: resp.slice(3).trim(), items, dataChunks }};
            }}
          }}
        }};
        const next = this.mutex.then(run, run);
        this.mutex = next.then(() => undefined, () => undefined);
        return next;
      }}

      async ping() {{
        try {{
          const {{ ok, rest }} = await this._cmd("PING", 2500);
          if (!ok) return false;
          const doc = JSON.parse(rest);
          if (doc.version) this.targetDescription = `Playdate (${{doc.version}}) via Live USB Serial`;
          return !!doc.ok;
        }} catch {{
          return false;
        }}
      }}

      async stat(vpath) {{
        vpath = normalizeVPath(vpath);
        const {{ ok, rest }} = await this._cmd("STAT " + vpath);
        if (!ok) return {{ exists: false, isDir: false, size: 0 }};
        try {{
          const doc = JSON.parse(rest);
          return {{ exists: !!doc.exists, isDir: !!doc.isDir, size: Number(doc.size || 0) }};
        }} catch {{
          return {{ exists: false, isDir: false, size: 0 }};
        }}
      }}

      async exists(vpath) {{
        return (await this.stat(vpath)).exists;
      }}

      async isDir(vpath) {{
        const st = await this.stat(vpath);
        return st.exists && st.isDir;
      }}

      async listDir(vpath) {{
        vpath = normalizeVPath(vpath);
        const {{ ok, items }} = await this._cmd("LIST " + vpath);
        if (!ok) return [];
        const out = [];
        for (const raw of items) {{
          try {{
            const d = JSON.parse(raw);
            out.push({{ name: String(d.name || ""), isDir: !!d.isDir, size: Number(d.size || 0) }});
          }} catch {{}}
        }}
        return out.sort((a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase()));
      }}

      async readFile(vpath) {{
        vpath = normalizeVPath(vpath);
        const st = await this.stat(vpath);
        if (!st.exists || st.isDir) throw new Error("Not found: " + vpath);
        const totalSize = st.size;
        const chunks = [];
        let offset = 0;
        const step = 12288;
        while (offset < totalSize || (totalSize === 0 && offset === 0)) {{
          const {{ ok, rest, dataChunks }} = await this._cmd(`READ ${{offset}} ${{step}} ${{vpath}}`, 12000);
          if (!ok) throw new Error("Serial READ failed: " + rest);
          const b64 = dataChunks.join("");
          const part = b64 ? base64ToUint8(b64) : new Uint8Array(0);
          chunks.push(part);
          if (part.length < step || totalSize === 0) break;
          offset += part.length;
        }}
        const total = chunks.reduce((acc, c) => acc + c.length, 0);
        const res = new Uint8Array(total);
        let pos = 0;
        for (const c of chunks) {{
          res.set(c, pos);
          pos += c.length;
        }}
        return res;
      }}

      async writeFile(vpath, dataBytes) {{
        vpath = normalizeVPath(vpath);
        const bytes = dataBytes instanceof Uint8Array ? dataBytes : new Uint8Array(dataBytes);
        const openRes = await this._cmd("WOPEN " + vpath);
        if (!openRes.ok) throw new Error("Serial WOPEN failed: " + openRes.rest);
        const step = 120;
        let sinceFlush = 0;
        const enc = new TextEncoder();
        for (let i = 0; i < bytes.length; i += step) {{
          const slice = bytes.subarray(i, i + step);
          const b64 = uint8ToBase64(slice);
          await this.mutex.then(async () => {{
            await this.writer.write(enc.encode("msg !CP WDATA " + b64 + "\\n"));
          }});
          sinceFlush += slice.length;
          if (sinceFlush >= 3600) {{
            const fl = await this._cmd("WFLUSH", 8000);
            if (!fl.ok) throw new Error("Serial WFLUSH failed: " + fl.rest);
            sinceFlush = 0;
          }}
        }}
        const cl = await this._cmd("WCLOSE", 8000);
        if (!cl.ok) throw new Error("Serial WCLOSE failed: " + cl.rest);
      }}

      async mkdir(vpath) {{
        vpath = normalizeVPath(vpath);
        await this._cmd("MKDIR " + vpath);
      }}

      async remove(vpath) {{
        vpath = normalizeVPath(vpath);
        const {{ ok }} = await this._cmd("RM " + vpath);
        return ok;
      }}

      async rename(oldPath, newPath) {{
        oldPath = normalizeVPath(oldPath);
        newPath = normalizeVPath(newPath);
        const {{ ok }} = await this._cmd(`RENAME ${{oldPath}}\\t${{newPath}}`);
        return ok;
      }}

      async relayHttp(reqObj) {{
        const reqPath = "/.crosspoint/.serial-relay-req.json";
        await this.writeFile(reqPath, new TextEncoder().encode(JSON.stringify(reqObj)));
        const {{ ok, rest, dataChunks }} = await this._cmd("RELAY " + reqPath, 35000);
        if (!ok) throw new Error(rest || "Serial HTTP relay failed");
        const meta = JSON.parse(rest || "{{}}");
        const b64 = dataChunks.join("");
        const bodyBytes = b64 ? base64ToUint8(b64) : new Uint8Array(0);
        return {{
          status: Number(meta.status || 200),
          bytes: Number(meta.bytes || bodyBytes.length),
          bodyBytes
        }};
      }}
    }}

    // ---- 2. File System Access API Backend (`window.showDirectoryPicker`) ----
    class FileSystemAccessBackend {{
      constructor(rootHandle, label) {{
        this.rootHandle = rootHandle;
        this.modeName = "Playdate Data Disk";
        this.targetDescription = label || rootHandle.name;
      }}

      static async resolveCrossPointDataHandle(pickedHandle) {{
        // Check if pickedHandle is PLAYDATE root (contains Data/), or Data/ (contains *com.crosspoint.reader*),
        // or already the app data folder (contains .crosspoint or books).
        const entries = [];
        for await (const [name, handle] of pickedHandle.entries()) {{
          entries.push({{ name, handle }});
        }}
        const findAppDirIn = async (dirHandle, prefixLabel) => {{
          const candidates = [];
          for await (const [name, handle] of dirHandle.entries()) {{
            if (handle.kind === "directory" && name.includes("com.crosspoint.reader")) {{
              let hasDotCp = false;
              try {{
                await handle.getDirectoryHandle(".crosspoint");
                hasDotCp = true;
              }} catch {{}}
              candidates.push({{ name, handle, hasDotCp, isUser: name.startsWith("user.") }});
            }}
          }}
          if (candidates.length > 0) {{
            candidates.sort((a, b) => (Number(b.hasDotCp) - Number(a.hasDotCp)) || (Number(b.isUser) - Number(a.isUser)));
            return {{ handle: candidates[0].handle, label: `${{prefixLabel}}/${{candidates[0].name}}` }};
          }}
          const created = await dirHandle.getDirectoryHandle("com.crosspoint.reader", {{ create: true }});
          return {{ handle: created, label: `${{prefixLabel}}/com.crosspoint.reader` }};
        }};

        if (pickedHandle.name.includes("com.crosspoint.reader") || entries.some(e => e.name === ".crosspoint")) {{
          return {{ handle: pickedHandle, label: pickedHandle.name }};
        }}
        const dataSub = entries.find(e => e.handle.kind === "directory" && e.name.toLowerCase() === "data");
        if (dataSub) {{
          return await findAppDirIn(dataSub.handle, `${{pickedHandle.name}}/Data`);
        }}
        if (pickedHandle.name.toLowerCase() === "data" || entries.some(e => e.name.includes("com.crosspoint.reader"))) {{
          return await findAppDirIn(pickedHandle, pickedHandle.name);
        }}
        return {{ handle: pickedHandle, label: pickedHandle.name }};
      }}

      async _resolveParentAndLeaf(vpath, createParents = false) {{
        const norm = normalizeVPath(vpath);
        const parts = norm.split("/").filter(Boolean);
        if (parts.length === 0) return {{ parent: null, leaf: "", handle: this.rootHandle }};
        const leaf = parts.pop();
        let curr = this.rootHandle;
        for (const seg of parts) {{
          curr = await curr.getDirectoryHandle(seg, {{ create: createParents }});
        }}
        return {{ parent: curr, leaf }};
      }}

      async stat(vpath) {{
        const norm = normalizeVPath(vpath);
        if (norm === "/") return {{ exists: true, isDir: true, size: 0 }};
        try {{
          const {{ parent, leaf }} = await this._resolveParentAndLeaf(norm, false);
          try {{
            const fh = await parent.getFileHandle(leaf);
            const file = await fh.getFile();
            return {{ exists: true, isDir: false, size: file.size }};
          }} catch {{
            await parent.getDirectoryHandle(leaf);
            return {{ exists: true, isDir: true, size: 0 }};
          }}
        }} catch {{
          return {{ exists: false, isDir: false, size: 0 }};
        }}
      }}

      async exists(vpath) {{
        return (await this.stat(vpath)).exists;
      }}

      async isDir(vpath) {{
        const st = await this.stat(vpath);
        return st.exists && st.isDir;
      }}

      async listDir(vpath) {{
        const norm = normalizeVPath(vpath);
        try {{
          let dirHandle = this.rootHandle;
          if (norm !== "/") {{
            const {{ parent, leaf }} = await this._resolveParentAndLeaf(norm, false);
            dirHandle = await parent.getDirectoryHandle(leaf);
          }}
          const out = [];
          for await (const [name, handle] of dirHandle.entries()) {{
            if (handle.kind === "directory") {{
              out.push({{ name, isDir: true, size: 0 }});
            }} else {{
              const f = await handle.getFile();
              out.push({{ name, isDir: false, size: f.size }});
            }}
          }}
          return out.sort((a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase()));
        }} catch {{
          return [];
        }}
      }}

      async readFile(vpath) {{
        const {{ parent, leaf }} = await this._resolveParentAndLeaf(vpath, false);
        const fh = await parent.getFileHandle(leaf);
        const file = await fh.getFile();
        return new Uint8Array(await file.arrayBuffer());
      }}

      async writeFile(vpath, dataBytes) {{
        const bytes = dataBytes instanceof Uint8Array ? dataBytes : new Uint8Array(dataBytes);
        const {{ parent, leaf }} = await this._resolveParentAndLeaf(vpath, true);
        const fh = await parent.getFileHandle(leaf, {{ create: true }});
        const writable = await fh.createWritable();
        await writable.write(bytes);
        await writable.close();
      }}

      async mkdir(vpath) {{
        const norm = normalizeVPath(vpath);
        if (norm === "/") return;
        const {{ parent, leaf }} = await this._resolveParentAndLeaf(norm, true);
        await parent.getDirectoryHandle(leaf, {{ create: true }});
      }}

      async remove(vpath) {{
        const norm = normalizeVPath(vpath);
        if (norm === "/") return false;
        try {{
          const {{ parent, leaf }} = await this._resolveParentAndLeaf(norm, false);
          await parent.removeEntry(leaf, {{ recursive: true }});
          return true;
        }} catch {{
          return false;
        }}
      }}

      async rename(oldPath, newPath) {{
        try {{
          const data = await this.readFile(oldPath);
          await this.writeFile(newPath, data);
          await this.remove(oldPath);
          return true;
        }} catch {{
          return false;
        }}
      }}
    }}

    let activeBackend = null;
    let useLocalPythonBackend = false;

    async function loadJsonFile(vpath, fallback = {{}}) {{
      try {{
        if (await activeBackend.exists(vpath)) {{
          const raw = await activeBackend.readFile(vpath);
          return JSON.parse(new TextDecoder().decode(raw));
        }}
      }} catch {{}}
      return fallback;
    }}

    async function saveJsonFile(vpath, obj) {{
      const bytes = new TextEncoder().encode(JSON.stringify(obj, null, 2));
      await activeBackend.writeFile(vpath, bytes);
    }}

    async function getDeviceSecret() {{
      try {{
        if (await activeBackend.exists("/.crosspoint/device_secret.bin")) {{
          const raw = await activeBackend.readFile("/.crosspoint/device_secret.bin");
          if (raw.length === 32) return raw;
        }}
      }} catch {{}}
      return DEFAULT_DEVICE_SECRET;
    }}

    async function getPluginDeviceId(pluginName) {{
      const secret = await getDeviceSecret();
      const pBytes = new TextEncoder().encode(String(pluginName || "").trim().toLowerCase().slice(0, 64));
      const combined = new Uint8Array(secret.length + pBytes.length);
      combined.set(secret, 0);
      combined.set(pBytes, secret.length);
      const hashBuf = await crypto.subtle.digest("SHA-256", combined);
      return Array.from(new Uint8Array(hashBuf)).map(b => b.toString(16).padStart(2, "0")).join("");
    }}

    async function scanInstalledPlugins() {{
      const seen = new Set();
      const out = [];
      for (const root of PLUGIN_ROOTS) {{
        const entries = await activeBackend.listDir(root);
        for (const e of entries) {{
          if (!e.isDir) continue;
          const key = e.name.toLowerCase();
          if (seen.has(key)) continue;
          const pdir = `${{root}}/${{e.name}}`;
          if (!(await activeBackend.exists(`${{pdir}}/plugin.js`))) continue;
          seen.add(key);
          const item = {{ name: e.name, dir: pdir, title: e.name, mount: "settings" }};
          if (await activeBackend.exists(`${{pdir}}/manifest.json`)) {{
            try {{
              const m = await loadJsonFile(`${{pdir}}/manifest.json`, {{}});
              if (typeof m.title === "string" && m.title) item.title = m.title;
              if (typeof m.mount === "string" && m.mount) item.mount = m.mount;
            }} catch {{}}
          }}
          out.push(item);
        }}
      }}
      return out;
    }}

    async function findPluginDir(name) {{
      const target = String(name || "").toLowerCase();
      for (const root of PLUGIN_ROOTS) {{
        const entries = await activeBackend.listDir(root);
        for (const e of entries) {{
          if (e.isDir && e.name.toLowerCase() === target) return `${{root}}/${{e.name}}`;
        }}
      }}
      return "";
    }}

    async function buildSettingsArray(keysFilter) {{
      const stored = await loadJsonFile("/.crosspoint/settings.json", {{}});
      const kr = await loadJsonFile("/.crosspoint/koreader.json", {{}});
      const krHeaders = Array.isArray(kr.headers) ? kr.headers : [];
      while (krHeaders.length < 2) krHeaders.push({{ name: "", value_obf: "" }});
      const krPass = deobfuscateStr(kr.password_obf || "") || kr.password || "";
      const h1Val = deobfuscateStr(krHeaders[0].value_obf || "");
      const h2Val = deobfuscateStr(krHeaders[1].value_obf || "");

      const items = [
        {{ key: "koUsername", name: "KOReader Username", category: "KOReader Sync", type: "string", value: kr.username || "" }},
        {{ key: "koPassword", name: "KOReader Password", category: "KOReader Sync", type: "string", value: null, hasPassword: !!krPass }},
        {{ key: "koServerUrl", name: "Sync Server URL", category: "KOReader Sync", type: "string", value: kr.serverUrl ?? kr.server_url ?? "" }},
        {{ key: "koHeader1Name", name: "Custom Header 1 Name", category: "KOReader Sync", type: "string", value: krHeaders[0].name || "" }},
        {{ key: "koHeader1Value", name: "Custom Header 1 Value", category: "KOReader Sync", type: "string", value: null, hasPassword: !!h1Val }},
        {{ key: "koHeader2Name", name: "Custom Header 2 Name", category: "KOReader Sync", type: "string", value: krHeaders[1].name || "" }},
        {{ key: "koHeader2Value", name: "Custom Header 2 Value", category: "KOReader Sync", type: "string", value: null, hasPassword: !!h2Val }},
        {{ key: "koServerType", name: "Server Type", category: "KOReader Sync", type: "enum", value: Number(kr.serverType ?? 0), options: ["CrossPoint", "KOReader Sync Server", "Other"] }},
        {{ key: "koMatchMethod", name: "Document Matching", category: "KOReader Sync", type: "enum", value: Number(kr.matchMethod ?? kr.match_method ?? 0), options: ["Filename", "Binary"] }},
        {{ key: "koSendMetadata", name: "Send Metadata", category: "KOReader Sync", type: "toggle", value: (kr.sendMetadata ?? kr.send_metadata) ? 1 : 0 }},
        {{ key: "koSyncBehavior", name: "Sync Behavior", category: "KOReader Sync", type: "enum", value: Number(kr.syncBehavior ?? kr.sync_behavior ?? 0), options: ["Ask Every Time", "Smart Sync"] }},
        {{ key: "koSyncClippings", name: "Sync Clippings", category: "KOReader Sync", type: "toggle", value: kr.syncClippings ? 1 : 0 }},
        {{ key: "extraParagraphSpacing", name: "Paragraph Spacing", category: "Reader", type: "toggle", value: Number(stored.extraParagraphSpacing ?? 1) }},
        {{ key: "hyphenationEnabled", name: "Hyphenation", category: "Reader", type: "toggle", value: Number(stored.hyphenationEnabled ?? 1) }},
        {{ key: "screenMargin", name: "Screen Margin", category: "Reader", type: "value", value: Number(stored.screenMargin ?? 5), min: 0, max: 40, step: 1 }},
        {{ key: "showHiddenFiles", name: "Show Hidden Files", category: "System", type: "toggle", value: Number(stored.showHiddenFiles ?? 0) }},
        {{ key: "libraryUseMetadata", name: "Library Use Metadata", category: "System", type: "toggle", value: Number(stored.libraryUseMetadata ?? 1) }},
        {{ key: "clockShowHeader", name: "Clock in Header", category: "Status Bar", type: "toggle", value: Number(stored.clockShowHeader ?? 0) }},
      ];
      if (keysFilter && keysFilter.size > 0) {{
        return items.filter(i => keysFilter.has(i.key));
      }}
      return items;
    }}

    async function applySettingsUpdates(updates) {{
      const stored = await loadJsonFile("/.crosspoint/settings.json", {{}});
      const kr = await loadJsonFile("/.crosspoint/koreader.json", {{}});
      if (!("cfgVersion" in kr)) kr.cfgVersion = 3;
      const krHeaders = Array.isArray(kr.headers) ? kr.headers : [];
      while (krHeaders.length < 2) krHeaders.push({{ name: "", value_obf: "" }});
      let krChanged = false;
      let storedChanged = false;

      for (const [k, v] of Object.entries(updates || {{}})) {{
        if (k === "koUsername" || k === "koreaderUsername") {{ kr.username = String(v); krChanged = true; }}
        else if (k === "koPassword" || k === "koreaderPassword") {{ kr.password_obf = obfuscateStr(String(v)); delete kr.password; krChanged = true; }}
        else if (k === "koServerUrl" || k === "koreaderSyncServerUrl") {{ kr.serverUrl = String(v); krChanged = true; }}
        else if (k === "koHeader1Name") {{ krHeaders[0].name = String(v); krChanged = true; }}
        else if (k === "koHeader1Value") {{ krHeaders[0].value_obf = obfuscateStr(String(v)); krChanged = true; }}
        else if (k === "koHeader2Name") {{ krHeaders[1].name = String(v); krChanged = true; }}
        else if (k === "koHeader2Value") {{ krHeaders[1].value_obf = obfuscateStr(String(v)); krChanged = true; }}
        else if (k === "koServerType") {{ kr.serverType = Number(v); krChanged = true; }}
        else if (k === "koMatchMethod" || k === "koreaderMatchMethod") {{ kr.matchMethod = Number(v); krChanged = true; }}
        else if (k === "koSendMetadata" || k === "koreaderSendMetadata") {{ kr.sendMetadata = Boolean(Number(v)); krChanged = true; }}
        else if (k === "koSyncBehavior" || k === "koreaderSyncBehavior") {{ kr.syncBehavior = Number(v); krChanged = true; }}
        else if (k === "koSyncClippings") {{ kr.syncClippings = Boolean(Number(v)); krChanged = true; }}
        else {{ stored[k] = v; storedChanged = true; }}
      }}
      stored.pluginsEnabled = 1;
      if (krChanged) {{
        kr.headers = krHeaders;
        await saveJsonFile("/.crosspoint/koreader.json", kr);
      }}
      if (storedChanged || !(await activeBackend.exists("/.crosspoint/settings.json"))) {{
        await saveJsonFile("/.crosspoint/settings.json", stored);
      }}
    }}

    function jsonResponse(status, obj, extraHeaders = {{}}) {{
      const body = typeof obj === "string" ? obj : JSON.stringify(obj);
      return new Response(body, {{
        status,
        headers: Object.assign({{ "Content-Type": "application/json; charset=utf-8" }}, extraHeaders)
      }});
    }}

    function textResponse(status, text, contentType = "text/plain; charset=utf-8", extraHeaders = {{}}) {{
      return new Response(text, {{
        status,
        headers: Object.assign({{ "Content-Type": contentType }}, extraHeaders)
      }});
    }}

    async function performOutboundFetch(url, method, headersObj, bodyStr) {{
      const fetchOpts = {{
        method,
        headers: headersObj || {{}},
        credentials: "omit",
        mode: "cors"
      }};
      if (method !== "GET" && method !== "HEAD" && bodyStr) {{
        fetchOpts.body = bodyStr;
      }}
      try {{
        return await nativeFetch(url, fetchOpts);
      }} catch (directErr) {{
        // Fallback 1: If connected to Playdate via Web Serial, relay the HTTP(S) request
        // directly through the Playdate's own Wi-Fi stack (`!CP RELAY`)!
        if (activeBackend && typeof activeBackend.relayHttp === "function") {{
          try {{
            const rel = await activeBackend.relayHttp({{
              url,
              method,
              headers: headersObj || {{}},
              body: bodyStr || ""
            }});
            return new Response(rel.bodyBytes, {{
              status: rel.status,
              headers: {{ "Content-Type": "application/octet-stream" }}
            }});
          }} catch (serialRelayErr) {{
            console.warn("Playdate WebSerial HTTP relay fallback failed:", serialRelayErr);
          }}
        }}
        // Fallback 2: If local web_host.py is running on 127.0.0.1:8080, use its /api/relay
        try {{
          const relayRes = await nativeFetch("http://127.0.0.1:8080/api/relay", {{
            method: "POST",
            headers: {{ "Content-Type": "application/json" }},
            body: JSON.stringify({{ plugin: "hub", method, url, headers: headersObj || {{}}, body: bodyStr || "" }})
          }});
          if (relayRes.ok) {{
            const upstreamStatus = Number(relayRes.headers.get("X-Relay-Status") || 200);
            const rawHdrs = JSON.parse(relayRes.headers.get("X-Relay-Headers") || "[]");
            const buf = await relayRes.arrayBuffer();
            return new Response(buf, {{ status: upstreamStatus, headers: rawHdrs }});
          }}
        }} catch {{}}
        // Fallback 3: Public CORS proxy for Data Disk mode when endpoint lacks CORS headers
        const proxyUrl = "https://corsproxy.io/?url=" + encodeURIComponent(url);
        return await nativeFetch(proxyUrl, fetchOpts);
      }}
    }}

    async function handleVirtualRequest(urlObj, method, init) {{
      const route = urlObj.pathname;
      const qs = urlObj.searchParams;

      const parseBodyText = async () => {{
        if (!init || !init.body) return "";
        if (typeof init.body === "string") return init.body;
        if (init.body instanceof URLSearchParams) return init.body.toString();
        if (init.body instanceof FormData) {{
          const obj = {{}};
          for (const [k, v] of init.body.entries()) {{
            if (typeof v === "string") obj[k] = v;
          }}
          return JSON.stringify(obj);
        }}
        return await new Response(init.body).text();
      }};

      if (route === "/api/status" && method === "GET") {{
        const resp = {{
          version: "1.2.0-playdate",
          firmwareFamily: FIRMWARE_FAMILY,
          pluginApi: PLUGIN_API_VERSION,
          pluginCapabilities: PLUGIN_CAPABILITIES,
          ip: "127.0.0.1",
          mode: "STA",
          rssi: -45,
          freeHeap: 8388608,
          uptime: 3600,
          device: "Playdate",
          deviceModel: "Playdate",
          psramBytes: 16 * 1024 * 1024,
          serial: "Playdate",
          storageMode: activeBackend.modeName,
          storageTarget: activeBackend.targetDescription
        }};
        const plugin = qs.get("plugin");
        if (plugin) resp.deviceId = await getPluginDeviceId(plugin);
        return jsonResponse(200, resp);
      }}

      if (route === "/api/plugins" && method === "GET") {{
        return jsonResponse(200, await scanInstalledPlugins());
      }}

      if (route === "/plugin" && method === "GET") {{
        const name = qs.get("name") || qs.get("id") || "";
        const fn = qs.get("file") || "";
        if (!name || !fn || name.includes("..") || fn.includes("..")) {{
          return textResponse(400, "bad plugin path");
        }}
        const pdir = await findPluginDir(name);
        if (!pdir) return textResponse(404, "not found");
        const vpath = `${{pdir}}/${{fn}}`;
        if (!(await activeBackend.exists(vpath))) return textResponse(404, "not found");
        const bytes = await activeBackend.readFile(vpath);
        let mime = "application/octet-stream";
        if (fn.endsWith(".js")) mime = "application/javascript; charset=utf-8";
        else if (fn.endsWith(".json")) mime = "application/json; charset=utf-8";
        else if (fn.endsWith(".css")) mime = "text/css; charset=utf-8";
        else if (fn.endsWith(".html")) mime = "text/html; charset=utf-8";
        return new Response(bytes, {{ status: 200, headers: {{ "Content-Type": mime }} }});
      }}

      if (route === "/api/relay" && method === "POST") {{
        const req = JSON.parse((await parseBodyText()) || "{{}}");
        const targetUrl = req.url || "";
        const reqMethod = String(req.method || "GET").toUpperCase();
        const reqHeaders = req.headers || {{}};
        const reqBody = req.body || "";
        try {{
          const up = await performOutboundFetch(targetUrl, reqMethod, reqHeaders, reqBody);
          const buf = await up.arrayBuffer();
          const hdrList = [];
          up.headers.forEach((v, k) => hdrList.push([k, v]));
          return new Response(buf, {{
            status: 200,
            headers: {{
              "Content-Type": "application/octet-stream",
              "X-Relay-Status": String(up.status),
              "X-Relay-Headers": JSON.stringify(hdrList)
            }}
          }});
        }} catch (e) {{
          return jsonResponse(502, {{ error: "relay failed: " + e.message }});
        }}
      }}

      if (route === "/api/fetch" && method === "POST") {{
        const req = JSON.parse((await parseBodyText()) || "{{}}");
        const targetUrl = req.url || "";
        const dest = normalizeVPath(req.dest || req.path || "");
        const reqMethod = String(req.method || "GET").toUpperCase();
        const reqHeaders = req.headers || {{}};
        const reqBody = req.body || "";
        if (!targetUrl || !dest || dest === "/") {{
          return jsonResponse(400, {{ error: "bad url/dest" }});
        }}
        try {{
          // If connected via Web Serial and direct browser fetch fails due to CORS,
          // let Playdate download directly to `dest` on its filesystem over Wi-Fi!
          let up;
          try {{
            const fetchOpts = {{
              method: reqMethod,
              headers: reqHeaders,
              credentials: "omit",
              mode: "cors"
            }};
            if (reqMethod !== "GET" && reqMethod !== "HEAD" && reqBody) {{
              fetchOpts.body = reqBody;
            }}
            up = await nativeFetch(targetUrl, fetchOpts);
          }} catch (corsErr) {{
            if (activeBackend && typeof activeBackend.relayHttp === "function") {{
              const rel = await activeBackend.relayHttp({{
                url: targetUrl,
                method: reqMethod,
                headers: reqHeaders,
                body: reqBody,
                dest
              }});
              return jsonResponse(200, {{
                status: rel.status,
                bytes: rel.bytes,
                total: rel.bytes,
                complete: rel.status >= 200 && rel.status < 300
              }});
            }}
            up = await performOutboundFetch(targetUrl, reqMethod, reqHeaders, reqBody);
          }}
          const bytes = new Uint8Array(await up.arrayBuffer());
          if (up.status < 200 || up.status >= 300) {{
            const errText = new TextDecoder().decode(bytes.subarray(0, 512));
            return jsonResponse(200, {{ status: up.status, bytes: 0, complete: false, error: "http status", body: errText }});
          }}
          await activeBackend.writeFile(dest, bytes);
          return jsonResponse(200, {{ status: up.status, bytes: bytes.length, total: bytes.length, complete: true }});
        }} catch (e) {{
          return jsonResponse(200, {{ status: -1, bytes: 0, complete: false, error: "transport failure: " + e.message }});
        }}
      }}

      if (route === "/api/plugin-fs" && method === "POST") {{
        const target = normalizeVPath(qs.get("path") || "");
        if (!target || target === "/" || target === "/.crosspoint/device_secret.bin") {{
          return jsonResponse(400, {{ error: "bad path" }});
        }}
        let bytes = null;
        if (init && init.body instanceof FormData) {{
          const filePart = init.body.get("file");
          if (filePart && typeof filePart.arrayBuffer === "function") {{
            bytes = new Uint8Array(await filePart.arrayBuffer());
          }}
        }}
        if (!bytes && init && init.body) {{
          bytes = new Uint8Array(await new Response(init.body).arrayBuffer());
        }}
        if (!bytes || bytes.length === 0) {{
          return jsonResponse(400, {{ error: "empty body" }});
        }}
        await activeBackend.writeFile(target, bytes);
        return jsonResponse(200, {{ ok: true, bytes: bytes.length }});
      }}

      if (route === "/api/crypto" && method === "POST") {{
        const req = JSON.parse((await parseBodyText()) || "{{}}");
        const op = req.op || "";
        if (op === "random") {{
          const len = Math.max(0, Math.min(4096, Number(req.len ?? 16)));
          const buf = new Uint8Array(len);
          crypto.getRandomValues(buf);
          return jsonResponse(200, {{ data: uint8ToBase64(buf) }});
        }}
        if (op === "sha1" || op === "sha256") {{
          const d = base64ToUint8(req.data || "");
          const digest = await crypto.subtle.digest(op === "sha1" ? "SHA-1" : "SHA-256", d);
          return jsonResponse(200, {{ data: uint8ToBase64(new Uint8Array(digest)) }});
        }}
        if (op === "aesenc" || op === "aesdec") {{
          const k = base64ToUint8(req.key || "");
          const iv = base64ToUint8(req.iv || "");
          const d = base64ToUint8(req.data || "");
          const cryptoKey = await crypto.subtle.importKey("raw", k, {{ name: "AES-CBC" }}, false, [op === "aesenc" ? "encrypt" : "decrypt"]);
          const res = await crypto.subtle[op === "aesenc" ? "encrypt" : "decrypt"]({{ name: "AES-CBC", iv }}, cryptoKey, d);
          return jsonResponse(200, {{ data: uint8ToBase64(new Uint8Array(res)) }});
        }}
        return jsonResponse(200, {{ error: "unsupported crypto op: " + op }});
      }}

      if (route === "/api/book-key" && method === "POST") {{
        const req = JSON.parse((await parseBodyText()) || "{{}}");
        const bookPath = normalizeVPath(req.path || "");
        const keyBytes = base64ToUint8(req.key || "");
        const expires = BigInt(req.expires || 0);
        if (!bookPath || (keyBytes.length !== 16 && keyBytes.length !== 32)) {{
          return jsonResponse(400, {{ error: "bad path/key" }});
        }}
        const blob = new Uint8Array(1 + 8 + keyBytes.length);
        blob[0] = keyBytes.length;
        const dv = new DataView(blob.buffer);
        dv.setBigInt64(1, expires, true);
        blob.set(keyBytes, 9);
        await activeBackend.writeFile(bookPath + ".key", blob);
        return jsonResponse(200, {{ ok: true }});
      }}

      if (route === "/api/settings" && method === "GET") {{
        const keysParam = qs.get("keys") || "";
        const filter = keysParam ? new Set(keysParam.split(",").map(s => s.trim()).filter(Boolean)) : null;
        return jsonResponse(200, await buildSettingsArray(filter));
      }}

      if (route === "/api/settings" && method === "POST") {{
        const updates = JSON.parse((await parseBodyText()) || "{{}}");
        await applySettingsUpdates(updates);
        return textResponse(200, "OK");
      }}

      if (route === "/api/files" && method === "GET") {{
        const vpath = normalizeVPath(qs.get("path") || "/");
        const entries = await activeBackend.listDir(vpath);
        const stored = await loadJsonFile("/.crosspoint/settings.json", {{}});
        const showHidden = !!stored.showHiddenFiles;
        const inPluginDir = vpath.startsWith("/.crosspoint/plugins") || vpath.startsWith("/.plugins");
        const out = [];
        for (const e of entries) {{
          if (!showHidden && !inPluginDir && e.name.startsWith(".")) continue;
          out.push({{
            name: e.name,
            isDirectory: e.isDir,
            size: e.size,
            isEpub: !e.isDir && /\\.(epub|xtc|xtch|txt|md)$/i.test(e.name)
          }});
        }}
        return jsonResponse(200, out);
      }}

      if (route === "/download" && method === "GET") {{
        const vpath = normalizeVPath(qs.get("path") || "");
        const itemName = vpath.split("/").pop() || "";
        if (!vpath || vpath === "/" || itemName.startsWith(".")) {{
          return textResponse(403, "Cannot access system files");
        }}
        if (!(await activeBackend.exists(vpath))) {{
          return textResponse(404, "Item not found");
        }}
        const bytes = await activeBackend.readFile(vpath);
        return new Response(bytes, {{
          status: 200,
          headers: {{ "Content-Type": "application/octet-stream" }}
        }});
      }}

      if (route === "/upload" && method === "POST") {{
        const destDir = normalizeVPath(qs.get("path") || "/");
        if (init && init.body instanceof FormData) {{
          let lastFn = "";
          for (const [, val] of init.body.entries()) {{
            if (val && typeof val.arrayBuffer === "function") {{
              const fn = val.name || "uploaded.bin";
              const bytes = new Uint8Array(await val.arrayBuffer());
              await activeBackend.writeFile(normalizeVPath(`${{destDir}}/${{fn}}`), bytes);
              lastFn = fn;
            }}
          }}
          return textResponse(200, "File uploaded successfully: " + lastFn);
        }}
        return textResponse(400, "No file uploaded");
      }}

      if (route === "/mkdir" && method === "POST") {{
        const bodyText = await parseBodyText();
        const params = new URLSearchParams(bodyText);
        const name = params.get("name") || "";
        const pathArg = params.get("path") || "/";
        const target = name ? normalizeVPath(`${{pathArg}}/${{name}}`) : normalizeVPath(pathArg);
        await activeBackend.mkdir(target);
        return textResponse(200, "Folder created: " + target);
      }}

      if (route === "/rename" && method === "POST") {{
        const params = new URLSearchParams(await parseBodyText());
        const oldPath = normalizeVPath(params.get("path") || "");
        const newName = (params.get("name") || "").trim();
        const parent = oldPath.slice(0, oldPath.lastIndexOf("/")) || "/";
        const ok = await activeBackend.rename(oldPath, normalizeVPath(`${{parent}}/${{newName}}`));
        return textResponse(ok ? 200 : 404, ok ? "Renamed" : "Failed");
      }}

      if (route === "/delete" && method === "POST") {{
        const bodyText = await parseBodyText();
        let paths = [];
        try {{
          const j = JSON.parse(bodyText);
          paths = Array.isArray(j.paths) ? j.paths : j.path ? [j.path] : [];
        }} catch {{
          const params = new URLSearchParams(bodyText);
          if (params.get("paths")) {{
            try {{ paths = JSON.parse(params.get("paths")); }} catch {{ paths = [params.get("paths")]; }}
          }} else if (params.get("path")) {{
            paths = [params.get("path")];
          }}
        }}
        for (const p of paths) {{
          await activeBackend.remove(normalizeVPath(p));
        }}
        return textResponse(200, "All items deleted successfully");
      }}

      if (route === "/api/opds" && method === "GET") {{
        const doc = await loadJsonFile("/.crosspoint/opds.json", {{ servers: [] }});
        const servers = (doc.servers || []).map((s, idx) => ({{
          index: idx,
          name: s.name || "",
          url: s.url || "",
          username: s.username || "",
          hasPassword: !!(s.password_obf || s.password)
        }}));
        return jsonResponse(200, servers);
      }}

      if (route === "/api/opds" && method === "POST") {{
        const doc = await loadJsonFile("/.crosspoint/opds.json", {{ servers: [] }});
        const servers = doc.servers || [];
        const body = JSON.parse((await parseBodyText()) || "{{}}");
        const entry = {{
          name: body.name || "",
          url: body.url || "",
          username: body.username || "",
          password_obf: obfuscateStr(body.password || "")
        }};
        if (typeof body.index === "number" && body.index >= 0 && body.index < servers.length) {{
          if (!("password" in body) && servers[body.index].password_obf) {{
            entry.password_obf = servers[body.index].password_obf;
          }}
          servers[body.index] = entry;
        }} else {{
          servers.push(entry);
        }}
        doc.servers = servers;
        await saveJsonFile("/.crosspoint/opds.json", doc);
        return textResponse(200, "OK");
      }}

      if (route === "/api/opds/delete" && method === "POST") {{
        const doc = await loadJsonFile("/.crosspoint/opds.json", {{ servers: [] }});
        const body = JSON.parse((await parseBodyText()) || "{{}}");
        if (typeof body.index === "number" && body.index >= 0 && body.index < (doc.servers || []).length) {{
          doc.servers.splice(body.index, 1);
          await saveJsonFile("/.crosspoint/opds.json", doc);
        }}
        return textResponse(200, "OK");
      }}

      if (route === "/api/fonts" && method === "GET") {{
        const entries = await activeBackend.listDir("/.crosspoint/fonts");
        const families = [];
        for (const e of entries) {{
          if (!e.isDir) continue;
          const sub = await activeBackend.listDir(`/.crosspoint/fonts/${{e.name}}`);
          const files = sub.filter(s => !s.isDir && /\\.(cpfont|epdfont|ttf|otf)$/i.test(s.name))
                           .map(s => ({{ name: s.name, size: s.size }}));
          if (files.length) families.push({{ name: e.name, sizes: [], files }});
        }}
        return jsonResponse(200, {{ vectorFonts: true, families }});
      }}

      if (route === "/api/fonts/upload" && method === "POST") {{
        if (init && init.body instanceof FormData) {{
          const family = String(init.body.get("family") || "CustomFont").replace(/[^A-Za-z0-9_-]/g, "_");
          const file = init.body.get("file");
          if (file && typeof file.arrayBuffer === "function") {{
            const bytes = new Uint8Array(await file.arrayBuffer());
            await activeBackend.writeFile(`/.crosspoint/fonts/${{family}}/${{file.name}}`, bytes);
            return jsonResponse(200, {{ ok: true }});
          }}
        }}
        return jsonResponse(400, {{ ok: false, error: "Invalid font upload" }});
      }}

      if (route === "/api/fonts/delete" && method === "POST") {{
        const body = JSON.parse((await parseBodyText()) || "{{}}");
        const fam = String(body.family || "").replace(/[^A-Za-z0-9_-]/g, "");
        if (fam) await activeBackend.remove(`/.crosspoint/fonts/${{fam}}`);
        return jsonResponse(200, {{ ok: true }});
      }}

      if (route.startsWith("/api/plugin-jobs")) {{
        return jsonResponse(200, {{}});
      }}

      return jsonResponse(404, {{ error: "Unknown virtual route: " + route }});
    }}

    // Intercept window.fetch for virtual CrossPoint endpoints
    window.fetch = async function(input, init) {{
      const rawUrl = typeof input === "string" ? input : (input && input.url) || "";
      let isLocalRoute = false;
      let urlObj = null;
      try {{
        if (rawUrl.startsWith("/")) {{
          isLocalRoute = true;
          urlObj = new URL(rawUrl, "http://crosspoint.local");
        }} else {{
          const parsed = new URL(rawUrl, window.location.href);
          if (parsed.origin === window.location.origin && (
              parsed.pathname.startsWith("/api/") ||
              ["/plugin", "/download", "/upload", "/mkdir", "/rename", "/move", "/delete"].includes(parsed.pathname)
          )) {{
            isLocalRoute = true;
            urlObj = parsed;
          }}
        }}
      }} catch {{}}

      if (isLocalRoute && urlObj) {{
        if (activeBackend) {{
          const method = ((init && init.method) || (typeof input === "object" && input.method) || "GET").toUpperCase();
          return await handleVirtualRequest(urlObj, method, init);
        }}
        if (useLocalPythonBackend) {{
          return await nativeFetch(input, init);
        }}
        return jsonResponse(503, {{ error: "Playdate not connected yet" }});
      }}
      return await nativeFetch(input, init);
    }};
  </script>

  <!-- Official CrossPoint Plugin Host (`plugin-host.js`) -->
  <script>
{plugin_host_js}
  </script>

  <!-- Hub UI Controllers (Connection, Plugins, Files, Settings, Fonts) -->
  <script>
    let currentTab = "plugins";
    let currentFilesPath = "/";
    let allSettings = [];

    function isConnected() {{
      return !!activeBackend || useLocalPythonBackend;
    }}

    async function connectWebSerial(anyPort = false) {{
      if (!("serial" in navigator)) {{
        await showWebAlert(
          "Web Serial API is not available in this browser. Please open this page in Google Chrome, Microsoft Edge, Brave, or Opera."
        );
        return;
      }}
      try {{
        setConnectionStatus("busy", "Connecting over USB Serial…", "Select your Playdate in the browser popup");
        let port;
        try {{
          port = await navigator.serial.requestPort(
            anyPort ? {{}} : {{ filters: [{{ usbVendorId: 0x1331 }}] }}
          );
        }} catch (filterErr) {{
          if (!anyPort) {{
            port = await navigator.serial.requestPort();
          }} else {{
            throw filterErr;
          }}
        }}
        const backend = new WebSerialBackend(port);
        await backend.open();
        activeBackend = backend;
        useLocalPythonBackend = false;
        setConnectionStatus("connected", backend.modeName, backend.targetDescription);
        showWebToast("Connected to Playdate via Live USB Serial!", false);
        await refreshActiveTab();
      }} catch (err) {{
        console.error(err);
        setConnectionStatus("disconnected", "Serial Connection Failed", err.message || String(err));
        showWebToast("Serial connection failed: " + (err.message || err), true);
      }}
    }}

    async function connectDirectoryPicker() {{
      if (!("showDirectoryPicker" in window)) {{
        await showWebAlert(
          "Directory Picker API is not available in this browser. Please use Google Chrome, Microsoft Edge, Brave, or Opera."
        );
        return;
      }}
      try {{
        setConnectionStatus("busy", "Opening Playdate Data Folder…", "Select your PLAYDATE drive or Data folder");
        const picked = await window.showDirectoryPicker({{ mode: "readwrite" }});
        const {{ handle, label }} = await FileSystemAccessBackend.resolveCrossPointDataHandle(picked);
        activeBackend = new FileSystemAccessBackend(handle, label);
        useLocalPythonBackend = false;
        setConnectionStatus("connected", "Playdate Data Folder", label);
        showWebToast("Opened Playdate Data folder: " + label, false);
        await refreshActiveTab();
      }} catch (err) {{
        if (err && err.name === "AbortError") {{
          setConnectionStatus("disconnected", "Not connected to Playdate", "Folder selection cancelled");
          return;
        }}
        setConnectionStatus("disconnected", "Folder Access Failed", err.message || String(err));
        showWebToast("Could not open folder: " + (err.message || err), true);
      }}
    }}

    async function switchTab(tab) {{
      currentTab = tab;
      document.querySelectorAll(".nav-links a[data-tab]").forEach(a => {{
        a.classList.toggle("active-tab", a.dataset.tab === tab);
      }});
      document.querySelectorAll(".hub-section").forEach(sec => {{
        sec.classList.toggle("active", sec.id === "tab-" + tab);
      }});
      await refreshActiveTab();
    }}

    async function refreshActiveTab() {{
      if (!isConnected()) return;
      if (currentTab === "plugins") await reloadPluginsTab();
      else if (currentTab === "files") await reloadFilesTab(currentFilesPath);
      else if (currentTab === "settings") await reloadSettingsTab();
      else if (currentTab === "fonts") await reloadFontsTab();
    }}

    async function reloadPluginsTab() {{
      const host = document.getElementById("plugin-container");
      const empty = document.getElementById("empty-state");
      const emptyMsg = document.getElementById("empty-state-msg");
      host.replaceChildren();
      empty.hidden = true;
      const ok = await loadPlugins("settings");
      if (!ok) {{
        emptyMsg.textContent = "Could not load plugins from Playdate. Click Refresh to try again.";
        empty.hidden = false;
      }} else if (host.children.length === 0) {{
        emptyMsg.innerHTML =
          "No web plugins are installed in <code>/.crosspoint/plugins</code> yet. " +
          "Install plugins from <b>Plugins &rarr; Plugin Hub</b> on your Playdate, then click Refresh.";
        empty.hidden = false;
      }}
    }}

    // ---- File Manager Tab ----
    function formatBytes(bytes) {{
      if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
      if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB";
      return bytes + " B";
    }}

    async function reloadFilesTab(path = "/") {{
      currentFilesPath = normalizeVPath(path);
      const bc = document.getElementById("filesBreadcrumb");
      const container = document.getElementById("filesTableContainer");
      bc.textContent = currentFilesPath;
      try {{
        const res = await fetch("/api/files?path=" + encodeURIComponent(currentFilesPath));
        const items = await res.json();
        const table = document.createElement("table");
        table.className = "file-table";
        table.innerHTML = "<thead><tr><th>Name</th><th>Size</th><th style='text-align:right'>Actions</th></tr></thead>";
        const tbody = document.createElement("tbody");

        if (currentFilesPath !== "/") {{
          const parentPath = currentFilesPath.slice(0, currentFilesPath.lastIndexOf("/")) || "/";
          const tr = document.createElement("tr");
          tr.innerHTML = `<td colspan="3"><a class="folder-link" href="#">&#11013; .. (Parent Folder)</a></td>`;
          tr.querySelector("a").onclick = (e) => {{ e.preventDefault(); reloadFilesTab(parentPath); }};
          tbody.appendChild(tr);
        }}

        const sorted = (items || []).slice().sort((a, b) => (Number(b.isDirectory) - Number(a.isDirectory)) || a.name.localeCompare(b.name));
        for (const item of sorted) {{
          const fullPath = normalizeVPath(`${{currentFilesPath}}/${{item.name}}`);
          const tr = document.createElement("tr");
          const tdName = document.createElement("td");
          const a = document.createElement("a");
          a.className = item.isDirectory ? "folder-link" : "file-link";
          a.textContent = (item.isDirectory ? "📁 " : "📄 ") + item.name;
          a.onclick = (e) => {{
            e.preventDefault();
            if (item.isDirectory) reloadFilesTab(fullPath);
            else downloadPlaydateFile(fullPath, item.name);
          }};
          tdName.appendChild(a);

          const tdSize = document.createElement("td");
          tdSize.textContent = item.isDirectory ? "Folder" : formatBytes(item.size || 0);

          const tdAct = document.createElement("td");
          tdAct.style.textAlign = "right";
          if (!item.isDirectory) {{
            const dlBtn = document.createElement("button");
            dlBtn.className = "btn-small";
            dlBtn.textContent = "Download";
            dlBtn.style.marginRight = "6px";
            dlBtn.onclick = () => downloadPlaydateFile(fullPath, item.name);
            tdAct.appendChild(dlBtn);
          }}
          const delBtn = document.createElement("button");
          delBtn.className = "btn-small btn-delete";
          delBtn.textContent = "Delete";
          delBtn.onclick = async () => {{
            if (!(await showWebConfirm(`Delete "${{item.name}}"?`))) return;
            await fetch("/delete", {{
              method: "POST",
              headers: {{ "Content-Type": "application/x-www-form-urlencoded" }},
              body: "path=" + encodeURIComponent(fullPath)
            }});
            showWebToast(`Deleted ${{item.name}}`, false);
            reloadFilesTab(currentFilesPath);
          }};
          tdAct.appendChild(delBtn);

          tr.append(tdName, tdSize, tdAct);
          tbody.appendChild(tr);
        }}
        table.appendChild(tbody);
        container.replaceChildren(table);
      }} catch (e) {{
        container.innerHTML = `<p class="empty-state">Error loading files: ${{e.message || e}}</p>`;
      }}
    }}

    async function downloadPlaydateFile(fullPath, filename) {{
      try {{
        const res = await fetch("/download?path=" + encodeURIComponent(fullPath));
        if (!res.ok) throw new Error("HTTP " + res.status);
        const blob = await res.blob();
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        a.remove();
      }} catch (e) {{
        showWebToast("Download failed: " + (e.message || e), true);
      }}
    }}

    async function uploadSelectedFiles(fileList) {{
      if (!fileList || fileList.length === 0) return;
      for (const file of Array.from(fileList)) {{
        showWebToast(`Uploading ${{file.name}}…`, false);
        const form = new FormData();
        form.append("file", file, file.name);
        const res = await fetch("/upload?path=" + encodeURIComponent(currentFilesPath) + "&overwrite=1", {{
          method: "POST",
          body: form
        }});
        if (!res.ok) {{
          showWebToast(`Upload failed for ${{file.name}}`, true);
          return;
        }}
      }}
      showWebToast("Upload complete!", false);
      document.getElementById("fileUploadInput").value = "";
      await reloadFilesTab(currentFilesPath);
    }}

    async function promptCreateFolder() {{
      const name = prompt("New folder name:");
      if (!name || !name.trim()) return;
      await fetch("/mkdir", {{
        method: "POST",
        headers: {{ "Content-Type": "application/x-www-form-urlencoded" }},
        body: new URLSearchParams({{ path: currentFilesPath, name: name.trim() }}).toString()
      }});
      await reloadFilesTab(currentFilesPath);
    }}

    // ---- Settings Tab ----
    function escapeHtml(unsafe) {{
      return String(unsafe || "")
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
    }}

    async function reloadSettingsTab() {{
      const container = document.getElementById("settings-container");
      try {{
        const res = await fetch("/api/settings");
        allSettings = await res.json();
        const groups = {{}};
        for (const s of allSettings) {{
          const cat = s.category || "Other";
          (groups[cat] = groups[cat] || []).push(s);
        }}
        let html = "";
        for (const [cat, list] of Object.entries(groups)) {{
          html += `<div class="card"><h2>${{escapeHtml(cat)}}</h2>`;
          for (const s of list) {{
            const id = "setting-" + s.key;
            let ctrl = "";
            if (s.type === "toggle") {{
              ctrl = `<label class="toggle-switch"><input type="checkbox" id="${{id}}" ${{s.value ? "checked" : ""}}><span class="toggle-slider"></span></label>`;
            }} else if (s.type === "enum") {{
              ctrl = `<select id="${{id}}">` + (s.options || []).map((opt, idx) =>
                `<option value="${{idx}}" ${{idx === s.value ? "selected" : ""}}>${{escapeHtml(opt)}}</option>`
              ).join("") + `</select>`;
            }} else if (s.type === "value") {{
              ctrl = `<input type="number" id="${{id}}" value="${{s.value}}" min="${{s.min}}" max="${{s.max}}" step="${{s.step}}">`;
            }} else if (s.type === "string") {{
              const isPw = "hasPassword" in s;
              ctrl = `<input type="${{isPw ? "password" : "text"}}" id="${{id}}" value="${{escapeHtml(s.value || "")}}" placeholder="${{s.hasPassword ? "(unchanged)" : ""}}" oninput="this.dataset.changed='true'">`;
            }}
            html += `<div class="setting-row"><span class="setting-name">${{escapeHtml(s.name)}}</span><span class="setting-control">${{ctrl}}</span></div>`;
          }}
          html += `</div>`;
        }}
        container.innerHTML = html;
        document.getElementById("save-container").style.display = "block";
      }} catch (e) {{
        container.innerHTML = `<div class="card"><p class="empty-state">Failed to load settings: ${{e.message || e}}</p></div>`;
      }}
    }}

    async function saveSettings() {{
      const payload = {{}};
      for (const s of allSettings) {{
        const el = document.getElementById("setting-" + s.key);
        if (!el) continue;
        if (s.type === "toggle") payload[s.key] = el.checked ? 1 : 0;
        else if (s.type === "enum" || s.type === "value") payload[s.key] = parseInt(el.value, 10);
        else if (s.type === "string") {{
          if ("hasPassword" in s && !el.dataset.changed) continue;
          payload[s.key] = el.value;
        }}
      }}
      const res = await fetch("/api/settings", {{
        method: "POST",
        headers: {{ "Content-Type": "application/json" }},
        body: JSON.stringify(payload)
      }});
      if (res.ok) {{
        showWebToast("Settings saved to Playdate!", false);
        await reloadSettingsTab();
      }} else {{
        showWebToast("Failed to save settings", true);
      }}
    }}

    // ---- Fonts Tab ----
    async function reloadFontsTab() {{
      const el = document.getElementById("families");
      try {{
        const res = await fetch("/api/fonts");
        const data = await res.json();
        el.replaceChildren();
        if (!data.families || data.families.length === 0) {{
          el.innerHTML = `<p class="empty-state">No custom fonts installed on Playdate.</p>`;
          return;
        }}
        for (const f of data.families) {{
          const row = document.createElement("div");
          row.className = "setting-row";
          const filesDesc = (f.files || []).map(fi => `${{fi.name}} (${{formatBytes(fi.size || 0)}})`).join(", ");
          row.innerHTML = `<span class="setting-name"><b>${{escapeHtml(f.name)}}</b><br><small style="color:var(--label-color)">${{escapeHtml(filesDesc)}}</small></span>`;
          const btn = document.createElement("button");
          btn.className = "btn-small btn-delete";
          btn.textContent = "Delete";
          btn.onclick = async () => {{
            if (!(await showWebConfirm(`Delete font family "${{f.name}}"?`))) return;
            await fetch("/api/fonts/delete", {{
              method: "POST",
              headers: {{ "Content-Type": "application/json" }},
              body: JSON.stringify({{ family: f.name }})
            }});
            showWebToast(`Deleted font ${{f.name}}`, false);
            reloadFontsTab();
          }};
          row.appendChild(btn);
          el.appendChild(row);
        }}
      }} catch (e) {{
        el.innerHTML = `<p class="empty-state">Failed to load fonts.</p>`;
      }}
    }}

    async function uploadSelectedFonts() {{
      const input = document.getElementById("fontFilesInput");
      const files = Array.from(input.files || []).filter(f => /\\.(cpfont|ttf|otf)$/i.test(f.name));
      if (files.length === 0) {{
        showWebToast("Please select .cpfont, .ttf, or .otf font files.", true);
        return;
      }}
      const stem = files[0].name.replace(/\\.(cpfont|ttf|otf)$/i, "");
      const cut = Math.max(stem.lastIndexOf("-"), stem.lastIndexOf("_"));
      const family = (cut > 0 ? stem.slice(0, cut) : stem).replace(/[^A-Za-z0-9_-]/g, "_");
      for (const file of files) {{
        showWebToast(`Uploading ${{file.name}} to family "${{family}}"…`, false);
        const fd = new FormData();
        fd.append("family", family);
        fd.append("file", file, file.name);
        await fetch("/api/fonts/upload", {{ method: "POST", body: fd }});
      }}
      input.value = "";
      showWebToast(`Uploaded ${{files.length}} font file(s) to "${{family}}"!`, false);
      await reloadFontsTab();
    }}

    // Auto-detect if served by local `web_host.py`
    (async () => {{
      if (window.location.protocol.startsWith("http")) {{
        try {{
          const res = await nativeFetch("/api/status");
          if (res.ok) {{
            const info = await res.json();
            if (info && info.firmwareFamily === "crosspoint") {{
              useLocalPythonBackend = true;
              setConnectionStatus(
                "connected",
                info.storageMode || "Local Companion Server",
                info.storageTarget || window.location.origin
              );
              await refreshActiveTab();
              return;
            }}
          }}
        }} catch {{}}
      }}
      document.getElementById("empty-state").hidden = false;
    }})();
  </script>
</body>
</html>
"""
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"Generated {OUT_PATH} ({len(html)} bytes)")


if __name__ == "__main__":
    main()
