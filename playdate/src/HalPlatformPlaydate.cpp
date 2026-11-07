#include <Arduino.h>
#include <BoardConfig.h>
#include <BookKey.h>
#include <DeviceSecret.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <PlaydateRuntime.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "../../src/SilentRestart.h"

namespace {

PlaydateAPI* gPlaydateApi = nullptr;
std::string gSerialLineBuffer;

constexpr int64_t kPlaydateEpochToUnixOffset = 946684800LL;

}  // namespace

namespace playdate_hal {

PlaydateAPI* pd() { return gPlaydateApi; }

void setPlaydateAPI(PlaydateAPI* api) {
  gPlaydateApi = api;
  BoardConfig::ACTIVE.name = "Panic Playdate";
  BoardConfig::ACTIVE.displayWidth = HalDisplay::DISPLAY_WIDTH;
  BoardConfig::ACTIVE.displayHeight = HalDisplay::DISPLAY_HEIGHT;
  BoardConfig::ACTIVE.viewableInsets = {0, 0, 0, 0};
}

}  // namespace playdate_hal

unsigned long millis() {
  if (gPlaydateApi && gPlaydateApi->system) {
    return gPlaydateApi->system->getCurrentTimeMilliseconds();
  }
  return 0;
}

unsigned long micros() { return millis() * 1000UL; }

void delay(unsigned long ms) {
  if (ms == 0) {
    playdate_hal::pumpMainLoop();
    return;
  }
  const unsigned long start = millis();
  while (millis() - start < ms) {
    playdate_hal::pumpMainLoop();
    if (gPlaydateApi && gPlaydateApi->system && gPlaydateApi->system->delay) {
      const unsigned long remaining = ms - (millis() - start);
      gPlaydateApi->system->delay(static_cast<uint32_t>(std::min<unsigned long>(remaining, 10UL)));
    } else {
      break;
    }
  }
}

void delayMicroseconds(unsigned int us) { (void)us; }

void yield() { playdate_hal::pumpMainLoop(); }

#ifdef Serial
#undef Serial
#endif
HardwareSerial Serial;

size_t HardwareSerial::write(uint8_t b) { return write(&b, 1); }

size_t HardwareSerial::write(const uint8_t* buffer, size_t size) {
  if (!buffer || size == 0) return 0;
  for (size_t i = 0; i < size; ++i) {
    const char c = static_cast<char>(buffer[i]);
    if (c == '\r') continue;
    if (c == '\n') {
      if (gPlaydateApi && gPlaydateApi->system) {
        gPlaydateApi->system->logToConsole("%s", gSerialLineBuffer.c_str());
      }
      gSerialLineBuffer.clear();
    } else {
      gSerialLineBuffer.push_back(c);
    }
  }
  return size;
}

EspClass ESP;

void EspClass::restart() {
  if (gPlaydateApi && gPlaydateApi->system && gPlaydateApi->system->restartGame) {
    gPlaydateApi->system->restartGame(nullptr);
  }
}

uint32_t EspClass::getFreeHeap() const { return 12 * 1024 * 1024; }

uint32_t EspClass::getHeapSize() const { return 16 * 1024 * 1024; }

uint32_t EspClass::getMinFreeHeap() const { return 8 * 1024 * 1024; }

uint32_t EspClass::getMaxAllocHeap() const { return 8 * 1024 * 1024; }

const char* EspClass::getSdkVersion() const { return "PlaydateSDK-3.1.2"; }

const char* EspClass::getChipModel() const { return "Playdate ARM Cortex-M7"; }

namespace HalSystem {

void begin() {
  BoardConfig::ACTIVE.name = "Panic Playdate";
  BoardConfig::ACTIVE.displayWidth = HalDisplay::DISPLAY_WIDTH;
  BoardConfig::ACTIVE.displayHeight = HalDisplay::DISPLAY_HEIGHT;
  BoardConfig::ACTIVE.viewableInsets = {0, 0, 0, 0};
}

void checkPanic() {}

void clearPanic() {}

std::string getPanicInfo(bool full) {
  (void)full;
  return "";
}

bool isRebootFromPanic() { return false; }

}  // namespace HalSystem

void HalMemory::PsramDeleter::operator()(uint8_t* buffer) const { std::free(buffer); }

HalMemory::PsramBuffer HalMemory::allocatePsram(size_t bytes) {
  (void)bytes;
  return PsramBuffer(nullptr);
}

HalMemory::HeapStats HalMemory::getDefaultHeap() { return getInternalHeap(); }

HalMemory::HeapStats HalMemory::getInternalHeap() {
  return HeapStats{
      .freeBytes = 12 * 1024 * 1024,
      .totalBytes = 16 * 1024 * 1024,
      .minFreeBytes = 8 * 1024 * 1024,
      .largestBlockBytes = 8 * 1024 * 1024,
  };
}

HalMemory::HeapStats HalMemory::getPsramHeap() { return HeapStats{0, 0, 0, 0}; }

HalPowerManager powerManager;

void HalPowerManager::begin() {
  normalFreq = 168;
  isLowPower = false;
  _batteryCachedPercent = static_cast<int>(getBatteryPercentage());
  _batteryLastPollMs = millis();
}

void HalPowerManager::setPowerSaving(bool enabled) { isLowPower = enabled; }

void HalPowerManager::startDeepSleep(HalGPIO& gpioRef, bool wakeOnHeldButton) const {
  (void)gpioRef;
  (void)wakeOnHeldButton;
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  if (gPlaydateApi && gPlaydateApi->system && gPlaydateApi->system->getBatteryPercentage) {
    const float pct = gPlaydateApi->system->getBatteryPercentage();
    const int clamped = std::clamp(static_cast<int>(std::lround(pct)), 0, 100);
    _batteryCachedPercent = clamped;
    _batteryLastPollMs = millis();
    return static_cast<uint16_t>(clamped);
  }
  return 100;
}

HalPowerManager::Lock::Lock() { valid = true; }

HalPowerManager::Lock::~Lock() = default;

HalClock halClock;

void HalClock::begin() { _available = true; }

void HalClock::setTimezone(const char* posixTz) {
  (void)posixTz;
  _hasCachedTime = false;
}

bool HalClock::localTime(struct tm& out) const {
  if (!gPlaydateApi || !gPlaydateApi->system || !gPlaydateApi->system->getSecondsSinceEpoch ||
      !gPlaydateApi->system->convertEpochToDateTime) {
    return false;
  }
  const uint32_t pdEpoch = gPlaydateApi->system->getSecondsSinceEpoch(nullptr);
  const int32_t tzOffset = gPlaydateApi->system->getTimezoneOffset ? gPlaydateApi->system->getTimezoneOffset() : 0;
  const uint32_t localPdEpoch =
      (tzOffset >= 0 || static_cast<uint32_t>(-tzOffset) <= pdEpoch)
          ? static_cast<uint32_t>(static_cast<int64_t>(pdEpoch) + tzOffset)
          : pdEpoch;

  PDDateTime dt{};
  gPlaydateApi->system->convertEpochToDateTime(localPdEpoch, &dt);
  out = {};
  out.tm_year = static_cast<int>(dt.year) - 1900;
  out.tm_mon = static_cast<int>(dt.month) - 1;
  out.tm_mday = static_cast<int>(dt.day);
  out.tm_hour = static_cast<int>(dt.hour);
  out.tm_min = static_cast<int>(dt.minute);
  out.tm_sec = static_cast<int>(dt.second);
  out.tm_wday = (dt.weekday == 7) ? 0 : static_cast<int>(dt.weekday);
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  struct tm t{};
  if (!localTime(t)) return false;
  hour = static_cast<uint8_t>(t.tm_hour);
  minute = static_cast<uint8_t>(t.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, bool use12Hour) const {
  if (!buf || bufSize == 0) return false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  if (!getTime(hour, minute)) {
    buf[0] = '\0';
    return false;
  }
  if (!use12Hour) {
    if (bufSize < 6) {
      buf[0] = '\0';
      return false;
    }
    std::snprintf(buf, bufSize, "%02u:%02u", hour, minute);
    return true;
  }
  if (bufSize < 9) {
    buf[0] = '\0';
    return false;
  }
  const char* suffix = (hour >= 12) ? "PM" : "AM";
  const uint8_t hour12 = (hour % 12 == 0) ? 12 : (hour % 12);
  std::snprintf(buf, bufSize, "%u:%02u %s", hour12, minute, suffix);
  return true;
}

bool HalClock::syncFromNTP() { return true; }

HalTiltSensor halTiltSensor;

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  gx = 0.0f;
  gy = 0.0f;
  gz = 0.0f;
  return false;
}

void HalTiltSensor::begin() { _available = false; }

bool HalTiltSensor::wake() { return false; }

bool HalTiltSensor::deepSleep() { return true; }

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  (void)mode;
  (void)orientation;
  (void)inReader;
}

bool HalTiltSensor::wasTiltedForward() { return false; }

bool HalTiltSensor::wasTiltedBack() { return false; }

bool HalTiltSensor::hadActivity() { return false; }

void HalTiltSensor::clearPendingEvents() {}

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(uint8_t brightness, uint8_t warmth, bool on) {
  lastBrightness = brightness;
  (void)warmth;
  lit = on;
}

void HalFrontlight::setBrightness(uint8_t percent) { lastBrightness = percent; }

void HalFrontlight::setWarmth(uint8_t warmPercent) { (void)warmPercent; }

void HalFrontlight::setOn(bool on) { lit = on; }

namespace trustedtime {

void init() {}

void adopt(int64_t epoch) { (void)epoch; }

void note() {}

void startSync() {}

bool syncNow(uint32_t timeoutMs) {
  (void)timeoutMs;
  return true;
}

int64_t trustedNow() {
  if (gPlaydateApi && gPlaydateApi->system) {
    return static_cast<int64_t>(gPlaydateApi->system->getSecondsSinceEpoch(nullptr)) + kPlaydateEpochToUnixOffset;
  }
  return 1767225600LL;
}

}  // namespace trustedtime

bool deviceSecret(uint8_t (&out)[32]) {
  static constexpr char kSecretPath[] = "/.crosspoint/device_secret.bin";
  if (Storage.exists(kSecretPath)) {
    HalFile f = Storage.open(kSecretPath, O_RDONLY);
    if (f && f.read(out, sizeof(out)) == static_cast<int>(sizeof(out))) {
      return true;
    }
  }
  for (size_t i = 0; i < sizeof(out); ++i) {
    out[i] = static_cast<uint8_t>((0x50U + i * 17U) & 0xFFU);
  }
  Storage.mkdir("/.crosspoint");
  HalFile wf = Storage.open(kSecretPath, O_RDWR | O_CREAT | O_TRUNC);
  if (wf) {
    wf.write(out, sizeof(out));
    wf.close();
  }
  return true;
}

namespace bookkey {

bool write(const std::string& bookPath, const uint8_t* key, size_t keyLen, int64_t expiresAt) {
  if (!key || (keyLen != 16 && keyLen != 32)) return false;
  const std::string keyPath = bookPath + ".key";
  HalFile f = Storage.open(keyPath.c_str(), O_RDWR | O_CREAT | O_TRUNC);
  if (!f) return false;
  const uint8_t lenByte = static_cast<uint8_t>(keyLen);
  f.write(&lenByte, 1);
  f.write(reinterpret_cast<const uint8_t*>(&expiresAt), sizeof(expiresAt));
  f.write(key, keyLen);
  f.close();
  return true;
}

size_t read(const std::string& bookPath, uint8_t (&key)[MAX_KEY_LEN], int64_t* expiresAt) {
  const std::string keyPath = bookPath + ".key";
  if (!Storage.exists(keyPath.c_str())) return 0;
  HalFile f = Storage.open(keyPath.c_str(), O_RDONLY);
  if (!f) return 0;
  uint8_t lenByte = 0;
  int64_t storedExpiry = 0;
  if (f.read(&lenByte, 1) != 1 || (lenByte != 16 && lenByte != 32)) return 0;
  if (f.read(&storedExpiry, sizeof(storedExpiry)) != static_cast<int>(sizeof(storedExpiry))) return 0;
  if (f.read(key, lenByte) != static_cast<int>(lenByte)) return 0;
  if (expiresAt) *expiresAt = storedExpiry;
  return lenByte;
}

}  // namespace bookkey
