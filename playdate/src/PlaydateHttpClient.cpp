#include <SecureHttpClient.h>

#include <Arduino.h>
#include <PlaydateRuntime.h>
#include <base64.h>

extern "C" {
#include <pd_api.h>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace freeink {

namespace {

constexpr size_t kReadChunkSize = 2048;

struct AccessWaitState {
  volatile bool decided = false;
  volatile bool allowed = false;
};

void onAccessResponse(bool allowed, void* userdata) {
  auto* state = static_cast<AccessWaitState*>(userdata);
  if (!state) return;
  state->allowed = allowed;
  state->decided = true;
}

struct RequestCallbackState {
  std::vector<std::pair<std::string, std::string>> headers;
  size_t contentLength = 0;
  bool haveContentLength = false;
  volatile bool headersRead = false;
  volatile bool requestComplete = false;
  volatile bool connectionClosed = false;
};

void onHeaderReceived(HTTPConnection* conn, const char* key, const char* value) {
  if (!conn || !key || !value) return;
  auto* state = static_cast<RequestCallbackState*>(playdate_hal::pd()->network->http->getUserdata(conn));
  if (!state || state->headers.size() >= 64) return;

  std::string name(key);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string val(value);
  while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
  while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();

  state->headers.emplace_back(name, val);
  if (name == "content-length") {
    state->contentLength = static_cast<size_t>(std::strtoul(val.c_str(), nullptr, 10));
    state->haveContentLength = true;
  }
}

void onHeadersRead(HTTPConnection* conn) {
  if (!conn) return;
  auto* state = static_cast<RequestCallbackState*>(playdate_hal::pd()->network->http->getUserdata(conn));
  if (state) state->headersRead = true;
}

void onRequestComplete(HTTPConnection* conn) {
  if (!conn) return;
  auto* state = static_cast<RequestCallbackState*>(playdate_hal::pd()->network->http->getUserdata(conn));
  if (state) {
    state->headersRead = true;
    state->requestComplete = true;
  }
}

void onConnectionClosed(HTTPConnection* conn) {
  if (!conn) return;
  auto* state = static_cast<RequestCallbackState*>(playdate_hal::pd()->network->http->getUserdata(conn));
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
  if (!pd || !pd->network || !pd->network->http) return false;

  static AccessWaitState waitState;
  waitState.decided = false;
  waitState.allowed = false;
  const accessReply status = pd->network->http->requestAccess(
      host.c_str(), static_cast<int>(port), useSsl, "CrossPoint network access", onAccessResponse, &waitState);
  if (status == kAccessAllow) return true;
  if (status == kAccessDeny) return false;

  const unsigned long deadline = millis() + timeoutMs;
  while (!waitState.decided) {
    if (shouldAbort && shouldAbort()) {
      abortedOut = true;
      return false;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) return false;
    playdate_hal::pumpMainLoop();
    delay(5);
  }
  return waitState.allowed;
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
  return ensureHostAccess(_host, _port, _scheme == "https", _timeoutMs, shouldAbort, _aborted);
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

  const bool useSsl = (_scheme == "https");
  if (!ensureHostAccess(_host, _port, useSsl, _timeoutMs, shouldAbort, _aborted)) return -1;

  HTTPConnection* conn = pd->network->http->newConnection(_host.c_str(), static_cast<int>(_port), useSsl);
  if (!conn) return -1;

  RequestCallbackState state;
  pd->network->http->setUserdata(conn, &state);
  pd->network->http->setConnectTimeout(conn, static_cast<int>(_timeoutMs));
  pd->network->http->setReadTimeout(conn, static_cast<int>(_timeoutMs));
  pd->network->http->setKeepAlive(conn, _reuse);
  pd->network->http->setHeaderReceivedCallback(conn, onHeaderReceived);
  pd->network->http->setHeadersReadCallback(conn, onHeadersRead);
  pd->network->http->setRequestCompleteCallback(conn, onRequestComplete);
  pd->network->http->setConnectionClosedCallback(conn, onConnectionClosed);

  std::string headerBlob = "User-Agent: " + _userAgent + "\r\n";
  if (!_authUser.empty()) {
    const std::string creds = _authUser + ":" + _authPass;
    headerBlob += "Authorization: Basic " + std::string(base64::encode(creds.c_str()).c_str()) + "\r\n";
  }
  for (const std::string& h : _headers) {
    int rangeStart = 0;
    int rangeEnd = 0;
    if (parseByteRange(h, rangeStart, rangeEnd)) {
      pd->network->http->setByteRange(conn, rangeStart, rangeEnd);
    }
    headerBlob += h;
  }

  const char* reqMethod = method ? method : "GET";
  PDNetErr err = NET_OK;
  if (std::strcmp(reqMethod, "GET") == 0 && (!payload || payloadLen == 0)) {
    err = pd->network->http->get(conn, _path.c_str(), headerBlob.c_str(), headerBlob.size());
  } else if (std::strcmp(reqMethod, "POST") == 0) {
    err = pd->network->http->post(conn, _path.c_str(), headerBlob.c_str(), headerBlob.size(),
                                  reinterpret_cast<const char*>(payload), payloadLen);
  } else {
    err = pd->network->http->query(conn, reqMethod, _path.c_str(), headerBlob.c_str(), headerBlob.size(),
                                   reinterpret_cast<const char*>(payload), payloadLen);
  }

  auto cleanupConn = [&]() {
    pd->network->http->setUserdata(conn, nullptr);
    pd->network->http->close(conn);
    pd->network->http->release(conn);
  };

  if (err != NET_OK) {
    cleanupConn();
    return -1;
  }

  unsigned long deadline = millis() + _timeoutMs;
  while (!state.headersRead && !state.requestComplete && !state.connectionClosed) {
    if (shouldAbort && shouldAbort()) {
      _aborted = true;
      cleanupConn();
      return -1;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      cleanupConn();
      return -1;
    }
    playdate_hal::pumpMainLoop();
    delay(2);
  }

  _status = pd->network->http->getResponseStatus(conn);
  if (_status <= 0) {
    cleanupConn();
    return -1;
  }

  _responseHeaders.reserve(state.headers.size());
  for (const auto& kv : state.headers) {
    _responseHeaders.push_back(Header{kv.first, kv.second});
  }
  _contentLength = state.contentLength;
  _haveContentLength = state.haveContentLength;

  const bool discardBody = _followRedirects > 0 && isRedirectStatus(_status);
  const DataCallback discard = [](const uint8_t*, size_t) { return true; };
  const DataCallback& bodySink = discardBody ? discard : onData;
  _downloaded = 0;
  _reportProgress = !discardBody && static_cast<bool>(_progress);

  uint8_t buf[kReadChunkSize];
  deadline = millis() + _timeoutMs;
  for (;;) {
    if (shouldAbort && shouldAbort()) {
      _aborted = true;
      cleanupConn();
      return _status;
    }

    const size_t avail = pd->network->http->getBytesAvailable(conn);
    if (avail > 0) {
      const size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
      const int n = pd->network->http->read(conn, buf, static_cast<unsigned int>(want));
      if (n > 0) {
        deadline = millis() + _timeoutMs;
        if (bodySink && !bodySink(buf, static_cast<size_t>(n))) {
          _callbackAborted = true;
          cleanupConn();
          return _status;
        }
        _downloaded += static_cast<size_t>(n);
        if (_reportProgress && !_progress(_downloaded, _haveContentLength ? _contentLength : 0)) {
          _callbackAborted = true;
          cleanupConn();
          return _status;
        }
        continue;
      }
    }

    if (state.requestComplete || state.connectionClosed) {
      if (pd->network->http->getBytesAvailable(conn) == 0) {
        _bodyComplete = !_haveContentLength || (_downloaded >= _contentLength) || state.requestComplete;
        break;
      }
    }

    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }

    playdate_hal::pumpMainLoop();
    delay(2);
  }

  cleanupConn();
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
