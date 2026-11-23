#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <PlaydateRuntime.h>
#include <SecureHttpClient.h>
#include <base64.h>

extern "C" {
#include <pd_api.h>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>

namespace freeink {

namespace {

constexpr size_t kReadChunkSize = 2048;
constexpr size_t kMaxAllowedHostEntries = 16;
constexpr size_t kMaxAbandonedAccessEntries = 8;
constexpr int32_t kClosedReapDelayMs = 9000;
constexpr int32_t kBurstClosedReapDelayMs = 8000;
constexpr int32_t kBurstFallbackReapDelayMs = 20000;
constexpr int32_t kUnclosedFallbackReapDelayMs = 35000;
constexpr size_t kRetiredBurstThreshold = 12;

uint32_t gTotalRequests = 0;
uint32_t gActiveConnections = 0;
uint32_t gReapedConnections = 0;
uint32_t gFailedRequests = 0;

const char* netErrName(PDNetErr err) {
  switch (err) {
    case NET_OK:
      return "OK";
    case NET_NO_DEVICE:
      return "NO_DEVICE";
    case NET_BUSY:
      return "BUSY";
    case NET_WRITE_ERROR:
      return "WRITE_ERR";
    case NET_WRITE_BUSY:
      return "WRITE_BUSY";
    case NET_WRITE_TIMEOUT:
      return "WRITE_TIMEOUT";
    case NET_READ_ERROR:
      return "READ_ERR";
    case NET_READ_BUSY:
      return "READ_BUSY";
    case NET_READ_TIMEOUT:
      return "READ_TIMEOUT";
    case NET_READ_OVERFLOW:
      return "READ_OVERFLOW";
    case NET_FRAME_ERROR:
      return "FRAME_ERR";
    case NET_BAD_RESPONSE:
      return "BAD_RESP";
    case NET_ERROR_RESPONSE:
      return "ERR_RESP";
    case NET_RESET_TIMEOUT:
      return "RESET_TIMEOUT";
    case NET_BUFFER_TOO_SMALL:
      return "BUF_SMALL";
    case NET_UNEXPECTED_RESPONSE:
      return "UNEXPECTED_RESP";
    case NET_NOT_CONNECTED_TO_AP:
      return "NO_AP";
    case NET_NOT_IMPLEMENTED:
      return "UNIMPL";
    case NET_CONNECTION_CLOSED:
      return "CLOSED";
  }
  return "UNKNOWN";
}

const char* wifiStatusName(PlaydateAPI* pd) {
  if (!pd || !pd->network || !pd->network->getStatus) return "unknown";
  switch (pd->network->getStatus()) {
    case kWifiNotConnected:
      return "disconnected";
    case kWifiConnected:
      return "connected";
    case kWifiNotAvailable:
      return "unavailable";
  }
  return "unknown";
}

void paceIdleNetworkWait(PlaydateAPI* pd) {
  if (pd && pd->system && pd->system->delay) {
    pd->system->delay(2);
  }
  playdate_hal::pumpMainLoop();
}

struct AllowedHostEntry {
  std::string host;
  uint16_t port = 0;
  bool useSsl = false;
};

std::vector<AllowedHostEntry>& allowedHosts() {
  static std::vector<AllowedHostEntry> cache;
  return cache;
}

bool isHostCachedAllowed(const std::string& host, uint16_t port, bool useSsl) {
  const auto& cache = allowedHosts();
  return std::any_of(cache.begin(), cache.end(),
                     [&](const AllowedHostEntry& e) { return e.port == port && e.useSsl == useSsl && e.host == host; });
}

void rememberAllowedHost(const std::string& host, uint16_t port, bool useSsl) {
  if (isHostCachedAllowed(host, port, useSsl)) return;
  auto& cache = allowedHosts();
  if (cache.size() >= kMaxAllowedHostEntries) {
    cache.erase(cache.begin());
  }
  cache.push_back(AllowedHostEntry{host, port, useSsl});
}

struct AccessWaitState {
  std::string host;
  volatile bool decided = false;
  volatile bool allowed = false;
  volatile bool abandoned = false;
};

// Retains timed-out or aborted requestAccess contexts until Playdate OS delivers
// onAccessResponse (or the game terminates), avoiding UAF on the prompt's host string.
std::vector<std::unique_ptr<AccessWaitState>>& abandonedAccessContexts() {
  static std::vector<std::unique_ptr<AccessWaitState>> list;
  return list;
}

void onAccessResponse(bool allowed, void* userdata) {
  auto* state = static_cast<AccessWaitState*>(userdata);
  if (!state) return;
  state->allowed = allowed;
  state->decided = true;
  if (state->abandoned) {
    auto& abandoned = abandonedAccessContexts();
    auto it = std::find_if(abandoned.begin(), abandoned.end(),
                           [state](const std::unique_ptr<AccessWaitState>& p) { return p.get() == state; });
    if (it != abandoned.end()) {
      abandoned.erase(it);
    }
  }
}

bool isPlaydateAutoRedirectStatus(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307;
}

struct RequestCallbackState {
  uint32_t reqId = 0;
  std::string host;
  std::string path;
  std::string headerBlob;
  std::string payloadBlob;
  std::vector<uint8_t> readBuf;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string interceptedRedirectLocation;
  std::string firstRedirectLocation;
  size_t contentLength = 0;
  int interceptedRedirectStatus = 0;
  bool haveContentLength = false;
  bool chunkedTransfer = false;
  bool closeIssued = false;
  volatile bool inRedirectHop = false;
  volatile bool redirectReady = false;
  volatile bool headersRead = false;
  volatile bool bodyAvailable = false;
  volatile bool requestComplete = false;
  volatile bool connectionClosed = false;
  volatile bool retired = false;
};

void onHeaderReceived(HTTPConnection* conn, const char* key, const char* value) {
  if (!conn || !key || !value) return;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http) return;
  auto* state = static_cast<RequestCallbackState*>(pd->network->http->getUserdata(conn));
  if (!state || state->retired) return;

  std::string name(key);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string val(value);
  while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
  while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();

  const int st = pd->network->http->getResponseStatus ? pd->network->http->getResponseStatus(conn) : 0;

  if (name == "location" && !val.empty() && state->firstRedirectLocation.empty()) {
    state->firstRedirectLocation = val;
  }

  if (isPlaydateAutoRedirectStatus(st)) {
    // Playdate OS intercepts 301/302/303/307 before firing onHeadersRead and attempts an
    // internal reconnect that fails after 2 hops and UAFs SSL contexts on SDK 3.0.x.
    // Capture the first hop's Location so SecureHttpClient::sendRequest follows cleanly.
    if (!state->inRedirectHop) {
      state->inRedirectHop = true;
      state->headers.clear();
      state->haveContentLength = false;
      state->contentLength = 0;
      state->chunkedTransfer = false;
    }
    state->interceptedRedirectStatus = st;
    if (name == "location" && !val.empty() && state->interceptedRedirectLocation.empty()) {
      state->interceptedRedirectLocation = val;
      state->redirectReady = true;
    }
    return;
  }

  if (state->inRedirectHop) {
    state->inRedirectHop = false;
    state->redirectReady = false;
    state->requestComplete = false;
    state->connectionClosed = false;
    state->headers.clear();
    state->haveContentLength = false;
    state->contentLength = 0;
    state->chunkedTransfer = false;
  }

  if (state->headers.size() >= 64) return;
  state->headers.emplace_back(name, val);
  if (name == "transfer-encoding") {
    std::string lowerVal = val;
    std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowerVal.find("chunked") != std::string::npos) {
      state->chunkedTransfer = true;
      state->haveContentLength = false;
      state->contentLength = 0;
    }
  } else if (name == "content-length" && !state->chunkedTransfer) {
    state->contentLength = static_cast<size_t>(std::strtoul(val.c_str(), nullptr, 10));
    state->haveContentLength = true;
  }
}

void onHeadersRead(HTTPConnection* conn) {
  if (!conn) return;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http) return;
  auto* state = static_cast<RequestCallbackState*>(pd->network->http->getUserdata(conn));
  if (!state || state->retired) return;
  if (state->inRedirectHop) {
    const int st = pd->network->http->getResponseStatus ? pd->network->http->getResponseStatus(conn) : 0;
    const bool stillRedirect = isPlaydateAutoRedirectStatus(st);
    state->inRedirectHop = false;
    state->redirectReady = stillRedirect && !state->interceptedRedirectLocation.empty();
    state->requestComplete = false;
    state->connectionClosed = false;
    state->headers.clear();
    state->haveContentLength = false;
    state->contentLength = 0;
    if (stillRedirect && !state->interceptedRedirectLocation.empty()) {
      state->headers.emplace_back("location", state->interceptedRedirectLocation);
    }
  }
  state->headersRead = true;
}

void onResponse(HTTPConnection* conn) {
  if (!conn) return;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http) return;
  auto* state = static_cast<RequestCallbackState*>(pd->network->http->getUserdata(conn));
  if (state && !state->retired) {
    state->headersRead = true;
    state->bodyAvailable = true;
  }
}

void onRequestComplete(HTTPConnection* conn) {
  if (!conn) return;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http) return;
  auto* state = static_cast<RequestCallbackState*>(pd->network->http->getUserdata(conn));
  if (state) {
    if (!state->retired) {
      state->bodyAvailable = true;
    }
    state->requestComplete = true;
  }
}

void onConnectionClosed(HTTPConnection* conn) {
  if (!conn) return;
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http) return;
  auto* state = static_cast<RequestCallbackState*>(pd->network->http->getUserdata(conn));
  if (state) state->connectionClosed = true;
}

bool parseByteRange(const std::string& headerLine, int& startOut, int& endOut) {
  std::string lower = headerLine;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower.rfind("range:", 0) != 0) return false;
  const size_t eqPos = lower.find("bytes=");
  if (eqPos == std::string::npos) return false;
  const size_t dashPos = lower.find('-', eqPos + 6);
  if (dashPos == std::string::npos) return false;
  const std::string startStr = lower.substr(eqPos + 6, dashPos - (eqPos + 6));
  std::string endStr = lower.substr(dashPos + 1);
  while (!endStr.empty() && (endStr.back() == '\r' || endStr.back() == '\n' || endStr.back() == ' ')) {
    endStr.pop_back();
  }
  if (startStr.empty() || endStr.empty()) return false;
  char* endPtr1 = nullptr;
  char* endPtr2 = nullptr;
  const long s = std::strtol(startStr.c_str(), &endPtr1, 10);
  const long e = std::strtol(endStr.c_str(), &endPtr2, 10);
  if (endPtr1 == startStr.c_str() || endPtr2 == endStr.c_str() || s < 0 || e < s) return false;
  startOut = static_cast<int>(s);
  endOut = static_cast<int>(e);
  return true;
}

bool ensureHostAccess(const std::string& host, uint16_t port, bool useSsl, uint32_t timeoutMs,
                      const SecureHttpClient::AbortCallback& shouldAbort, bool& abortedOut) {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http || host.empty()) return false;
  if (isHostCachedAllowed(host, port, useSsl)) return true;

  auto waitState = makeUniqueNoThrow<AccessWaitState>();
  if (!waitState) {
    LOG_ERR("NET", "OOM allocating AccessWaitState (%u bytes)", static_cast<unsigned>(sizeof(AccessWaitState)));
    return false;
  }
  waitState->host = host;

  const accessReply status =
      pd->network->http->requestAccess(waitState->host.c_str(), static_cast<int>(port), useSsl,
                                       "CrossPoint network access", onAccessResponse, waitState.get());
  if (status == kAccessAllow) {
    rememberAllowedHost(host, port, useSsl);
    return true;
  }
  if (status == kAccessDeny) {
    LOG_ERR("NET", "Access denied by Playdate OS for %s:%u (ssl=%d)", host.c_str(), static_cast<unsigned>(port),
            useSsl ? 1 : 0);
    return false;
  }

  LOG_INF("NET", "Waiting for Playdate OS network permission for %s:%u", host.c_str(), static_cast<unsigned>(port));
  const unsigned long deadline = millis() + timeoutMs;
  while (!waitState->decided) {
    if (shouldAbort && shouldAbort()) {
      abortedOut = true;
      break;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      LOG_ERR("NET", "Timed out waiting for network permission for %s:%u", host.c_str(), static_cast<unsigned>(port));
      break;
    }
    paceIdleNetworkWait(pd);
  }

  if (!waitState->decided) {
    waitState->abandoned = true;
    auto& abandoned = abandonedAccessContexts();
    if (abandoned.size() < kMaxAbandonedAccessEntries) {
      abandoned.push_back(std::move(waitState));
    } else {
      // Drop oldest only if already decided; otherwise retain to avoid UAF.
      auto doneIt = std::find_if(abandoned.begin(), abandoned.end(),
                                 [](const std::unique_ptr<AccessWaitState>& p) { return p->decided; });
      if (doneIt != abandoned.end()) {
        abandoned.erase(doneIt);
      }
      abandoned.push_back(std::move(waitState));
    }
    return false;
  }

  const bool allowed = waitState->allowed;
  if (allowed) {
    rememberAllowedHost(host, port, useSsl);
  } else {
    LOG_ERR("NET", "User denied network access for %s:%u", host.c_str(), static_cast<unsigned>(port));
  }
  return allowed;
}

struct RetiredConnection {
  HTTPConnection* conn;
  std::unique_ptr<RequestCallbackState> state;
  unsigned long closedAtMs;
};

std::vector<RetiredConnection>& retiredConnections() {
  static std::vector<RetiredConnection> list;
  return list;
}

void reapRetiredConnectionsImpl(PlaydateAPI* pd, bool forceAll) {
  if (!pd || !pd->network || !pd->network->http) {
    if (forceAll) {
      retiredConnections().clear();
    }
    return;
  }
  auto& list = retiredConnections();
  if (list.empty()) return;

  const unsigned long now = millis();
  const bool burstPressure = list.size() > kRetiredBurstThreshold;
  auto it = list.begin();
  while (it != list.end()) {
    const bool closed = it->state && (it->state->connectionClosed || it->state->closeIssued);
    const bool reqDone = it->state && it->state->requestComplete;
    const uint32_t reqId = it->state ? it->state->reqId : 0;
    const int32_t ageMs = static_cast<int32_t>(now - it->closedAtMs);
    const int32_t neededClosedMs = burstPressure ? kBurstClosedReapDelayMs : kClosedReapDelayMs;
    const int32_t neededFallbackMs = burstPressure ? kBurstFallbackReapDelayMs : kUnclosedFallbackReapDelayMs;

    if (forceAll || (closed && ageMs >= neededClosedMs) || ageMs >= neededFallbackMs) {
      if (forceAll && it->state && !it->state->connectionClosed && !it->state->closeIssued) {
        pd->network->http->close(it->conn);
      }
      pd->network->http->setUserdata(it->conn, nullptr);
      pd->network->http->release(it->conn);
      ++gReapedConnections;
      it = list.erase(it);
      const playdate_hal::HeapTelemetry heap = playdate_hal::getHeapTelemetry();
      LOG_INF("NET", "req #%u REAP age=%ldms closed=%d reqDone=%d retiredLeft=%u heapLive=%u",
              static_cast<unsigned>(reqId), static_cast<long>(ageMs), closed ? 1 : 0, reqDone ? 1 : 0,
              static_cast<unsigned>(list.size()), static_cast<unsigned>(heap.liveBytes));
    } else {
      ++it;
    }
  }
  if (list.empty()) {
    shrinkToFit(list);
  }
}

}  // namespace

SecureHttpClient::~SecureHttpClient() { end(); }

bool SecureHttpClient::begin(const std::string& url) {
  _headers.clear();
  _responseHeaders.clear();
  _body.clear();
  _status = 0;
  _contentLength = 0;
  _downloaded = 0;
  _reportProgress = false;
  _haveContentLength = false;
  _bodyComplete = false;
  _callbackAborted = false;
  _aborted = false;
  return parseUrl(url, _scheme, _host, _path, _port);
}

void SecureHttpClient::end() {
  _headers.clear();
  _responseHeaders.clear();
  _body.clear();
  _status = 0;
  _contentLength = 0;
  _downloaded = 0;
  _reportProgress = false;
  _haveContentLength = false;
  _bodyComplete = false;
  _callbackAborted = false;
  _aborted = false;
}

bool SecureHttpClient::connect(const AbortCallback& shouldAbort) {
  if (_host.empty()) return false;
  const uint32_t effectiveTimeoutMs = std::min<uint32_t>(_timeoutMs, 15000U);
  return ensureHostAccess(_host, _port, _scheme == "https", effectiveTimeoutMs, shouldAbort, _aborted);
}

void SecureHttpClient::addHeader(const std::string& name, const std::string& value) {
  if (name.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) return;
  _headers.push_back(name + ": " + value + "\r\n");
}

int SecureHttpClient::sendRequest(const char* method, const uint8_t* payload, size_t payloadLen) {
  return sendRequest(method, payload, payloadLen, [this](const uint8_t* data, size_t len) {
    _body.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
}

int SecureHttpClient::sendRequest(const char* method, const uint8_t* payload, size_t payloadLen,
                                  const DataCallback& onData, const AbortCallback& shouldAbort) {
  std::string activeMethod = method ? method : "GET";
  const uint8_t* activePayload = payload;
  size_t activePayloadLen = payloadLen;

  for (int hop = 0;; ++hop) {
    const int status = sendRequestOnce(activeMethod.c_str(), activePayload, activePayloadLen, onData, shouldAbort);
    if (status < 0 || hop >= _followRedirects || !isRedirectStatus(status)) return status;

    const std::string location = getHeader("location");
    if (location.empty()) return status;
    const std::string base = _scheme + "://" + hostHeader() + _path;
    std::string next;
    if (!resolveUrl(base, location, next)) return status;

    std::string scheme;
    std::string host;
    std::string path;
    uint16_t port = 0;
    if (!parseUrl(next, scheme, host, path, port)) return status;
    if (_scheme == "https" && scheme == "http" && !_allowRedirectDowngrade) return status;

    _scheme = scheme;
    _host = host;
    _path = path;
    _port = port;

    if ((status == 301 || status == 302 || status == 303) && activeMethod != "GET" && activeMethod != "HEAD") {
      activeMethod = "GET";
      activePayload = nullptr;
      activePayloadLen = 0;
    }
  }
}

int SecureHttpClient::sendRequestOnce(const char* method, const uint8_t* payload, size_t payloadLen,
                                      const DataCallback& onData, const AbortCallback& shouldAbort) {
  _status = 0;
  _body.clear();
  _responseHeaders.clear();
  _contentLength = 0;
  _haveContentLength = false;
  _bodyComplete = false;
  _callbackAborted = false;
  _aborted = false;

  if (shouldAbort && shouldAbort()) {
    _aborted = true;
    return -1;
  }

  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->network || !pd->network->http || _host.empty()) return -1;

  reapRetiredConnectionsImpl(pd, /*forceAll=*/false);

  const uint32_t effectiveTimeoutMs = std::min<uint32_t>(_timeoutMs, 15000U);
  const bool useSsl = (_scheme == "https");
  if (!ensureHostAccess(_host, _port, useSsl, effectiveTimeoutMs, shouldAbort, _aborted)) {
    ++gFailedRequests;
    return -1;
  }

  auto state = makeUniqueNoThrow<RequestCallbackState>();
  if (!state) {
    ++gFailedRequests;
    LOG_ERR("NET", "OOM allocating RequestCallbackState (%u bytes)",
            static_cast<unsigned>(sizeof(RequestCallbackState)));
    return -1;
  }
  const uint32_t reqId = ++gTotalRequests;
  state->reqId = reqId;
  state->host = _host;
  state->path = _path.empty() ? "/" : _path;
  state->readBuf.resize(kReadChunkSize);

  const char* reqMethod = method ? method : "GET";
  const unsigned long reqStartMs = millis();
  {
    const playdate_hal::HeapTelemetry heap = playdate_hal::getHeapTelemetry();
    LOG_INF("NET", "req #%u START %s %s://%s:%u%s wifi=%s timeout=%ums active=%u retired=%u heapLive=%u",
            static_cast<unsigned>(reqId), reqMethod, _scheme.c_str(), state->host.c_str(), static_cast<unsigned>(_port),
            state->path.c_str(), wifiStatusName(pd), static_cast<unsigned>(effectiveTimeoutMs),
            static_cast<unsigned>(gActiveConnections), static_cast<unsigned>(retiredConnections().size()),
            static_cast<unsigned>(heap.liveBytes));
  }

  HTTPConnection* conn = pd->network->http->newConnection(state->host.c_str(), static_cast<int>(_port), useSsl);
  if (!conn) {
    ++gFailedRequests;
    LOG_ERR("NET", "req #%u newConnection failed for %s:%u (ssl=%d, retired=%u)", static_cast<unsigned>(reqId),
            state->host.c_str(), static_cast<unsigned>(_port), useSsl ? 1 : 0,
            static_cast<unsigned>(retiredConnections().size()));
    return -1;
  }
  ++gActiveConnections;

  pd->network->http->setUserdata(conn, state.get());
  pd->network->http->setConnectTimeout(conn, static_cast<int>(effectiveTimeoutMs));
  pd->network->http->setReadTimeout(conn, 50);
  // Always disable keep-alive on single-request HTTPConnection instances so
  // chunked responses close immediately at end-of-body instead of hanging until timeout.
  pd->network->http->setKeepAlive(conn, false);
  pd->network->http->setHeaderReceivedCallback(conn, onHeaderReceived);
  pd->network->http->setHeadersReadCallback(conn, onHeadersRead);
  if (pd->network->http->setResponseCallback) {
    pd->network->http->setResponseCallback(conn, onResponse);
  }
  pd->network->http->setRequestCompleteCallback(conn, onRequestComplete);
  pd->network->http->setConnectionClosedCallback(conn, onConnectionClosed);

  if (!_userAgent.empty()) {
    state->headerBlob += "User-Agent: " + _userAgent + "\r\n";
  }
  if (!_authUser.empty()) {
    const std::string creds = _authUser + ":" + _authPass;
    state->headerBlob += "Authorization: Basic " + std::string(base64::encode(creds.c_str()).c_str()) + "\r\n";
  }
  bool hasConnectionHeader = false;
  for (const std::string& h : _headers) {
    int rangeStart = 0;
    int rangeEnd = 0;
    if (parseByteRange(h, rangeStart, rangeEnd)) {
      pd->network->http->setByteRange(conn, rangeStart, rangeEnd);
      continue;
    }
    if (h.size() >= 11 && strncasecmp(h.c_str(), "Connection:", 11) == 0) {
      hasConnectionHeader = true;
    }
    state->headerBlob += h;
  }
  if (!hasConnectionHeader) {
    state->headerBlob += "Connection: close\r\n";
  }

  if (payload && payloadLen > 0) {
    state->payloadBlob.assign(reinterpret_cast<const char*>(payload), payloadLen);
  }
  const char* payloadPtr = state->payloadBlob.empty() ? nullptr : state->payloadBlob.data();
  const size_t payloadSize = state->payloadBlob.size();

  const char* headersPtr = state->headerBlob.empty() ? nullptr : state->headerBlob.c_str();
  const size_t headersLen = state->headerBlob.size();
  PDNetErr err = NET_OK;
  if (std::strcmp(reqMethod, "GET") == 0 && payloadSize == 0) {
    err = pd->network->http->get(conn, state->path.c_str(), headersPtr, headersLen);
  } else if (std::strcmp(reqMethod, "POST") == 0) {
    err = pd->network->http->post(conn, state->path.c_str(), headersPtr, headersLen, payloadPtr, payloadSize);
  } else {
    err =
        pd->network->http->query(conn, reqMethod, state->path.c_str(), headersPtr, headersLen, payloadPtr, payloadSize);
  }

  bool connCleanedUp = false;
  auto cleanupConn = [&](const char* reason) {
    if (connCleanedUp) return;
    connCleanedUp = true;
    const PDNetErr finalErr = pd->network->http->getError ? pd->network->http->getError(conn) : NET_OK;
    const unsigned long elapsedMs = millis() - reqStartMs;
    const playdate_hal::HeapTelemetry heap = playdate_hal::getHeapTelemetry();
    LOG_INF("NET",
            "req #%u END reason=%s status=%d bytes=%u/%u complete=%d aborted=%d cbAbort=%d "
            "netErr=%d(%s) reqDone=%d closed=%d ms=%lu heapLive=%u stackPeak=%u",
            static_cast<unsigned>(reqId), reason, _status, static_cast<unsigned>(_downloaded),
            static_cast<unsigned>(_haveContentLength ? _contentLength : 0), _bodyComplete ? 1 : 0, _aborted ? 1 : 0,
            _callbackAborted ? 1 : 0, static_cast<int>(finalErr), netErrName(finalErr),
            (state && state->requestComplete) ? 1 : 0, (state && state->connectionClosed) ? 1 : 0, elapsedMs,
            static_cast<unsigned>(heap.liveBytes), static_cast<unsigned>(playdate_hal::getFiberStackPeakUsedBytes()));

    if (state) {
      state->retired = true;
      state->readBuf.clear();
      shrinkToFit(state->readBuf);
      state->headers.clear();
      shrinkToFit(state->headers);
      const bool hadRedirectHop = state->interceptedRedirectStatus > 0 || !state->firstRedirectLocation.empty();
      if (!state->connectionClosed || hadRedirectHop) {
        pd->network->http->close(conn);
        state->closeIssued = true;
        if (hadRedirectHop) {
          // Playdate OS closes redirected sockets asynchronously (and suppresses onConnectionClosed
          // when closed by caller); pump briefly so any internal redirect hop tears down before the
          // caller opens the next HTTPConnection.
          const unsigned long closeDeadline = millis() + 80U;
          while (static_cast<int32_t>(millis() - closeDeadline) < 0) {
            paceIdleNetworkWait(pd);
          }
        }
      }
    } else {
      pd->network->http->close(conn);
    }
    if (gActiveConnections > 0) {
      --gActiveConnections;
    }
    // Keep non-null static callbacks and `state` (host, path, headerBlob, payloadBlob)
    // alive inside RetiredConnection until Playdate OS has finished all async timeout/close
    // events for `conn`. Setting callbacks to nullptr or freeing `state` early causes
    // Playdate OS to jump to NULL or read freed buffers when its 5s HTTP timer fires.
    retiredConnections().push_back(RetiredConnection{conn, std::move(state), millis()});
  };

  if (err != NET_OK) {
    ++gFailedRequests;
    LOG_ERR("NET", "req #%u start failed: netErr=%d(%s)", static_cast<unsigned>(reqId), static_cast<int>(err),
            netErrName(err));
    cleanupConn("start_err");
    return -1;
  }

  unsigned long deadline = millis() + effectiveTimeoutMs;
  while (!state->headersRead && !state->redirectReady && !state->requestComplete && !state->connectionClosed) {
    if (shouldAbort && shouldAbort()) {
      _aborted = true;
      cleanupConn("hdr_abort");
      return -1;
    }
    if (!state->redirectReady && pd->network->http->getError) {
      const PDNetErr netErr = pd->network->http->getError(conn);
      if (netErr != NET_OK && netErr != NET_READ_BUSY && netErr != NET_BUSY && netErr != NET_CONNECTION_CLOSED) {
        ++gFailedRequests;
        LOG_ERR("NET", "req #%u header wait error: netErr=%d(%s)", static_cast<unsigned>(reqId),
                static_cast<int>(netErr), netErrName(netErr));
        cleanupConn("hdr_neterr");
        return -1;
      }
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ++gFailedRequests;
      LOG_ERR("NET", "req #%u header wait timeout after %ums", static_cast<unsigned>(reqId),
              static_cast<unsigned>(effectiveTimeoutMs));
      cleanupConn("hdr_timeout");
      return -1;
    }
    paceIdleNetworkWait(pd);
  }

  const int liveStatus = pd->network->http->getResponseStatus(conn);
  const std::string& redirectLoc =
      !state->interceptedRedirectLocation.empty() ? state->interceptedRedirectLocation : state->firstRedirectLocation;
  if ((!state->headersRead || liveStatus <= 0) && !redirectLoc.empty()) {
    _status = state->interceptedRedirectStatus > 0 ? state->interceptedRedirectStatus : 302;
    _responseHeaders.clear();
    _responseHeaders.push_back(Header{"location", redirectLoc});
    _contentLength = 0;
    _haveContentLength = false;
    _bodyComplete = true;
    LOG_INF("NET", "req #%u REDIRECT status=%d -> %s", static_cast<unsigned>(reqId), _status, redirectLoc.c_str());
    cleanupConn("redirect_intercept");
    return _status;
  }

  _status = liveStatus > 0 ? liveStatus : state->interceptedRedirectStatus;
  if (_status <= 0) {
    ++gFailedRequests;
    cleanupConn("no_status");
    return -1;
  }

  _responseHeaders.reserve(state->headers.size());
  for (const auto& kv : state->headers) {
    _responseHeaders.push_back(Header{kv.first, kv.second});
  }
  _contentLength = state->contentLength;
  _haveContentLength = state->haveContentLength;

  LOG_DBG("NET", "req #%u HEADERS status=%d contentLen=%u hasLen=%d hdrs=%u ms=%lu", static_cast<unsigned>(reqId),
          _status, static_cast<unsigned>(_contentLength), _haveContentLength ? 1 : 0,
          static_cast<unsigned>(_responseHeaders.size()), millis() - reqStartMs);

  const bool discardBody = _followRedirects > 0 && isRedirectStatus(_status);
  const DataCallback discard = [](const uint8_t*, size_t) { return true; };
  const DataCallback& bodySink = discardBody ? discard : onData;
  _downloaded = 0;
  _reportProgress = !discardBody && static_cast<bool>(_progress);

  const char* exitReason = "done";
  deadline = millis() + effectiveTimeoutMs;
  unsigned long lastDataMs = millis();
  for (;;) {
    if (shouldAbort && shouldAbort()) {
      _aborted = true;
      cleanupConn("body_abort");
      return _status;
    }

    if (state->bodyAvailable || state->requestComplete || state->connectionClosed) {
      const int avail = static_cast<int>(pd->network->http->getBytesAvailable(conn));
      if (avail > 0) {
        const size_t want =
            static_cast<size_t>(avail) < state->readBuf.size() ? static_cast<size_t>(avail) : state->readBuf.size();
        const int n = pd->network->http->read(conn, state->readBuf.data(), static_cast<unsigned int>(want));
        if (n > 0) {
          lastDataMs = millis();
          deadline = lastDataMs + effectiveTimeoutMs;
          if (bodySink && !bodySink(state->readBuf.data(), static_cast<size_t>(n))) {
            _callbackAborted = true;
            cleanupConn("sink_abort");
            return _status;
          }
          _downloaded += static_cast<size_t>(n);
          if (_reportProgress && !_progress(_downloaded, _haveContentLength ? _contentLength : 0)) {
            _callbackAborted = true;
            cleanupConn("progress_abort");
            return _status;
          }
          if (_haveContentLength && _downloaded >= _contentLength) {
            _bodyComplete = true;
            exitReason = "content_length";
            break;
          }
          continue;
        }
      }
    }

    const int availAfter = static_cast<int>(pd->network->http->getBytesAvailable(conn));
    if (availAfter <= 0) {
      if (_haveContentLength && _downloaded >= _contentLength) {
        _bodyComplete = true;
        exitReason = "content_length";
        break;
      }
      if (state->requestComplete || state->connectionClosed) {
        _bodyComplete = !_haveContentLength || (_downloaded >= _contentLength);
        exitReason = state->requestComplete ? "req_complete" : "conn_closed";
        break;
      }
      if (_haveContentLength && pd->network->http->getProgress) {
        int progRead = 0;
        int progTotal = 0;
        pd->network->http->getProgress(conn, &progRead, &progTotal);
        if (progTotal > 0 && progRead >= progTotal && _downloaded >= static_cast<size_t>(progTotal)) {
          _bodyComplete = true;
          exitReason = "progress_total";
          break;
        }
      }
      // For chunked responses without Content-Length where Playdate OS doesn't fire
      // requestComplete until socket timeout, finish once body bytes have arrived and
      // the stream has been idle for 600ms.
      if (!_haveContentLength && _downloaded > 0 && static_cast<int32_t>(millis() - lastDataMs) >= 600) {
        _bodyComplete = true;
        exitReason = "chunked_idle";
        break;
      }
    }

    if (pd->network->http->getError) {
      const PDNetErr netErr = pd->network->http->getError(conn);
      if (netErr == NET_CONNECTION_CLOSED) {
        _bodyComplete = !_haveContentLength || (_downloaded >= _contentLength);
        exitReason = "net_closed";
        break;
      }
      if (netErr != NET_OK && netErr != NET_READ_BUSY && netErr != NET_BUSY) {
        ++gFailedRequests;
        LOG_ERR("NET", "req #%u body read error: netErr=%d(%s) after %u bytes", static_cast<unsigned>(reqId),
                static_cast<int>(netErr), netErrName(netErr), static_cast<unsigned>(_downloaded));
        exitReason = "body_neterr";
        break;
      }
    }

    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ++gFailedRequests;
      LOG_ERR("NET", "req #%u body timeout after %u bytes", static_cast<unsigned>(reqId),
              static_cast<unsigned>(_downloaded));
      exitReason = "body_timeout";
      break;
    }

    paceIdleNetworkWait(pd);
  }

  cleanupConn(exitReason);
  return _status;
}

std::string SecureHttpClient::getHeader(const std::string& headerName) const {
  std::string normalized = headerName;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const auto found = std::find_if(_responseHeaders.begin(), _responseHeaders.end(),
                                  [&normalized](const Header& header) { return header.name == normalized; });
  return found == _responseHeaders.end() ? "" : found->value;
}

const std::string* SecureHttpClient::getUniqueHeader(const char* lowercaseName) const {
  if (!lowercaseName) return nullptr;
  const std::string* value = nullptr;
  for (const auto& header : _responseHeaders) {
    if (header.name != lowercaseName) continue;
    if (value) return nullptr;
    value = &header.value;
  }
  return value;
}

std::vector<std::pair<std::string, std::string>> SecureHttpClient::getHeaders() const {
  std::vector<std::pair<std::string, std::string>> out;
  out.reserve(_responseHeaders.size());
  std::transform(_responseHeaders.begin(), _responseHeaders.end(), std::back_inserter(out),
                 [](const Header& h) { return std::make_pair(h.name, h.value); });
  return out;
}

bool SecureHttpClient::resolveUrl(const std::string& baseUrl, const std::string& location, std::string& resolved) {
  if (location.find("://") != std::string::npos) {
    resolved = location;
    return true;
  }
  std::string scheme;
  std::string host;
  std::string path;
  uint16_t port = 0;
  if (!parseUrl(baseUrl, scheme, host, path, port)) return false;
  const std::string authority = hostHeaderFor(scheme, host, port);
  if (!location.empty() && location[0] == '/') {
    resolved = scheme + "://" + authority + location;
    return true;
  }
  std::string parent = path;
  const size_t slash = parent.rfind('/');
  parent = slash == std::string::npos ? "/" : parent.substr(0, slash + 1);
  resolved = scheme + "://" + authority + parent + location;
  return true;
}

bool SecureHttpClient::parseUrl(const std::string& url, std::string& scheme, std::string& host, std::string& path,
                                uint16_t& port) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return false;
  scheme = url.substr(0, schemeEnd);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  const std::string hostPort =
      pathStart == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, pathStart - hostStart);
  path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
  const size_t portSep = hostPort.rfind(':');
  if (portSep != std::string::npos) {
    host = hostPort.substr(0, portSep);
    port = static_cast<uint16_t>(std::atoi(hostPort.substr(portSep + 1).c_str()));
  } else {
    host = hostPort;
    port = scheme == "https" ? 443 : 80;
  }
  return !host.empty() && (scheme == "http" || scheme == "https");
}

std::string SecureHttpClient::hostHeaderFor(const std::string& scheme, const std::string& host, uint16_t port) {
  const uint16_t defaultPort = scheme == "https" ? 443 : 80;
  if (port == 0 || port == defaultPort) return host;
  return host + ":" + std::to_string(port);
}

}  // namespace freeink

namespace playdate_hal {

HttpTelemetry getHttpTelemetry() {
  HttpTelemetry out;
  out.totalRequests = freeink::gTotalRequests;
  out.activeConnections = freeink::gActiveConnections;
  out.retiredConnections = static_cast<uint32_t>(freeink::retiredConnections().size());
  out.reapedConnections = freeink::gReapedConnections;
  out.failedRequests = freeink::gFailedRequests;
  return out;
}

void reapRetiredHttpConnections(bool forceAll) { freeink::reapRetiredConnectionsImpl(pd(), forceAll); }

void releasePendingHttpAccessContexts() {
  freeink::abandonedAccessContexts().clear();
  freeink::allowedHosts().clear();
}

}  // namespace playdate_hal
