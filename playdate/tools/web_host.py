#!/usr/bin/env python3
"""
CrossPoint Playdate Web Host (`web_host.py`)
============================================
Serves the full CrossPoint Web UI (`http://localhost:8080/plugins`, `/files`,
`/settings`, `/fonts`, `/plugins-run`, and `/hub` for the Web Serial / File
System Access standalone hub) on your computer and bridges all `/api/*`
requests directly to your Playdate.

Supports three storage modes (auto-detected on startup):
  1. Live USB Serial Mode (`--serial /dev/cu.usbmodemPD...`)
     Talks directly to CrossPoint while it is running on your Playdate over USB
     Serial (`!CP` / `@CP` protocol).
  2. Playdate Data Disk Mode (`--data-dir /Volumes/PLAYDATE/Data/...`)
     Reads/writes directly to your Playdate's mounted Data Disk when put into
     USB Data Disk mode, supporting both sideloaded (`user.<id>.com.crosspoint.reader`)
     and dev (`com.crosspoint.reader`) data folders.
  3. Playdate Simulator Mode (`--data-dir playdate/SimulatorDisk/Data/com.crosspoint.reader`)
     Reads/writes directly to the local Playdate Simulator disk directory.
"""

import argparse
import base64
import glob
import hashlib
import hmac
import json
import mimetypes
import os
import platform
import re
import select
import shutil
import ssl
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Playdate hardware MAC stub used by CrossPoint's ObfuscationUtils on Playdate
# (`playdate/include/esp_mac.h`: {0x02, 0x00, 0x50, 0x44, 0x00, 0x01})
PLAYDATE_MAC_KEY = bytes([0x02, 0x00, 0x50, 0x44, 0x00, 0x01])

# Default Playdate 32-byte device secret (`HalPlatformPlaydate.cpp`)
DEFAULT_DEVICE_SECRET = bytes([(0x50 + i * 17) & 0xFF for i in range(32)])

FIRMWARE_FAMILY = "crosspoint"
PLUGIN_API_VERSION = 1
PLUGIN_CAPABILITIES = (
    "catalog.lists,catalog.notices,catalog.bundle_install,catalog.version_tracking,"
    "catalog.compatibility,catalog.item_details,catalog.encoded_item_fields,"
    "catalog.list_index_mapping,catalog.list_requirements,catalog.server_paths"
)
PLUGIN_ROOTS = ["/.crosspoint/plugins", "/plugins", "/.plugins"]


def obfuscate_str(plaintext: str) -> str:
    if not plaintext:
        return ""
    raw = plaintext.encode("utf-8")
    xored = bytes(b ^ PLAYDATE_MAC_KEY[i % len(PLAYDATE_MAC_KEY)] for i, b in enumerate(raw))
    return base64.b64encode(xored).decode("ascii")


def deobfuscate_str(encoded: str) -> str:
    if not encoded:
        return ""
    try:
        xored = base64.b64decode(encoded)
        raw = bytes(b ^ PLAYDATE_MAC_KEY[i % len(PLAYDATE_MAC_KEY)] for i, b in enumerate(xored))
        return raw.decode("utf-8", errors="replace")
    except Exception:
        return ""


def normalize_vpath(path: str) -> str:
    if not path:
        return "/"
    path = path.replace("\\", "/")
    parts = []
    for p in path.split("/"):
        if not p or p == ".":
            continue
        if p == "..":
            if parts:
                parts.pop()
        else:
            parts.append(p)
    return "/" + "/".join(parts)


class StorageBackend:
    mode_name = "unknown"
    target_description = ""

    def stat(self, vpath: str) -> dict:
        raise NotImplementedError

    def list_dir(self, vpath: str) -> list:
        raise NotImplementedError

    def read_file(self, vpath: str) -> bytes:
        raise NotImplementedError

    def write_file(self, vpath: str, data: bytes) -> None:
        raise NotImplementedError

    def mkdir(self, vpath: str) -> None:
        raise NotImplementedError

    def remove(self, vpath: str) -> bool:
        raise NotImplementedError

    def rename(self, old_vpath: str, new_vpath: str) -> bool:
        raise NotImplementedError

    def exists(self, vpath: str) -> bool:
        return self.stat(vpath).get("exists", False)

    def is_dir(self, vpath: str) -> bool:
        st = self.stat(vpath)
        return st.get("exists", False) and st.get("is_dir", False)


class DiskStorageBackend(StorageBackend):
    def __init__(self, root_dir: str, mode_name: str = "Data Disk"):
        self.root_dir = os.path.abspath(root_dir)
        self.mode_name = mode_name
        self.target_description = self.root_dir
        os.makedirs(self.root_dir, exist_ok=True)
        os.makedirs(os.path.join(self.root_dir, ".crosspoint", "plugins"), exist_ok=True)

    def _host_path(self, vpath: str) -> str:
        norm = normalize_vpath(vpath).lstrip("/")
        full = os.path.abspath(os.path.join(self.root_dir, norm))
        if not full.startswith(self.root_dir):
            raise PermissionError("Path traversal outside root directory")
        return full

    def stat(self, vpath: str) -> dict:
        hp = self._host_path(vpath)
        if not os.path.exists(hp):
            return {"exists": False, "is_dir": False, "size": 0}
        is_d = os.path.isdir(hp)
        sz = 0 if is_d else os.path.getsize(hp)
        return {"exists": True, "is_dir": is_d, "size": sz}

    def list_dir(self, vpath: str) -> list:
        hp = self._host_path(vpath)
        if not os.path.isdir(hp):
            return []
        items = []
        for entry in sorted(os.scandir(hp), key=lambda e: e.name.lower()):
            if entry.name in (".", ".."):
                continue
            is_d = entry.is_dir()
            sz = 0 if is_d else entry.stat().st_size
            items.append({"name": entry.name, "is_dir": is_d, "size": sz})
        return items

    def read_file(self, vpath: str) -> bytes:
        hp = self._host_path(vpath)
        with open(hp, "rb") as f:
            return f.read()

    def write_file(self, vpath: str, data: bytes) -> None:
        hp = self._host_path(vpath)
        os.makedirs(os.path.dirname(hp), exist_ok=True)
        tmp_path = hp + ".tmp"
        with open(tmp_path, "wb") as f:
            f.write(data)
        os.replace(tmp_path, hp)

    def mkdir(self, vpath: str) -> None:
        hp = self._host_path(vpath)
        os.makedirs(hp, exist_ok=True)

    def remove(self, vpath: str) -> bool:
        hp = self._host_path(vpath)
        if not os.path.exists(hp):
            return False
        if os.path.isdir(hp):
            shutil.rmtree(hp)
        else:
            os.remove(hp)
        return True

    def rename(self, old_vpath: str, new_vpath: str) -> bool:
        old_hp = self._host_path(old_vpath)
        new_hp = self._host_path(new_vpath)
        if not os.path.exists(old_hp):
            return False
        os.makedirs(os.path.dirname(new_hp), exist_ok=True)
        os.replace(old_hp, new_hp)
        return True


def termios_flag(mod, name: str) -> int:
    return getattr(mod, name, 0)


class SerialStorageBackend(StorageBackend):
    def __init__(self, port_path: str):
        self.port_path = port_path
        self.mode_name = "Live USB Serial"
        self.target_description = port_path
        self.lock = threading.Lock()
        self._fd = None
        self._pyserial = None
        self._rx_buf = b""
        self._open_port()

    def _open_port(self):
        if os.name == "nt":
            import serial  # type: ignore
            self._pyserial = serial.Serial(self.port_path, 115200, timeout=0.1)
            self._pyserial.write(b"\r\nmsg !CP PING\r\n")
            self._pyserial.flush()
        else:
            import termios
            fd = os.open(self.port_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            attrs = termios.tcgetattr(fd)
            attrs[0] &= ~(
                termios.IGNBRK
                | termios.BRKINT
                | termios_flag(termios, "PARMRK")
                | termios.ISTRIP
                | termios.INLCR
                | termios.IGNCR
                | termios.ICRNL
                | termios.IXON
            )
            attrs[1] &= ~termios.OPOST
            attrs[2] &= ~(termios.CSIZE | termios.PARENB)
            attrs[2] |= termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
            if hasattr(termios, "B115200"):
                attrs[4] = termios.B115200
                attrs[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            termios.tcflush(fd, termios.TCIOFLUSH)
            self._fd = fd

    def _write_raw(self, data: bytes):
        if self._pyserial is not None:
            self._pyserial.write(data)
            self._pyserial.flush()
            return
        view = memoryview(data)
        while len(view) > 0:
            _, wlist, _ = select.select([], [self._fd], [], 2.0)
            if not wlist:
                raise TimeoutError("Serial write timed out")
            n = os.write(self._fd, view)
            view = view[n:]

    def _read_line(self, timeout: float = 5.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            nl = self._rx_buf.find(b"\n")
            if nl != -1:
                line = self._rx_buf[:nl].decode("utf-8", errors="replace").strip("\r")
                self._rx_buf = self._rx_buf[nl + 1 :]
                return line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for response from Playdate over USB Serial")
            if self._pyserial is not None:
                chunk = self._pyserial.read(4096)
                if chunk:
                    self._rx_buf += chunk
            else:
                rlist, _, _ = select.select([self._fd], [], [], min(0.1, remaining))
                if rlist:
                    try:
                        chunk = os.read(self._fd, 4096)
                        if chunk:
                            self._rx_buf += chunk
                    except BlockingIOError:
                        pass

    def _cmd(self, cp_args: str, timeout: float = 5.0) -> tuple:
        with self.lock:
            if self._fd is not None:
                while True:
                    r, _, _ = select.select([self._fd], [], [], 0.0)
                    if not r:
                        break
                    try:
                        if not os.read(self._fd, 4096):
                            break
                    except Exception:
                        break
            self._rx_buf = b""
            payload = f"msg !CP {cp_args}\n".encode("utf-8")
            self._write_raw(payload)

            items = []
            data_chunks = []
            while True:
                line = self._read_line(timeout=timeout)
                idx = line.find("@CP ")
                if idx == -1:
                    continue
                resp = line[idx + 4 :]
                if resp.startswith("ITEM "):
                    items.append(resp[5:])
                elif resp.startswith("DATA "):
                    data_chunks.append(resp[5:])
                elif resp.startswith("OK"):
                    rest = resp[2:].strip()
                    return True, rest, items, data_chunks
                elif resp.startswith("ERR"):
                    rest = resp[3:].strip()
                    return False, rest, items, data_chunks

    def ping(self) -> bool:
        try:
            ok, rest, _, _ = self._cmd("PING", timeout=2.0)
            if not ok:
                return False
            doc = json.loads(rest)
            return bool(doc.get("ok"))
        except Exception:
            return False

    def stat(self, vpath: str) -> dict:
        vpath = normalize_vpath(vpath)
        ok, rest, _, _ = self._cmd(f"STAT {vpath}")
        if not ok:
            return {"exists": False, "is_dir": False, "size": 0}
        try:
            doc = json.loads(rest)
            return {
                "exists": bool(doc.get("exists", False)),
                "is_dir": bool(doc.get("isDir", False)),
                "size": int(doc.get("size", 0)),
            }
        except Exception:
            return {"exists": False, "is_dir": False, "size": 0}

    def list_dir(self, vpath: str) -> list:
        vpath = normalize_vpath(vpath)
        ok, _, items, _ = self._cmd(f"LIST {vpath}")
        if not ok:
            return []
        result = []
        for item in items:
            try:
                doc = json.loads(item)
                result.append({
                    "name": str(doc.get("name", "")),
                    "is_dir": bool(doc.get("isDir", False)),
                    "size": int(doc.get("size", 0)),
                })
            except Exception:
                pass
        return sorted(result, key=lambda x: x["name"].lower())

    def read_file(self, vpath: str) -> bytes:
        vpath = normalize_vpath(vpath)
        st = self.stat(vpath)
        if not st.get("exists") or st.get("is_dir"):
            raise FileNotFoundError(vpath)
        total_size = st.get("size", 0)
        out = bytearray()
        offset = 0
        chunk_size = 12288
        while offset < total_size or (total_size == 0 and offset == 0):
            ok, rest, _, data_chunks = self._cmd(f"READ {offset} {chunk_size} {vpath}", timeout=10.0)
            if not ok:
                raise IOError(f"Serial READ failed for {vpath}: {rest}")
            chunk_b64 = "".join(data_chunks)
            chunk = base64.b64decode(chunk_b64) if chunk_b64 else b""
            out.extend(chunk)
            if len(chunk) < chunk_size or total_size == 0:
                break
            offset += len(chunk)
        return bytes(out)

    def write_file(self, vpath: str, data: bytes) -> None:
        vpath = normalize_vpath(vpath)
        ok, rest, _, _ = self._cmd(f"WOPEN {vpath}")
        if not ok:
            raise IOError(f"Serial WOPEN failed for {vpath}: {rest}")
        step = 120
        since_flush = 0
        for i in range(0, len(data), step):
            b64 = base64.b64encode(data[i : i + step]).decode("ascii")
            with self.lock:
                self._write_raw(f"msg !CP WDATA {b64}\n".encode("utf-8"))
            since_flush += step
            if since_flush >= 3600:
                ok, rest, _, _ = self._cmd("WFLUSH", timeout=5.0)
                if not ok:
                    raise IOError(f"Serial WFLUSH failed for {vpath}: {rest}")
                since_flush = 0
        ok, rest, _, _ = self._cmd("WCLOSE", timeout=5.0)
        if not ok:
            raise IOError(f"Serial WCLOSE failed for {vpath}: {rest}")

    def mkdir(self, vpath: str) -> None:
        vpath = normalize_vpath(vpath)
        self._cmd(f"MKDIR {vpath}")

    def remove(self, vpath: str) -> bool:
        vpath = normalize_vpath(vpath)
        ok, _, _, _ = self._cmd(f"RM {vpath}")
        return ok

    def rename(self, old_vpath: str, new_vpath: str) -> bool:
        old_vpath = normalize_vpath(old_vpath)
        new_vpath = normalize_vpath(new_vpath)
        ok, _, _, _ = self._cmd(f"RENAME {old_vpath}\t{new_vpath}")
        return ok


def find_playdate_data_dir(repo_root: str) -> tuple:
    candidates = []
    sys_name = platform.system()
    if sys_name == "Darwin":
        candidates.append("/Volumes/PLAYDATE/Data")
    elif sys_name == "Linux":
        user = os.environ.get("USER", "")
        candidates.extend([
            f"/media/{user}/PLAYDATE/Data",
            f"/run/media/{user}/PLAYDATE/Data",
            "/mnt/PLAYDATE/Data",
        ])
        candidates.extend(glob.glob("/media/*/PLAYDATE/Data"))
        candidates.extend(glob.glob("/run/media/*/PLAYDATE/Data"))
    elif sys_name == "Windows":
        for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
            p = f"{letter}:\\Data"
            if os.path.isdir(p) and (
                os.path.isdir(f"{letter}:\\Games") or os.path.isdir(f"{letter}:\\System")
            ):
                candidates.append(p)

    for data_root in candidates:
        if os.path.isdir(data_root):
            matches = glob.glob(os.path.join(data_root, "*com.crosspoint.reader*"))
            if matches:
                matches.sort(
                    key=lambda m: (
                        os.path.isdir(os.path.join(m, ".crosspoint")),
                        os.path.basename(m).startswith("user."),
                    ),
                    reverse=True,
                )
                return matches[0], "Playdate Data Disk"
            return os.path.join(data_root, "com.crosspoint.reader"), "Playdate Data Disk"

    sim_dir = os.path.join(repo_root, "playdate", "SimulatorDisk", "Data", "com.crosspoint.reader")
    if os.path.isdir(os.path.join(repo_root, "playdate", "SimulatorDisk")):
        return sim_dir, "Playdate Simulator Disk"

    sdk_path = os.environ.get("PLAYDATE_SDK_PATH", "")
    if sdk_path and os.path.isdir(os.path.join(sdk_path, "Disk")):
        return os.path.join(sdk_path, "Disk", "Data", "com.crosspoint.reader"), "Playdate SDK Disk"

    return None, None


def find_playdate_serial_port() -> str:
    patterns = [
        "/dev/cu.usbmodemPD*",
        "/dev/tty.usbmodemPD*",
        "/dev/serial/by-id/*Playdate*",
        "/dev/serial/by-id/*Panic*",
        "/dev/ttyACM*",
    ]
    for pat in patterns:
        matches = sorted(glob.glob(pat))
        if matches:
            return matches[0]
    return ""


def get_device_secret(storage: StorageBackend) -> bytes:
    try:
        if storage.exists("/.crosspoint/device_secret.bin"):
            raw = storage.read_file("/.crosspoint/device_secret.bin")
            if len(raw) == 32:
                return raw
    except Exception:
        pass
    return DEFAULT_DEVICE_SECRET


def get_plugin_device_id(storage: StorageBackend, plugin_name: str) -> str:
    """Computes sha256(secret || plugin.lower()) as 64 hex chars (`CrossPointWebServer.cpp`)."""
    secret = get_device_secret(storage)
    p_bytes = plugin_name.strip().lower().encode("utf-8")[:64]
    return hashlib.sha256(secret + p_bytes).hexdigest()


def load_json_file(storage: StorageBackend, vpath: str, default=None):
    try:
        if storage.exists(vpath):
            raw = storage.read_file(vpath)
            return json.loads(raw.decode("utf-8"))
    except Exception:
        pass
    return {} if default is None else default


def save_json_file(storage: StorageBackend, vpath: str, obj) -> None:
    raw = json.dumps(obj, indent=2).encode("utf-8")
    storage.write_file(vpath, raw)


def scan_installed_plugins(storage: StorageBackend) -> list:
    """Matches `PluginLocations::scanPlugins()` and `handlePluginList()` in `CrossPointWebServer.cpp`."""
    seen = set()
    out = []
    for root in PLUGIN_ROOTS:
        for entry in storage.list_dir(root):
            if not entry["is_dir"]:
                continue
            name = entry["name"]
            key = name.lower()
            if key in seen:
                continue
            pdir = f"{root}/{name}"
            has_plugin_js = storage.exists(f"{pdir}/plugin.js")
            if not has_plugin_js:
                continue
            seen.add(key)
            item = {
                "name": name,
                "dir": pdir,
                "title": name,
                "mount": "settings",
            }
            if storage.exists(f"{pdir}/manifest.json"):
                try:
                    m = json.loads(storage.read_file(f"{pdir}/manifest.json").decode("utf-8"))
                    if isinstance(m.get("title"), str) and m["title"]:
                        item["title"] = m["title"]
                    if isinstance(m.get("mount"), str) and m["mount"]:
                        item["mount"] = m["mount"]
                except Exception:
                    pass
            out.append(item)
    return out


def find_plugin_dir(storage: StorageBackend, name: str) -> str:
    target = name.lower()
    for root in PLUGIN_ROOTS:
        for entry in storage.list_dir(root):
            if entry["is_dir"] and entry["name"].lower() == target:
                return f"{root}/{entry['name']}"
    return ""


def build_settings_array(storage: StorageBackend, keys_filter: set = None) -> list:
    """Matches `CrossPointWebServer::handleGetSettings()` JSON array schema."""
    stored = load_json_file(storage, "/.crosspoint/settings.json", default={})
    kr = load_json_file(storage, "/.crosspoint/koreader.json", default={})
    kr_headers = kr.get("headers") if isinstance(kr.get("headers"), list) else []
    while len(kr_headers) < 2:
        kr_headers.append({"name": "", "value_obf": ""})

    kr_pass = deobfuscate_str(kr.get("password_obf", "")) or kr.get("password", "")
    h1_val = deobfuscate_str(kr_headers[0].get("value_obf", ""))
    h2_val = deobfuscate_str(kr_headers[1].get("value_obf", ""))

    items = [
        # KOReader Sync
        {
            "key": "koUsername",
            "name": "KOReader Username",
            "category": "KOReader Sync",
            "type": "string",
            "value": kr.get("username", ""),
        },
        {
            "key": "koPassword",
            "name": "KOReader Password",
            "category": "KOReader Sync",
            "type": "string",
            "value": None,
            "hasPassword": bool(kr_pass),
        },
        {
            "key": "koServerUrl",
            "name": "Sync Server URL",
            "category": "KOReader Sync",
            "type": "string",
            "value": kr.get("serverUrl", kr.get("server_url", "")),
        },
        {
            "key": "koHeader1Name",
            "name": "Custom Header 1 Name",
            "category": "KOReader Sync",
            "type": "string",
            "value": kr_headers[0].get("name", ""),
        },
        {
            "key": "koHeader1Value",
            "name": "Custom Header 1 Value",
            "category": "KOReader Sync",
            "type": "string",
            "value": None,
            "hasPassword": bool(h1_val),
        },
        {
            "key": "koHeader2Name",
            "name": "Custom Header 2 Name",
            "category": "KOReader Sync",
            "type": "string",
            "value": kr_headers[1].get("name", ""),
        },
        {
            "key": "koHeader2Value",
            "name": "Custom Header 2 Value",
            "category": "KOReader Sync",
            "type": "string",
            "value": None,
            "hasPassword": bool(h2_val),
        },
        {
            "key": "koServerType",
            "name": "Server Type",
            "category": "KOReader Sync",
            "type": "enum",
            "value": int(kr.get("serverType", 0)),
            "options": ["CrossPoint", "KOReader Sync Server", "Other"],
        },
        {
            "key": "koMatchMethod",
            "name": "Document Matching",
            "category": "KOReader Sync",
            "type": "enum",
            "value": int(kr.get("matchMethod", kr.get("match_method", 0))),
            "options": ["Filename", "Binary"],
        },
        {
            "key": "koSendMetadata",
            "name": "Send Metadata",
            "category": "KOReader Sync",
            "type": "toggle",
            "value": 1 if kr.get("sendMetadata", kr.get("send_metadata", False)) else 0,
        },
        {
            "key": "koSyncBehavior",
            "name": "Sync Behavior",
            "category": "KOReader Sync",
            "type": "enum",
            "value": int(kr.get("syncBehavior", kr.get("sync_behavior", 0))),
            "options": ["Ask Every Time", "Smart Sync"],
        },
        {
            "key": "koSyncClippings",
            "name": "Sync Clippings",
            "category": "KOReader Sync",
            "type": "toggle",
            "value": 1 if kr.get("syncClippings", False) else 0,
        },
        # Reader & System settings
        {
            "key": "extraParagraphSpacing",
            "name": "Paragraph Spacing",
            "category": "Reader",
            "type": "toggle",
            "value": int(stored.get("extraParagraphSpacing", 1)),
        },
        {
            "key": "hyphenationEnabled",
            "name": "Hyphenation",
            "category": "Reader",
            "type": "toggle",
            "value": int(stored.get("hyphenationEnabled", 1)),
        },
        {
            "key": "screenMargin",
            "name": "Screen Margin",
            "category": "Reader",
            "type": "value",
            "value": int(stored.get("screenMargin", 5)),
            "min": 0,
            "max": 40,
            "step": 1,
        },
        {
            "key": "showHiddenFiles",
            "name": "Show Hidden Files",
            "category": "System",
            "type": "toggle",
            "value": int(stored.get("showHiddenFiles", 0)),
        },
        {
            "key": "libraryUseMetadata",
            "name": "Library Use Metadata",
            "category": "System",
            "type": "toggle",
            "value": int(stored.get("libraryUseMetadata", 1)),
        },
        {
            "key": "clockShowHeader",
            "name": "Clock in Header",
            "category": "Status Bar",
            "type": "toggle",
            "value": int(stored.get("clockShowHeader", 0)),
        },
    ]

    if keys_filter:
        return [item for item in items if item["key"] in keys_filter]
    return items


def apply_settings_updates(storage: StorageBackend, updates: dict) -> None:
    stored = load_json_file(storage, "/.crosspoint/settings.json", default={})
    kr = load_json_file(storage, "/.crosspoint/koreader.json", default={})
    if "cfgVersion" not in kr:
        kr["cfgVersion"] = 3
    kr_headers = kr.get("headers") if isinstance(kr.get("headers"), list) else []
    while len(kr_headers) < 2:
        kr_headers.append({"name": "", "value_obf": ""})

    kr_changed = False
    stored_changed = False

    for k, v in updates.items():
        if k in ("koUsername", "koreaderUsername"):
            kr["username"] = str(v)
            kr_changed = True
        elif k in ("koPassword", "koreaderPassword"):
            kr["password_obf"] = obfuscate_str(str(v))
            kr.pop("password", None)
            kr_changed = True
        elif k in ("koServerUrl", "koreaderSyncServerUrl"):
            kr["serverUrl"] = str(v)
            kr_changed = True
        elif k == "koHeader1Name":
            kr_headers[0]["name"] = str(v)
            kr_changed = True
        elif k == "koHeader1Value":
            kr_headers[0]["value_obf"] = obfuscate_str(str(v))
            kr_changed = True
        elif k == "koHeader2Name":
            kr_headers[1]["name"] = str(v)
            kr_changed = True
        elif k == "koHeader2Value":
            kr_headers[1]["value_obf"] = obfuscate_str(str(v))
            kr_changed = True
        elif k == "koServerType":
            kr["serverType"] = int(v)
            kr_changed = True
        elif k in ("koMatchMethod", "koreaderMatchMethod"):
            kr["matchMethod"] = int(v)
            kr_changed = True
        elif k in ("koSendMetadata", "koreaderSendMetadata"):
            kr["sendMetadata"] = bool(int(v)) if isinstance(v, (int, float, bool)) else bool(v)
            kr_changed = True
        elif k in ("koSyncBehavior", "koreaderSyncBehavior"):
            kr["syncBehavior"] = int(v)
            kr_changed = True
        elif k == "koSyncClippings":
            kr["syncClippings"] = bool(int(v)) if isinstance(v, (int, float, bool)) else bool(v)
            kr_changed = True
        else:
            stored[k] = v
            stored_changed = True

    stored["pluginsEnabled"] = 1
    if kr_changed:
        kr["headers"] = kr_headers
        save_json_file(storage, "/.crosspoint/koreader.json", kr)
    if stored_changed or not storage.exists("/.crosspoint/settings.json"):
        save_json_file(storage, "/.crosspoint/settings.json", stored)


class CrossPointWebHandler(BaseHTTPRequestHandler):
    server_version = "CrossPointPlaydateWebHost/1.0"

    @property
    def storage(self) -> StorageBackend:
        return self.server.storage  # type: ignore

    @property
    def html_dir(self) -> str:
        return self.server.html_dir  # type: ignore

    @property
    def tools_dir(self) -> str:
        return self.server.tools_dir  # type: ignore

    def log_message(self, fmt: str, *args):
        sys.stderr.write(f"[web_host] {self.address_string()} - {fmt % args}\n")

    def _send_json(self, status: int, payload, extra_headers=None):
        body = (
            payload.encode("utf-8")
            if isinstance(payload, str)
            else json.dumps(payload).encode("utf-8")
        )
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Expose-Headers", "X-Relay-Status, X-Relay-Headers, ETag")
        if extra_headers:
            for hk, hv in extra_headers.items():
                self.send_header(hk, hv)
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, status: int, content_type: str, data: bytes, extra_headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Expose-Headers", "X-Relay-Status, X-Relay-Headers, ETag")
        if extra_headers:
            for hk, hv in extra_headers.items():
                self.send_header(hk, hv)
        self.end_headers()
        self.wfile.write(data)

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0") or "0")
        return self.rfile.read(length) if length > 0 else b""

    def _render_html_page(self, filename: str) -> bytes:
        full_path = os.path.join(self.html_dir, filename)
        with open(full_path, "r", encoding="utf-8") as f:
            content = f.read()

        def replace_inc(match):
            inc_rel = match.group(1)
            inc_path = os.path.join(self.html_dir, inc_rel)
            if os.path.isfile(inc_path):
                with open(inc_path, "r", encoding="utf-8") as inc_f:
                    return inc_f.read()
            return ""

        content = re.sub(r'<!--#include\s+file="([^"]+)"\s*-->', replace_inc, content)
        return content.encode("utf-8")

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Access-Control-Expose-Headers", "X-Relay-Status, X-Relay-Headers, ETag")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        route = parsed.path
        qs = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)

        if route == "/hub":
            hub_path = os.path.join(self.tools_dir, "playdate_web_hub.html")
            if os.path.isfile(hub_path):
                with open(hub_path, "rb") as f:
                    self._send_bytes(200, "text/html; charset=utf-8", f.read())
                return

        html_routes = {
            "/": "HomePage.html",
            "/files": "FilesPage.html",
            "/settings": "SettingsPage.html",
            "/fonts": "FontsPage.html",
            "/plugins": "PluginsPage.html",
            "/plugins-run": "RunnerPage.html",
        }
        if route in html_routes:
            data = self._render_html_page(html_routes[route])
            self._send_bytes(
                200,
                "text/html; charset=utf-8",
                data,
                extra_headers={"Set-Cookie": "cpPlugins=1; Path=/; SameSite=Strict"},
            )
            return

        static_routes = {
            "/assets/web.css": ("assets/web.css", "text/css; charset=utf-8"),
            "/assets/icons.svg": ("assets/icons.svg", "image/svg+xml"),
            "/assets/crosspoint-icon.svg": ("assets/crosspoint-icon.svg", "image/svg+xml"),
            "/js/jszip.min.js": ("js/jszip.min.js", "application/javascript; charset=utf-8"),
            "/js/epub-optimizer.js": ("js/epub-optimizer.js", "application/javascript; charset=utf-8"),
            "/js/plugin-host.js": ("js/plugin-host.js", "application/javascript; charset=utf-8"),
        }
        if route in static_routes:
            rel_file, mime = static_routes[route]
            full_path = os.path.join(self.html_dir, rel_file)
            with open(full_path, "rb") as f:
                self._send_bytes(200, mime, f.read())
            return

        if route == "/api/status":
            self._handle_status(qs)
        elif route == "/api/plugins":
            self._send_json(200, scan_installed_plugins(self.storage))
        elif route == "/plugin":
            self._handle_plugin_file_get(qs)
        elif route == "/api/plugin-fs":
            self._handle_plugin_fs_get(qs)
        elif route == "/api/settings":
            keys_param = qs.get("keys", [""])[0]
            keys_filter = {k.strip() for k in keys_param.split(",") if k.strip()} if keys_param else None
            self._send_json(200, build_settings_array(self.storage, keys_filter))
        elif route == "/api/files":
            self._handle_files_list(qs)
        elif route == "/download":
            self._handle_download(qs)
        elif route == "/api/opds":
            self._handle_opds_get()
        elif route == "/api/wifi":
            self._handle_wifi_get()
        elif route == "/api/fonts":
            self._handle_fonts_get()
        elif route == "/api/plugin-jobs/claim":
            self._send_json(200, {})
        elif route == "/api/plugin-jobs/status":
            self._send_json(200, {"state": "done"})
        else:
            self._send_json(404, {"error": f"Not found: {route}"})

    def do_POST(self):
        parsed = urllib.parse.urlsplit(self.path)
        route = parsed.path
        qs = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)

        if route == "/api/relay":
            self._handle_relay()
        elif route == "/api/fetch":
            self._handle_fetch()
        elif route == "/api/plugin-fs":
            self._handle_plugin_fs_post(qs)
        elif route == "/api/crypto":
            self._handle_crypto()
        elif route == "/api/book-key":
            self._handle_book_key()
        elif route == "/api/settings":
            body = self._read_body()
            updates = json.loads(body.decode("utf-8")) if body else {}
            apply_settings_updates(self.storage, updates)
            self._send_bytes(200, "text/plain; charset=utf-8", b"OK")
        elif route == "/upload":
            self._handle_upload(qs)
        elif route == "/mkdir":
            self._handle_mkdir()
        elif route == "/rename":
            self._handle_rename()
        elif route == "/move":
            self._handle_move()
        elif route == "/delete":
            self._handle_delete()
        elif route == "/api/opds":
            self._handle_opds_post()
        elif route == "/api/opds/delete":
            self._handle_opds_delete()
        elif route == "/api/wifi":
            self._handle_wifi_post()
        elif route == "/api/wifi/delete":
            self._handle_wifi_delete()
        elif route == "/api/fonts/upload":
            self._handle_fonts_upload(qs)
        elif route == "/api/fonts/delete":
            self._handle_fonts_delete()
        elif route in ("/api/plugin-jobs", "/api/plugin-jobs/complete"):
            self._send_json(200, {"ok": True})
        else:
            self._send_json(404, {"error": f"Not found: {route}"})

    # ---- Endpoint implementations ----

    def _handle_status(self, qs: dict):
        resp = {
            "version": "1.2.0-playdate",
            "firmwareFamily": FIRMWARE_FAMILY,
            "pluginApi": PLUGIN_API_VERSION,
            "pluginCapabilities": PLUGIN_CAPABILITIES,
            "ip": "127.0.0.1",
            "mode": "STA",
            "rssi": -45,
            "freeHeap": 8388608,
            "uptime": int(time.time() - self.server.start_time),  # type: ignore
            "device": "Playdate",
            "deviceModel": "Playdate",
            "psramBytes": 16 * 1024 * 1024,
            "serial": "Playdate",
            "storageMode": self.storage.mode_name,
            "storageTarget": self.storage.target_description,
        }
        plugin = qs.get("plugin", [""])[0]
        if plugin:
            resp["deviceId"] = get_plugin_device_id(self.storage, plugin)
        self._send_json(200, resp)

    def _safe_component(self, s: str) -> bool:
        return bool(s) and "/" not in s and "\\" not in s and ".." not in s

    def _handle_plugin_file_get(self, qs: dict):
        name = qs.get("name", qs.get("id", [""]))[0]
        fn = qs.get("file", [""])[0]
        if not self._safe_component(name) or not self._safe_component(fn):
            self._send_bytes(400, "text/plain; charset=utf-8", b"bad plugin path")
            return
        pdir = find_plugin_dir(self.storage, name)
        if not pdir:
            self._send_bytes(404, "text/plain; charset=utf-8", b"not found")
            return
        vpath = f"{pdir}/{fn}"
        if not self.storage.exists(vpath) or self.storage.is_dir(vpath):
            self._send_bytes(404, "text/plain; charset=utf-8", b"not found")
            return
        data = self.storage.read_file(vpath)
        etag = '"' + hashlib.md5(data).hexdigest() + '"'
        if self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.end_headers()
            return
        mime = "application/octet-stream"
        if fn.endswith(".js"):
            mime = "application/javascript; charset=utf-8"
        elif fn.endswith(".css"):
            mime = "text/css; charset=utf-8"
        elif fn.endswith(".html"):
            mime = "text/html; charset=utf-8"
        elif fn.endswith(".json"):
            mime = "application/json; charset=utf-8"
        elif fn.endswith(".svg"):
            mime = "image/svg+xml"
        self._send_bytes(200, mime, data, extra_headers={"ETag": etag, "Cache-Control": "no-cache"})

    def _handle_relay(self):
        """Matches `CrossPointWebServer::handleRelay()` and `CrossPoint.relay()` in `plugin-host.js`."""
        raw = self._read_body()
        try:
            doc = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"error": "bad json"})
            return
        url = doc.get("url", "")
        method = str(doc.get("method", "GET")).upper()
        req_headers = doc.get("headers") or {}
        req_body_str = doc.get("body", "")

        if not url:
            self._send_json(400, {"error": "missing plugin/url"})
            return

        req_data = None
        if method not in ("GET", "HEAD") and req_body_str:
            req_data = str(req_body_str).encode("utf-8")

        try:
            req = urllib.request.Request(url, data=req_data, method=method)
            if isinstance(req_headers, dict):
                for hk, hv in req_headers.items():
                    req.add_header(str(hk), str(hv))
            elif isinstance(req_headers, list):
                for pair in req_headers:
                    if isinstance(pair, (list, tuple)) and len(pair) == 2:
                        req.add_header(str(pair[0]), str(pair[1]))
            if not req.has_header("User-agent"):
                req.add_header("User-Agent", "CrossPoint")

            ctx = ssl.create_default_context()
            with urllib.request.urlopen(req, timeout=30, context=ctx) as resp:
                status_code = resp.status
                resp_bytes = resp.read()
                resp_headers_list = [[k, v] for k, v in resp.headers.items()]
        except urllib.error.HTTPError as e:
            status_code = e.code
            resp_bytes = e.read() if e.fp else b""
            resp_headers_list = [[k, v] for k, v in e.headers.items()] if e.headers else []
        except Exception as e:
            self._send_json(502, {"error": f"relay failed: {e}"})
            return

        headers_json = json.dumps(resp_headers_list)
        self._send_bytes(
            200,
            "application/octet-stream",
            resp_bytes,
            extra_headers={
                "X-Relay-Status": str(status_code),
                "X-Relay-Headers": headers_json,
            },
        )

    def _handle_fetch(self):
        """Matches `CrossPointWebServer::handleFetch()` and `CrossPoint.fetchToSd()`."""
        raw = self._read_body()
        try:
            doc = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"error": "bad json"})
            return
        url = doc.get("url", "")
        dest = normalize_vpath(doc.get("dest", doc.get("path", "")))
        method = str(doc.get("method", "GET")).upper()
        req_headers = doc.get("headers") or {}
        req_body = doc.get("body", "")

        if not url or not dest or dest == "/":
            self._send_json(400, {"error": "bad url/dest"})
            return

        try:
            data = str(req_body).encode("utf-8") if (method not in ("GET", "HEAD") and req_body) else None
            req = urllib.request.Request(url, data=data, method=method)
            if isinstance(req_headers, dict):
                for hk, hv in req_headers.items():
                    req.add_header(str(hk), str(hv))
            if not req.has_header("User-agent"):
                req.add_header("User-Agent", "CrossPoint")
            ctx = ssl.create_default_context()
            with urllib.request.urlopen(req, timeout=60, context=ctx) as resp:
                status_code = resp.status
                payload = resp.read()
        except urllib.error.HTTPError as e:
            err_body = (e.read() if e.fp else b"").decode("utf-8", errors="replace")[:512]
            self._send_json(
                200,
                {
                    "status": e.code,
                    "bytes": 0,
                    "complete": False,
                    "error": "http status",
                    "body": err_body,
                },
            )
            return
        except Exception as e:
            self._send_json(
                200,
                {
                    "status": -1,
                    "bytes": 0,
                    "complete": False,
                    "error": f"transport failure: {e}",
                },
            )
            return

        self.storage.write_file(dest, payload)
        self._send_json(
            200,
            {
                "status": status_code,
                "bytes": len(payload),
                "total": len(payload),
                "complete": True,
            },
        )

    def _handle_plugin_fs_get(self, qs: dict):
        target = normalize_vpath(qs.get("path", ["/"])[0])
        if target == "/.crosspoint/device_secret.bin" or not self.storage.exists(target):
            self._send_json(404, {"error": "not found"})
            return
        data = self.storage.read_file(target)
        self._send_bytes(200, "application/octet-stream", data)

    def _handle_plugin_fs_post(self, qs: dict):
        plugin = qs.get("plugin", [""])[0]
        target = normalize_vpath(qs.get("path", [""])[0])
        if not self._safe_component(plugin) or not target or target == "/" or target == "/.crosspoint/device_secret.bin":
            self._send_json(400, {"error": "bad path"})
            return
        raw = self._read_body()
        ctype = self.headers.get("Content-Type", "")
        if "multipart/form-data" in ctype:
            files = self._parse_multipart_files(ctype, raw)
            if not files:
                self._send_json(400, {"error": "missing file part"})
                return
            _, raw = files[0]
        if len(raw) == 0:
            self._send_json(400, {"error": "empty body"})
            return
        self.storage.write_file(target, raw)
        self._send_json(200, {"ok": True, "bytes": len(raw)})

    def _handle_crypto(self):
        raw = self._read_body()
        try:
            doc = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"error": "bad json"})
            return
        op = doc.get("op", "")
        if op == "random":
            n = max(0, min(4096, int(doc.get("len", 16))))
            self._send_json(200, {"data": base64.b64encode(os.urandom(n)).decode("ascii")})
        elif op == "sha1":
            d = base64.b64decode(doc.get("data", ""))
            self._send_json(200, {"data": base64.b64encode(hashlib.sha1(d).digest()).decode("ascii")})
        elif op == "sha256":
            d = base64.b64decode(doc.get("data", ""))
            self._send_json(200, {"data": base64.b64encode(hashlib.sha256(d).digest()).decode("ascii")})
        elif op == "hmac-sha256":
            k = bytes.fromhex(doc.get("key", ""))
            d = bytes.fromhex(doc.get("data", ""))
            self._send_json(200, {"result": hmac.new(k, d, hashlib.sha256).hexdigest()})
        else:
            self._send_json(200, {"error": f"unsupported op in python stdlib: {op}"})

    def _handle_book_key(self):
        raw = self._read_body()
        try:
            doc = json.loads(raw.decode("utf-8"))
            book_path = normalize_vpath(doc.get("path", ""))
            key_str = doc.get("key", "")
            expires_at = int(doc.get("expires", doc.get("expiresAt", 0)))
            try:
                key_bytes = base64.b64decode(key_str)
            except Exception:
                key_bytes = bytes.fromhex(key_str)
        except Exception:
            self._send_json(400, {"error": "bad path/key"})
            return
        if not book_path or len(key_bytes) not in (16, 32) or expires_at < 0:
            self._send_json(400, {"error": "bad path/key"})
            return
        blob = struct.pack("<Bq", len(key_bytes), expires_at) + key_bytes
        self.storage.write_file(book_path + ".key", blob)
        self._send_json(200, {"ok": True})

    def _handle_files_list(self, qs: dict):
        vpath = normalize_vpath(qs.get("path", ["/"])[0])
        entries = self.storage.list_dir(vpath)
        stored = load_json_file(self.storage, "/.crosspoint/settings.json", default={})
        show_hidden = bool(stored.get("showHiddenFiles", False))
        # Also allow listing inside /.crosspoint/plugins even when showHiddenFiles is off
        in_plugin_dir = vpath.startswith("/.crosspoint/plugins") or vpath.startswith("/.plugins")
        out = []
        for e in entries:
            name = e["name"]
            if not show_hidden and not in_plugin_dir and name.startswith("."):
                continue
            item = {
                "name": name,
                "isDirectory": e["is_dir"],
                "size": e["size"],
                "isEpub": not e["is_dir"] and name.lower().endswith((".epub", ".xtc", ".xtch", ".txt", ".md")),
            }
            out.append(item)
        self._send_json(200, out)

    def _handle_download(self, qs: dict):
        vpath = normalize_vpath(qs.get("path", [""])[0])
        if not vpath or vpath == "/":
            self._send_bytes(400, "text/plain; charset=utf-8", b"Invalid path")
            return
        item_name = os.path.basename(vpath)
        if item_name.startswith(".") or vpath in (
            "/.crosspoint/koreader.json",
            "/.crosspoint/opds.json",
            "/.crosspoint/wifi.json",
            "/.crosspoint/device_secret.bin",
        ):
            self._send_bytes(403, "text/plain; charset=utf-8", b"Cannot access system files")
            return
        if not self.storage.exists(vpath):
            self._send_bytes(404, "text/plain; charset=utf-8", b"Item not found")
            return
        if self.storage.is_dir(vpath):
            self._send_bytes(400, "text/plain; charset=utf-8", b"Path is a directory")
            return
        data = self.storage.read_file(vpath)
        mime, _ = mimetypes.guess_type(item_name)
        self._send_bytes(
            200,
            mime or "application/octet-stream",
            data,
            extra_headers={"Content-Disposition": f'attachment; filename="{item_name}"'},
        )

    def _parse_multipart_files(self, ctype: str, body: bytes) -> list:
        m = re.search(r"boundary=([^;\s]+)", ctype)
        if not m:
            return []
        boundary = m.group(1).strip('"').encode("utf-8")
        parts = body.split(b"--" + boundary)
        files = []
        for part in parts[1:]:
            if part.startswith(b"--") or not part.strip():
                continue
            if part.startswith(b"\r\n"):
                part = part[2:]
            sep = part.find(b"\r\n\r\n")
            if sep == -1:
                continue
            hdr_block = part[:sep].decode("utf-8", errors="replace")
            content = part[sep + 4 :]
            if content.endswith(b"\r\n"):
                content = content[:-2]
            fn_m = re.search(r'filename="([^"]*)"', hdr_block)
            if fn_m:
                files.append((fn_m.group(1), content))
        return files

    def _parse_form_fields(self, ctype: str, body: bytes) -> dict:
        if "application/json" in ctype:
            try:
                return json.loads(body.decode("utf-8"))
            except Exception:
                return {}
        if "multipart/form-data" in ctype:
            m = re.search(r"boundary=([^;\s]+)", ctype)
            if not m:
                return {}
            boundary = m.group(1).strip('"').encode("utf-8")
            fields = {}
            for part in body.split(b"--" + boundary)[1:]:
                if part.startswith(b"\r\n"):
                    part = part[2:]
                sep = part.find(b"\r\n\r\n")
                if sep == -1:
                    continue
                hdr_block = part[:sep].decode("utf-8", errors="replace")
                content = part[sep + 4 :]
                if content.endswith(b"\r\n"):
                    content = content[:-2]
                nm = re.search(r'name="([^"]*)"', hdr_block)
                if nm and 'filename="' not in hdr_block:
                    fields[nm.group(1)] = content.decode("utf-8", errors="replace")
            return fields
        return {
            k: v[0]
            for k, v in urllib.parse.parse_qs(body.decode("utf-8", errors="replace"), keep_blank_values=True).items()
        }

    def _handle_upload(self, qs: dict):
        dest_dir = normalize_vpath(qs.get("path", ["/"])[0])
        ctype = self.headers.get("Content-Type", "")
        raw = self._read_body()
        files = self._parse_multipart_files(ctype, raw)
        if not files:
            self._send_bytes(400, "text/plain; charset=utf-8", b"No file uploaded")
            return
        last_fn = ""
        for fn, data in files:
            safe_fn = os.path.basename(fn.replace("\\", "/"))
            target = normalize_vpath(f"{dest_dir}/{safe_fn}")
            self.storage.write_file(target, data)
            last_fn = safe_fn
        self._send_bytes(200, "text/plain; charset=utf-8", f"File uploaded successfully: {last_fn}".encode("utf-8"))

    def _handle_mkdir(self):
        fields = self._parse_form_fields(self.headers.get("Content-Type", ""), self._read_body())
        name = fields.get("name", "").strip()
        path_arg = fields.get("path", "/").strip()
        if name:
            target = normalize_vpath(f"{path_arg}/{name}")
        else:
            target = normalize_vpath(path_arg)
        if not target or target == "/":
            self._send_bytes(400, "text/plain; charset=utf-8", b"Missing folder name")
            return
        self.storage.mkdir(target)
        self._send_bytes(200, "text/plain; charset=utf-8", f"Folder created: {target}".encode("utf-8"))

    def _handle_rename(self):
        fields = self._parse_form_fields(self.headers.get("Content-Type", ""), self._read_body())
        old_path = normalize_vpath(fields.get("path", ""))
        new_name = fields.get("name", "").strip()
        if not old_path or not new_name:
            self._send_bytes(400, "text/plain; charset=utf-8", b"Missing path or new name")
            return
        parent = os.path.dirname(old_path)
        new_path = normalize_vpath(f"{parent}/{new_name}")
        if self.storage.rename(old_path, new_path):
            self._send_bytes(200, "text/plain; charset=utf-8", b"Renamed successfully")
        else:
            self._send_bytes(404, "text/plain; charset=utf-8", b"Failed to rename")

    def _handle_move(self):
        fields = self._parse_form_fields(self.headers.get("Content-Type", ""), self._read_body())
        dest = normalize_vpath(fields.get("dest", "/"))
        item_path = fields.get("path", "")
        if item_path:
            src = normalize_vpath(item_path)
            target = normalize_vpath(f"{dest}/{os.path.basename(src)}")
            if self.storage.rename(src, target):
                self._send_bytes(200, "text/plain; charset=utf-8", b"Moved successfully")
            else:
                self._send_bytes(500, "text/plain; charset=utf-8", b"Failed to move file")
            return
        paths = fields.get("paths") or []
        if isinstance(paths, str):
            try:
                paths = json.loads(paths)
            except Exception:
                paths = [paths]
        moved = 0
        for p in paths:
            src = normalize_vpath(p)
            target = normalize_vpath(f"{dest}/{os.path.basename(src)}")
            if self.storage.rename(src, target):
                moved += 1
        self._send_json(200, {"moved": moved})

    def _handle_delete(self):
        fields = self._parse_form_fields(self.headers.get("Content-Type", ""), self._read_body())
        paths = []
        if "paths" in fields:
            try:
                paths = json.loads(fields["paths"]) if isinstance(fields["paths"], str) else list(fields["paths"])
            except Exception:
                paths = [fields["paths"]]
        elif "path" in fields:
            paths = [fields["path"]]
        if not paths:
            self._send_bytes(400, "text/plain; charset=utf-8", b"Missing path or paths argument")
            return
        all_ok = True
        for p in paths:
            vpath = normalize_vpath(str(p))
            if not vpath or vpath == "/":
                all_ok = False
                continue
            if not self.storage.remove(vpath):
                all_ok = False
        if all_ok:
            self._send_bytes(200, "text/plain; charset=utf-8", b"All items deleted successfully")
        else:
            self._send_bytes(500, "text/plain; charset=utf-8", b"Failed to delete some items")

    def _handle_opds_get(self):
        doc = load_json_file(self.storage, "/.crosspoint/opds.json", default={"servers": []})
        servers = doc.get("servers", [])
        out = []
        for idx, srv in enumerate(servers):
            hdrs = srv.get("headers", [])
            out.append({
                "index": idx,
                "name": srv.get("name", ""),
                "url": srv.get("url", ""),
                "username": srv.get("username", ""),
                "hasPassword": bool(srv.get("password_obf") or srv.get("password")),
                "headers": [
                    {"name": h.get("name", ""), "hasValue": bool(h.get("value_obf") or h.get("value"))}
                    for h in hdrs
                ],
            })
        self._send_json(200, out)

    def _handle_opds_post(self):
        doc = load_json_file(self.storage, "/.crosspoint/opds.json", default={"servers": []})
        servers = doc.get("servers", [])
        body = json.loads(self._read_body().decode("utf-8") or "{}")
        entry = {
            "name": body.get("name", ""),
            "url": body.get("url", ""),
            "username": body.get("username", ""),
            "password_obf": obfuscate_str(body.get("password", "")),
        }
        idx = body.get("index", body.get("id"))
        if idx is not None and 0 <= int(idx) < len(servers):
            i = int(idx)
            if "password" not in body and "password_obf" in servers[i]:
                entry["password_obf"] = servers[i]["password_obf"]
            servers[i] = entry
        else:
            servers.append(entry)
        doc["servers"] = servers
        save_json_file(self.storage, "/.crosspoint/opds.json", doc)
        self._send_bytes(200, "text/plain; charset=utf-8", b"OK")

    def _handle_opds_delete(self):
        doc = load_json_file(self.storage, "/.crosspoint/opds.json", default={"servers": []})
        servers = doc.get("servers", [])
        body = json.loads(self._read_body().decode("utf-8") or "{}")
        idx = int(body.get("index", body.get("id", -1)))
        if 0 <= idx < len(servers):
            servers.pop(idx)
            doc["servers"] = servers
            save_json_file(self.storage, "/.crosspoint/opds.json", doc)
        self._send_bytes(200, "text/plain; charset=utf-8", b"OK")

    def _handle_wifi_get(self):
        doc = load_json_file(self.storage, "/.crosspoint/wifi.json", default={"networks": []})
        nets = doc.get("networks", [])
        out = [
            {
                "index": i,
                "ssid": n.get("ssid", ""),
                "hasPassword": bool(n.get("password_obf")),
                "isLastConnected": False,
            }
            for i, n in enumerate(nets)
        ]
        self._send_json(200, out)

    def _handle_wifi_post(self):
        doc = load_json_file(self.storage, "/.crosspoint/wifi.json", default={"networks": []})
        nets = doc.get("networks", [])
        body = json.loads(self._read_body().decode("utf-8") or "{}")
        ssid = body.get("ssid", "")
        pw_obf = obfuscate_str(body.get("password", ""))
        idx = body.get("index")
        if idx is not None and 0 <= int(idx) < len(nets):
            i = int(idx)
            if "password" not in body and "password_obf" in nets[i]:
                pw_obf = nets[i]["password_obf"]
            nets[i] = {"ssid": ssid, "password_obf": pw_obf}
        else:
            nets = [n for n in nets if n.get("ssid") != ssid]
            nets.append({"ssid": ssid, "password_obf": pw_obf})
        doc["networks"] = nets
        save_json_file(self.storage, "/.crosspoint/wifi.json", doc)
        self._send_bytes(200, "text/plain; charset=utf-8", b"OK")

    def _handle_wifi_delete(self):
        doc = load_json_file(self.storage, "/.crosspoint/wifi.json", default={"networks": []})
        nets = doc.get("networks", [])
        body = json.loads(self._read_body().decode("utf-8") or "{}")
        idx = int(body.get("index", -1))
        if 0 <= idx < len(nets):
            nets.pop(idx)
            doc["networks"] = nets
            save_json_file(self.storage, "/.crosspoint/wifi.json", doc)
        self._send_bytes(200, "text/plain; charset=utf-8", b"OK")

    def _handle_fonts_get(self):
        entries = self.storage.list_dir("/.crosspoint/fonts")
        families = []
        for e in entries:
            if e["is_dir"]:
                sub = self.storage.list_dir(f"/.crosspoint/fonts/{e['name']}")
                files = [
                    {"name": s["name"], "size": s["size"]}
                    for s in sub
                    if not s["is_dir"] and s["name"].lower().endswith((".cpfont", ".epdfont", ".ttf", ".otf"))
                ]
                if files:
                    families.append({"name": e["name"], "sizes": [], "files": files})
        self._send_json(200, {"vectorFonts": True, "families": families})

    def _handle_fonts_upload(self, qs: dict):
        ctype = self.headers.get("Content-Type", "")
        raw = self._read_body()
        fields = self._parse_form_fields(ctype, raw)
        files = self._parse_multipart_files(ctype, raw)
        family = re.sub(r"[^A-Za-z0-9_-]", "_", fields.get("family", "CustomFont"))
        if not files or not family:
            self._send_json(400, {"ok": False, "error": "Missing family or file"})
            return
        for fn, data in files:
            safe_fn = os.path.basename(fn.replace("\\", "/"))
            self.storage.write_file(f"/.crosspoint/fonts/{family}/{safe_fn}", data)
        self._send_json(200, {"ok": True})

    def _handle_fonts_delete(self):
        body = json.loads(self._read_body().decode("utf-8") or "{}")
        name = body.get("family", body.get("name", ""))
        if self._safe_component(name):
            self.storage.remove(f"/.crosspoint/fonts/{name}")
        self._send_json(200, {"ok": True})


def create_storage_backend(args, repo_root: str) -> StorageBackend:
    if args.serial:
        backend = SerialStorageBackend(args.serial)
        if not backend.ping():
            sys.stderr.write(
                f"Warning: Playdate on {args.serial} did not reply to !CP PING yet. "
                "Make sure CrossPoint is open on your Playdate.\n"
            )
        return backend

    if args.data_dir:
        return DiskStorageBackend(args.data_dir, mode_name="Custom Data Directory")

    disk_dir, mode_name = find_playdate_data_dir(repo_root)
    if disk_dir and mode_name == "Playdate Data Disk":
        return DiskStorageBackend(disk_dir, mode_name=mode_name)

    serial_port = find_playdate_serial_port()
    if serial_port:
        try:
            backend = SerialStorageBackend(serial_port)
            if backend.ping():
                return backend
        except Exception as e:
            sys.stderr.write(f"[web_host] Note: Found serial port {serial_port} but handshake failed ({e}).\n")

    if disk_dir:
        return DiskStorageBackend(disk_dir, mode_name=mode_name)

    default_sim = os.path.join(repo_root, "playdate", "SimulatorDisk", "Data", "com.crosspoint.reader")
    return DiskStorageBackend(default_sim, mode_name="Playdate Simulator Disk")


def main():
    parser = argparse.ArgumentParser(
        description="CrossPoint Playdate Web Host - Serves http://localhost:8080/plugins for Playdate"
    )
    parser.add_argument("--port", type=int, default=8080, help="HTTP port to listen on (default: 8080)")
    parser.add_argument("--bind", default="127.0.0.1", help="Address to bind to (default: 127.0.0.1)")
    parser.add_argument(
        "--data-dir",
        default="",
        help="Path to Playdate Data directory (e.g. /Volumes/PLAYDATE/Data/user.XXXX.com.crosspoint.reader)",
    )
    parser.add_argument(
        "--serial",
        default="",
        help="Path to Playdate USB serial port (e.g. /dev/cu.usbmodemPD... or /dev/ttyACM0)",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    html_dir = os.path.join(repo_root, "src", "network", "html")
    if not os.path.isdir(html_dir):
        sys.stderr.write(f"Error: Could not locate src/network/html at {html_dir}\n")
        sys.exit(1)

    storage = create_storage_backend(args, repo_root)

    server = ThreadingHTTPServer((args.bind, args.port), CrossPointWebHandler)
    server.storage = storage  # type: ignore
    server.html_dir = html_dir  # type: ignore
    server.tools_dir = script_dir  # type: ignore
    server.start_time = time.time()  # type: ignore

    url_base = f"http://{args.bind}:{args.port}"
    print("=" * 68)
    print("  CrossPoint Playdate Web Host")
    print("=" * 68)
    print(f"  Storage Mode : {storage.mode_name}")
    print(f"  Target       : {storage.target_description}")
    print(f"  Web Hub      : {url_base}/hub")
    print(f"  Web Plugins  : {url_base}/plugins")
    print(f"  Web Settings : {url_base}/settings")
    print(f"  Web Files    : {url_base}/files")
    print("=" * 68)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down CrossPoint Playdate Web Host...")
        server.server_close()


if __name__ == "__main__":
    main()
