#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace freeink {

class SecureHttpClient {
 public:
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;
  using AbortCallback = std::function<bool()>;
  using ProgressCallback = std::function<bool(size_t downloaded, size_t total)>;

  SecureHttpClient() = default;
  ~SecureHttpClient();

  SecureHttpClient(const SecureHttpClient&) = delete;
  SecureHttpClient& operator=(const SecureHttpClient&) = delete;

  void setInsecure() { _insecure = true; }
  void setReserveFullRecord(bool reserve) { _reserveFullRecord = reserve; }
  void setCACert(const char* rootCA) {
    _rootCA = rootCA;
    _insecure = false;
  }
  void setTimeout(uint32_t ms) { _timeoutMs = ms; }
  void setProgressCallback(const ProgressCallback& cb) { _progress = cb; }
  void setFollowRedirects(int maxHops) { _followRedirects = maxHops > 0 ? maxHops : 0; }
  void setAllowRedirectDowngrade(bool allow) { _allowRedirectDowngrade = allow; }
  void setBasicAuth(const std::string& user, const std::string& pass) {
    _authUser = user;
    _authPass = pass;
  }
  void clearBasicAuth() {
    _authUser.clear();
    _authPass.clear();
  }
  void setUserAgent(const std::string& ua) { _userAgent = ua; }
  void setReuse(bool reuse) { _reuse = reuse; }

  bool begin(const std::string& url);
  void end();
  bool connect(const AbortCallback& shouldAbort = nullptr);
  void addHeader(const std::string& name, const std::string& value);

  int GET() { return sendRequest("GET", nullptr, 0); }
  int GET(const DataCallback& onData, const AbortCallback& shouldAbort = nullptr) {
    return sendRequest("GET", nullptr, 0, onData, shouldAbort);
  }
  int POST(const std::string& payload) {
    return sendRequest("POST", reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  }
  int sendRequest(const char* method, const std::string& payload) {
    return sendRequest(method, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  }
  int sendRequest(const char* method, const uint8_t* payload, size_t payloadLen);
  int sendRequest(const char* method, const uint8_t* payload, size_t payloadLen, const DataCallback& onData,
                  const AbortCallback& shouldAbort = nullptr);
  int sendRequestOnce(const char* method, const uint8_t* payload, size_t payloadLen, const DataCallback& onData,
                      const AbortCallback& shouldAbort = nullptr);

  const std::string& getString() const { return _body; }
  int getStatus() const { return _status; }
  int getSize() const { return static_cast<int>(_body.size()); }
  bool responseComplete() const { return _bodyComplete; }
  bool callbackAborted() const { return _callbackAborted; }
  bool aborted() const { return _aborted; }
  bool hasContentLength() const { return _haveContentLength; }
  size_t getContentLength() const { return _contentLength; }

  std::string getHeader(const std::string& headerName) const;
  const std::string* getUniqueHeader(const char* lowercaseName) const;
  std::vector<std::pair<std::string, std::string>> getHeaders() const;

  static bool resolveUrl(const std::string& baseUrl, const std::string& location, std::string& resolved);
  static bool tls13Available() { return true; }

 private:
  struct Header {
    std::string name;
    std::string value;
  };

  static bool isRedirectStatus(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
  }
  static bool parseUrl(const std::string& url, std::string& scheme, std::string& host, std::string& path,
                       uint16_t& port);
  static std::string hostHeaderFor(const std::string& scheme, const std::string& host, uint16_t port);
  std::string hostHeader() const { return hostHeaderFor(_scheme, _host, _port); }

  bool _reserveFullRecord = false;
  bool _reuse = true;
  std::string _scheme;
  std::string _host;
  std::string _path;
  std::string _body;
  std::vector<Header> _responseHeaders;
  uint16_t _port = 0;
  int _status = 0;
  size_t _contentLength = 0;
  const char* _rootCA = nullptr;
  std::string _userAgent = "CrossPoint-Playdate";
  std::string _authUser;
  std::string _authPass;
  bool _insecure = false;
  int _followRedirects = 0;
  bool _allowRedirectDowngrade = false;
  ProgressCallback _progress;
  size_t _downloaded = 0;
  bool _reportProgress = false;
  bool _haveContentLength = false;
  bool _bodyComplete = false;
  bool _callbackAborted = false;
  bool _aborted = false;
  uint32_t _timeoutMs = 15000;
  std::vector<std::string> _headers;
};

}  // namespace freeink
