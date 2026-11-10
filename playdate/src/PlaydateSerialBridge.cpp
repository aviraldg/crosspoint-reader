#include <Arduino.h>
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <PlaydateSerialBridge.h>
#include <SecureHttpClient.h>
#include <base64.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "util/BookCacheUtils.h"
#include "util/PluginEvents.h"

extern SdCardFontSystem sdFontSystem;
extern ActivityManager activityManager;

namespace playdate_serial {

namespace {

HalFile gWriteFile;
std::string gWriteTargetPath;
std::string gWriteTmpPath;
std::vector<uint8_t> gWriteBuffer;
size_t gWriteBytes = 0;
bool gWriteError = false;
bool gWriteOpen = false;
bool gPendingRelay = false;
std::string gPendingRelayReqPath;
bool gSerialFastMode = false;
unsigned long gLastSerialActivityMs = 0;

constexpr size_t kWriteBufferFlushThreshold = 16 * 1024;

void enterSerialFastMode() {
  gLastSerialActivityMs = millis();
  if (!gSerialFastMode) {
    gSerialFastMode = true;
    playdate_hal::setSerialFastMode(true);
  }
}

void maybeExitSerialFastMode() {
  if (!gSerialFastMode || gWriteOpen || gPendingRelay) return;
  if (millis() - gLastSerialActivityMs <= 3000) return;
  gSerialFastMode = false;
  playdate_hal::setSerialFastMode(false);
}

void sendLine(const char* kind, const std::string& json) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (pd && pd->system && pd->system->logToConsole) {
    pd->system->logToConsole("@CP %s %s", kind, json.c_str());
  }
}

void sendError(const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  std::string out;
  serializeJson(doc, out);
  sendLine("ERR", out);
}

bool flushWriteBufferToDisk() {
  if (gWriteBuffer.empty()) return true;
  if (!gWriteFile.isOpen()) {
    gWriteError = true;
    return false;
  }
  const size_t n = gWriteBuffer.size();
  if (gWriteFile.write(gWriteBuffer.data(), n) != n) {
    gWriteError = true;
    return false;
  }
  gWriteBuffer.clear();
  return true;
}

void closeWriteState() {
  if (gWriteFile.isOpen()) {
    gWriteFile.close();
  }
  if (!gWriteTmpPath.empty()) {
    Storage.remove(gWriteTmpPath.c_str());
  }
  gWriteTargetPath.clear();
  gWriteTmpPath.clear();
  gWriteBuffer.clear();
  gWriteBytes = 0;
  gWriteError = false;
  gWriteOpen = false;
}

void reloadAfterWrite(const std::string& path) {
  if (path == "/.crosspoint/settings.json") {
    SETTINGS.loadFromFile();
    SETTINGS.frontButtonFollowOrientation = 0;
    SETTINGS.sideButtonFollowOrientation = 0;
    UITheme::getInstance().reload();
    display.setInverted(SETTINGS.screenInverted != 0);
    if (activityManager.isForegroundHomeShown()) {
      activityManager.goHome();
    } else {
      activityManager.requestUpdate();
    }
  } else if (path == "/.crosspoint/koreader.json") {
    KOREADER_STORE.loadFromFile();
  } else if (path == "/.crosspoint/opds.json") {
    OPDS_STORE.loadFromFile();
  } else if (path == "/.crosspoint/wifi.json") {
    WIFI_STORE.loadFromFile();
  } else if (path.rfind("/.crosspoint/plugins/", 0) == 0 || path.rfind("/plugins/", 0) == 0 ||
             path.rfind("/.plugins/", 0) == 0) {
    if (!SETTINGS.pluginsEnabled) {
      SETTINGS.pluginsEnabled = 1;
      SETTINGS.saveToFile();
    }
    pluginevents::refreshSubscriptions();
    activityManager.requestUpdate();
  } else if (path.rfind("/.crosspoint/fonts/", 0) == 0 || path.rfind("/fonts/", 0) == 0) {
    sdFontSystem.markRegistryDirty();
    sdFontSystem.refreshIfDirty();
    activityManager.requestUpdate();
  } else if (library::isLibraryBookFile(path)) {
    clearBookCache(path.c_str());
    library::markLibraryIndexDirty();
    activityManager.requestUpdate();
  }
}

void handlePing() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["device"] = "Playdate";
  doc["version"] = CROSSPOINT_VERSION;
  doc["pluginsEnabled"] = static_cast<int>(SETTINGS.pluginsEnabled);
  std::string out;
  serializeJson(doc, out);
  sendLine("OK", out);
}

void handleStat(const std::string& path) {
  JsonDocument doc;
  doc["ok"] = true;
  if (!Storage.exists(path.c_str())) {
    doc["exists"] = false;
    doc["isDir"] = false;
    doc["size"] = 0;
  } else {
    HalFile f = Storage.open(path.c_str());
    doc["exists"] = static_cast<bool>(f);
    doc["isDir"] = f ? f.isDirectory() : false;
    doc["size"] = (f && !f.isDirectory()) ? f.size() : 0;
    if (f) f.close();
  }
  std::string out;
  serializeJson(doc, out);
  sendLine("OK", out);
}

void handleList(const std::string& path) {
  HalFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    sendError("not a directory");
    return;
  }
  int count = 0;
  char nameBuf[256];
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    nameBuf[0] = '\0';
    entry.getName(nameBuf, sizeof(nameBuf));
    const bool isDir = entry.isDirectory();
    const size_t sz = isDir ? 0 : entry.size();
    entry.close();
    if (nameBuf[0] == '\0') continue;

    JsonDocument itemDoc;
    itemDoc["name"] = nameBuf;
    itemDoc["isDir"] = isDir;
    itemDoc["size"] = sz;
    std::string itemJson;
    serializeJson(itemDoc, itemJson);
    sendLine("ITEM", itemJson);
    ++count;
  }
  dir.close();

  JsonDocument doneDoc;
  doneDoc["ok"] = true;
  doneDoc["count"] = count;
  std::string doneJson;
  serializeJson(doneDoc, doneJson);
  sendLine("OK", doneJson);
}

void handleRead(const std::string& args) {
  // Format: <offset> <len> <path>
  size_t sp1 = args.find(' ');
  size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : args.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    sendError("bad read args");
    return;
  }
  const size_t offset = static_cast<size_t>(std::strtoul(args.substr(0, sp1).c_str(), nullptr, 10));
  const size_t maxLen = static_cast<size_t>(std::strtoul(args.substr(sp1 + 1, sp2 - sp1 - 1).c_str(), nullptr, 10));
  const std::string path = args.substr(sp2 + 1);

  HalFile f = Storage.open(path.c_str(), O_RDONLY);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sendError("not found");
    return;
  }
  const size_t totalSize = f.size();
  if (offset > 0 && !f.seek(offset)) {
    f.close();
    sendError("seek failed");
    return;
  }

  uint8_t chunk[180];
  size_t totalRead = 0;
  while (totalRead < maxLen) {
    const size_t toRead = std::min(sizeof(chunk), maxLen - totalRead);
    const int got = f.read(chunk, toRead);
    if (got <= 0) break;
    const String encoded = base64::encode(chunk, static_cast<size_t>(got));
    sendLine("DATA", encoded.c_str());
    totalRead += static_cast<size_t>(got);
  }
  const bool eof = (offset + totalRead) >= totalSize;
  f.close();

  JsonDocument doneDoc;
  doneDoc["ok"] = true;
  doneDoc["bytes"] = totalRead;
  doneDoc["size"] = totalSize;
  doneDoc["eof"] = eof;
  std::string doneJson;
  serializeJson(doneDoc, doneJson);
  sendLine("OK", doneJson);
}

void handleWriteOpen(const std::string& path) {
  closeWriteState();
  if (path.empty() || path[0] != '/') {
    sendError("bad path");
    return;
  }
  const size_t slash = path.rfind('/');
  if (slash != std::string::npos && slash > 0) {
    Storage.ensureDirectoryExists(path.substr(0, slash).c_str());
  }
  gWriteTargetPath = path;
  gWriteTmpPath = path + ".part";
  Storage.remove(gWriteTmpPath.c_str());
  if (!Storage.openFileForWrite("SER", gWriteTmpPath, gWriteFile)) {
    closeWriteState();
    sendError("open write failed");
    return;
  }
  gWriteBuffer.clear();
  gWriteBuffer.reserve(kWriteBufferFlushThreshold);
  gWriteOpen = true;
  sendLine("OK", "{\"ok\":true}");
}

void appendWriteBytes(const std::string& b64) {
  if (!gWriteOpen || gWriteError || b64.empty()) {
    gWriteError = true;
    return;
  }
  uint8_t decoded[256];
  size_t olen = 0;
  const int rc = mbedtls_base64_decode(decoded, sizeof(decoded), &olen,
                                       reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
  if (rc != 0) {
    gWriteError = true;
    return;
  }
  if (olen > 0) {
    gWriteBuffer.insert(gWriteBuffer.end(), decoded, decoded + olen);
    gWriteBytes += olen;
    if (gWriteBuffer.size() >= kWriteBufferFlushThreshold) {
      flushWriteBufferToDisk();
    }
  }
}

void handleWriteData(const std::string& b64) {
  appendWriteBytes(b64);
}

void handleWriteFlush() {
  JsonDocument doc;
  doc["ok"] = gWriteOpen && !gWriteError;
  doc["written"] = gWriteBytes;
  std::string out;
  serializeJson(doc, out);
  sendLine(doc["ok"].as<bool>() ? "OK" : "ERR", out);
}

void handleWriteChunk(const std::string& b64) {
  appendWriteBytes(b64);
  handleWriteFlush();
}

void handleWriteClose() {
  if (!gWriteOpen || gWriteError || !flushWriteBufferToDisk()) {
    closeWriteState();
    sendError("write failed");
    return;
  }
  gWriteFile.flush();
  const bool closed = gWriteFile.close();
  const std::string target = gWriteTargetPath;
  const std::string tmp = gWriteTmpPath;
  const size_t totalBytes = gWriteBytes;
  gWriteTargetPath.clear();
  gWriteTmpPath.clear();
  gWriteBuffer.clear();
  gWriteBytes = 0;
  gWriteOpen = false;
  gWriteError = false;

  if (!closed || !Storage.replaceFile(tmp.c_str(), target.c_str())) {
    Storage.remove(tmp.c_str());
    sendError("commit failed");
    return;
  }
  reloadAfterWrite(target);

  JsonDocument doc;
  doc["ok"] = true;
  doc["bytes"] = totalBytes;
  std::string out;
  serializeJson(doc, out);
  sendLine("OK", out);
}

void handleMkdir(const std::string& path) {
  const bool ok = Storage.ensureDirectoryExists(path.c_str()) || Storage.mkdir(path.c_str());
  sendLine(ok ? "OK" : "ERR", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"mkdir failed\"}");
}

void handleRemove(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    sendLine("OK", "{\"ok\":true}");
    return;
  }
  HalFile f = Storage.open(path.c_str());
  const bool isDir = f && f.isDirectory();
  if (f) f.close();
  const bool ok = isDir ? (Storage.rmdir(path.c_str()) || Storage.removeDir(path.c_str())) : Storage.remove(path.c_str());
  if (ok && library::isLibraryBookFile(path)) {
    clearBookCache(path.c_str());
    library::markLibraryIndexDirty();
  }
  sendLine(ok ? "OK" : "ERR", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rm failed\"}");
}

void handleRename(const std::string& args) {
  const size_t sep = args.find('\t');
  if (sep == std::string::npos) {
    sendError("bad rename args");
    return;
  }
  const std::string src = args.substr(0, sep);
  const std::string dst = args.substr(sep + 1);
  const bool ok = Storage.replaceFile(src.c_str(), dst.c_str());
  if (ok && (library::isLibraryBookFile(src) || library::isLibraryBookFile(dst))) {
    clearBookCache(src.c_str());
    library::markLibraryIndexDirty();
  }
  sendLine(ok ? "OK" : "ERR", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rename failed\"}");
}

void executePendingRelayOnFiber() {
  const std::string reqPath = gPendingRelayReqPath.empty() ? "/.crosspoint/.serial-relay-req.json" : gPendingRelayReqPath;
  gPendingRelay = false;
  gPendingRelayReqPath.clear();

  std::string rawReq;
  if (!Storage.readFileToString("SER", reqPath, 64 * 1024, rawReq)) {
    Storage.remove(reqPath.c_str());
    sendError("missing relay request file");
    return;
  }
  Storage.remove(reqPath.c_str());

  JsonDocument reqDoc;
  if (deserializeJson(reqDoc, rawReq) != DeserializationError::Ok) {
    sendError("bad relay json");
    return;
  }

  const std::string url = reqDoc["url"] | "";
  const std::string method = reqDoc["method"] | "GET";
  const std::string body = reqDoc["body"] | "";
  const std::string dest = reqDoc["dest"] | "";
  if (url.empty()) {
    sendError("missing url");
    return;
  }

  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setUserAgent("CrossPoint");
  http.setTimeout(30000);
  http.setFollowRedirects(5);

  if (!http.begin(url)) {
    sendError("http begin failed");
    return;
  }

  if (reqDoc["headers"].is<JsonObjectConst>()) {
    for (JsonPairConst kv : reqDoc["headers"].as<JsonObjectConst>()) {
      http.addHeader(kv.key().c_str(), kv.value().as<std::string>());
    }
  }

  if (!dest.empty()) {
    const size_t slash = dest.rfind('/');
    if (slash != std::string::npos && slash > 0) {
      Storage.ensureDirectoryExists(dest.substr(0, slash).c_str());
    }
    const std::string partPath = dest + ".part";
    Storage.remove(partPath.c_str());
    HalFile outFile;
    if (!Storage.openFileForWrite("SER", partPath, outFile)) {
      sendError("cannot open dest file");
      return;
    }
    size_t written = 0;
    bool writeErr = false;
    const int status = http.sendRequest(
        method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
        [&](const uint8_t* data, size_t len) {
          if (outFile.write(data, len) != len) {
            writeErr = true;
            return false;
          }
          written += len;
          return true;
        });
    outFile.flush();
    outFile.close();
    if (status < 200 || status >= 300 || writeErr || !Storage.replaceFile(partPath.c_str(), dest.c_str())) {
      Storage.remove(partPath.c_str());
      JsonDocument errDoc;
      errDoc["ok"] = false;
      errDoc["status"] = status;
      errDoc["error"] = writeErr ? "sd write failed" : "http status";
      std::string out;
      serializeJson(errDoc, out);
      sendLine("ERR", out);
      return;
    }
    reloadAfterWrite(dest);
    JsonDocument doneDoc;
    doneDoc["ok"] = true;
    doneDoc["status"] = status;
    doneDoc["bytes"] = written;
    std::string out;
    serializeJson(doneDoc, out);
    sendLine("OK", out);
    return;
  }

  std::vector<uint8_t> respBody;
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if (respBody.size() + len > 64 * 1024) return false;
        respBody.insert(respBody.end(), data, data + len);
        return true;
      });
  if (status < 0) {
    sendError("http transport failed");
    return;
  }

  constexpr size_t kChunk = 180;
  for (size_t i = 0; i < respBody.size(); i += kChunk) {
    const size_t n = std::min(kChunk, respBody.size() - i);
    const String encoded = base64::encode(respBody.data() + i, n);
    sendLine("DATA", encoded.c_str());
  }

  JsonDocument doneDoc;
  doneDoc["ok"] = true;
  doneDoc["status"] = status;
  doneDoc["bytes"] = respBody.size();
  std::string out;
  serializeJson(doneDoc, out);
  sendLine("OK", out);
}

void onSerialMessage(const char* data) {
  if (!data) return;
  while (*data == ' ' || *data == '\t') ++data;
  if (std::strncmp(data, "!CP ", 4) != 0) return;
  enterSerialFastMode();
  const char* p = data + 4;
  while (*p == ' ') ++p;

  std::string line(p);
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }

  const size_t sp = line.find(' ');
  const std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
  const std::string rest = (sp == std::string::npos) ? std::string() : line.substr(sp + 1);

  if (cmd == "PING") {
    handlePing();
  } else if (cmd == "STAT") {
    handleStat(rest);
  } else if (cmd == "LIST") {
    handleList(rest);
  } else if (cmd == "READ") {
    handleRead(rest);
  } else if (cmd == "WOPEN") {
    handleWriteOpen(rest);
  } else if (cmd == "WDATA") {
    handleWriteData(rest);
  } else if (cmd == "WCHUNK") {
    handleWriteChunk(rest);
  } else if (cmd == "WFLUSH") {
    handleWriteFlush();
  } else if (cmd == "WCLOSE") {
    handleWriteClose();
  } else if (cmd == "MKDIR") {
    handleMkdir(rest);
  } else if (cmd == "RM") {
    handleRemove(rest);
  } else if (cmd == "RENAME") {
    handleRename(rest);
  } else if (cmd == "RELAY") {
    gPendingRelayReqPath = rest;
    gPendingRelay = true;
  } else {
    sendError("unknown command");
  }
}

}  // namespace

void registerSerialBridge(PlaydateAPI* pd) {
  if (pd && pd->system && pd->system->setSerialMessageCallback) {
    pd->system->setSerialMessageCallback(onSerialMessage);
  }
}

void pumpSerialBridge() {
  if (gPendingRelay) {
    executePendingRelayOnFiber();
    gLastSerialActivityMs = millis();
  }
  maybeExitSerialFastMode();
}

}  // namespace playdate_serial
