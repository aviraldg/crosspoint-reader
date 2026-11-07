#pragma once

#include <cctype>
#include <charconv>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

#include "SecureHttpClient.h"

namespace freeink {

struct FetchOptions {
  size_t startOffset = 0;
  bool redirectToHttp = false;
};

struct FetchSink {
  std::function<bool(const uint8_t* data, size_t len)> write;
  std::function<bool()> rewind;
  std::function<void(size_t bytes, size_t total)> progress;
};

struct FetchResult {
  int status = 0;
  size_t bytes = 0;
  size_t total = 0;
  bool complete = false;
  bool stopped = false;
  bool aborted = false;
};

inline std::string fetchOrigin(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};
  std::string scheme = url.substr(0, schemeEnd);
  const size_t hostStart = schemeEnd + 3;
  const size_t hostEnd = url.find_first_of("/?#", hostStart);
  std::string authority = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) authority.erase(0, at + 1);
  for (auto* part : {&scheme, &authority}) {
    for (char& c : *part) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  const size_t bracket = authority.rfind(']');
  if (authority.find(':', bracket == std::string::npos ? 0 : bracket) == std::string::npos) {
    authority += scheme == "https" ? ":443" : ":80";
  }
  return scheme + "://" + authority;
}

inline bool parseFetchContentRange(std::string_view value, size_t& first, size_t& last, size_t& total) {
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  if (value.substr(0, 6) != "bytes ") return false;
  const char* cursor = value.data() + 6;
  const char* end = value.data() + value.size();
  const auto number = [&](size_t& out, const char separator) {
    const auto parsed = std::from_chars(cursor, end, out);
    if (parsed.ec != std::errc{}) return false;
    cursor = parsed.ptr;
    if (separator == '\0') return cursor == end;
    if (cursor == end || *cursor != separator) return false;
    ++cursor;
    return true;
  };
  return number(first, '-') && number(last, '/') && number(total, '\0') && first <= last && last < total;
}

inline FetchResult fetchResumable(const std::string& startUrl, const FetchOptions& options,
                                  const std::function<void(SecureHttpClient&, bool sameOrigin)>& configure,
                                  const FetchSink& sink, const SecureHttpClient::AbortCallback& shouldAbort = nullptr) {
  static constexpr int kMaxRedirects = 5;
  static constexpr int kMaxStalled = 3;
  static constexpr int kMaxAttempts = 20;
  FetchResult result;
  result.bytes = options.startOffset;
  std::string url = startUrl;
  const std::string startOrigin = fetchOrigin(startUrl);
  int redirects = 0;
  int stalled = 0;
  for (int attempt = 0; attempt < kMaxAttempts && stalled < kMaxStalled; ++attempt) {
    SecureHttpClient http;
    if (!http.begin(url)) {
      result.status = -1;
      return result;
    }
    http.setReserveFullRecord(true);
    if (configure) configure(http, fetchOrigin(url) == startOrigin);
    size_t attemptStart = result.bytes;
    const bool resuming = attemptStart > 0;
    if (resuming) http.addHeader("Range", "bytes=" + std::to_string(attemptStart) + "-");

    bool firstChunk = true;
    bool invalidResponse = false;
    size_t responseEnd = 0;
    const auto beginResponse = [&] {
      firstChunk = false;
      const int status = http.getStatus();
      if (status == 206) {
        size_t first, last, total;
        if (!parseFetchContentRange(http.getHeader("content-range"), first, last, total) || first != attemptStart ||
            (result.total != 0 && result.total != total) ||
            (http.hasContentLength() && http.getContentLength() != last - first + 1)) {
          return false;
        }
        responseEnd = last + 1;
        result.total = total;
      } else {
        if (resuming) {
          if (status != 200) return false;
          if (!sink.rewind || !sink.rewind()) {
            result.stopped = true;
            return false;
          }
          result.bytes = attemptStart = 0;
        }
        result.total = http.hasContentLength() ? http.getContentLength() : 0;
        responseEnd = result.total;
      }
      return true;
    };
    result.status = http.GET(
        [&](const uint8_t* data, size_t len) {
          const int status = http.getStatus();
          if (status < 200 || status >= 300) return true;
          if ((firstChunk && !beginResponse()) || len > std::numeric_limits<size_t>::max() - result.bytes ||
              (responseEnd != 0 && (result.bytes > responseEnd || len > responseEnd - result.bytes))) {
            invalidResponse = true;
            return false;
          }
          if (!sink.write(data, len)) {
            result.stopped = true;
            return false;
          }
          result.bytes += len;
          if (sink.progress) sink.progress(result.bytes, result.total);
          return true;
        },
        shouldAbort);

    if (http.aborted()) {
      result.aborted = true;
      return result;
    }
    const int status = result.status;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      const std::string location = http.getHeader("location");
      if (redirects++ >= kMaxRedirects || location.empty() || !SecureHttpClient::resolveUrl(url, location, url)) {
        return result;
      }
      if (options.redirectToHttp && url.rfind("https://", 0) == 0) url.replace(0, 8, "http://");
      --attempt;
      continue;
    }
    if (invalidResponse || result.stopped || status < 200 || status >= 300) return result;
    if (firstChunk && !beginResponse()) return result;
    if (http.responseComplete()) {
      if (responseEnd != 0 && result.bytes != responseEnd) return result;
      if (status != 206 || result.bytes == result.total) {
        result.complete = true;
        return result;
      }
    }
    stalled = result.bytes > attemptStart ? 0 : stalled + 1;
  }
  return result;
}

}  // namespace freeink
