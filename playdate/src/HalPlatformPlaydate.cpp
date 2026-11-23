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
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>

#if defined(TARGET_SIMULATOR) && defined(__GLIBC__)
#include <malloc.h>
#endif

#include "../../src/SilentRestart.h"

namespace {

PlaydateAPI* gPlaydateApi = nullptr;
constexpr size_t kSerialLineBufCap = 512;
char gSerialLineBuf[kSerialLineBufCap];
size_t gSerialLineLen = 0;

// Matches HEAP_SIZE in PlaydateSDK/C_API/buildsupport/playdate.cmake.
constexpr size_t kPlaydateHeapBudgetBytes = 8388208U;

constexpr int64_t kPlaydateEpochToUnixOffset = 946684800LL;

constexpr float kRefreshRateUncapped = 0.0f;
constexpr float kRefreshRateInteractive = 30.0f;
constexpr float kRefreshRateActiveReader = 15.0f;
constexpr float kRefreshRateIdle = 8.0f;

bool gSerialFastMode = false;
bool gSkipLoopDelay = false;
bool gForegroundReader = false;
bool gPowerSavingEnabled = false;
float gAppliedRefreshRate = -1.0f;

#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
// 8-byte header preserves ARM Cortex-M7 AAPCS 8-byte alignment for double/int64/VFP.
struct alignas(8) HeapBlockHeader {
  uint32_t magic;
  uint32_t size;
};
static_assert(sizeof(HeapBlockHeader) == 8, "HeapBlockHeader must stay 8 bytes for AAPCS alignment");

constexpr uint32_t kHeapBlockMagic = 0xC055A110U;
constexpr uint32_t kHeapFreedMagic = 0xDEADFA11U;

size_t gHeapLiveBytes = 0;
size_t gHeapPeakBytes = 0;
uint32_t gHeapLiveBlocks = 0;
uint32_t gHeapTotalAllocs = 0;
uint32_t gHeapFailedAllocs = 0;
uint32_t gHeapBadFrees = 0;

void logHeapFault(const char* kind, unsigned requestedOrAddr) {
  if (gPlaydateApi && gPlaydateApi->system && gPlaydateApi->system->logToConsole) {
    gPlaydateApi->system->logToConsole("[MEM] %s arg=%u live=%u peak=%u blocks=%u fails=%u", kind, requestedOrAddr,
                                       static_cast<unsigned>(gHeapLiveBytes), static_cast<unsigned>(gHeapPeakBytes),
                                       static_cast<unsigned>(gHeapLiveBlocks),
                                       static_cast<unsigned>(gHeapFailedAllocs));
  }
}
#else
size_t gSimBaselineBytes = 0;
size_t gSimPeakBytes = 0;

size_t readSimulatorRawAllocatedBytes() {
#if defined(TARGET_SIMULATOR) && defined(__GLIBC__)
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)
  const struct mallinfo2 mi = mallinfo2();
  return static_cast<size_t>(mi.uordblks);
#else
  const struct mallinfo mi = mallinfo();
  return static_cast<size_t>(static_cast<unsigned int>(mi.uordblks));
#endif
#else
  return 0;
#endif
}
#endif

}  // namespace

#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
extern "C" {

struct _reent;
void* __real__malloc_r(struct _reent* reent, size_t nbytes);
void* __real__realloc_r(struct _reent* reent, void* ptr, size_t nbytes);
void __real__free_r(struct _reent* reent, void* ptr);

void* __wrap__malloc_r(struct _reent* reent, size_t nbytes) {
  const size_t payload = nbytes == 0 ? 1U : nbytes;
  if (payload > UINT32_MAX - sizeof(HeapBlockHeader)) {
    ++gHeapFailedAllocs;
    logHeapFault("OOM_OVERFLOW", static_cast<unsigned>(nbytes));
    return nullptr;
  }
  auto* hdr = static_cast<HeapBlockHeader*>(__real__malloc_r(reent, sizeof(HeapBlockHeader) + payload));
  if (!hdr) {
    ++gHeapFailedAllocs;
    logHeapFault("OOM_MALLOC", static_cast<unsigned>(nbytes));
    return nullptr;
  }
  hdr->magic = kHeapBlockMagic;
  hdr->size = static_cast<uint32_t>(payload);
  gHeapLiveBytes += payload;
  if (gHeapLiveBytes > gHeapPeakBytes) {
    gHeapPeakBytes = gHeapLiveBytes;
  }
  ++gHeapLiveBlocks;
  ++gHeapTotalAllocs;
  return hdr + 1;
}

void __wrap__free_r(struct _reent* reent, void* ptr) {
  if (!ptr) return;
  auto* hdr = static_cast<HeapBlockHeader*>(ptr) - 1;
  if (hdr->magic != kHeapBlockMagic) {
    ++gHeapBadFrees;
    logHeapFault(hdr->magic == kHeapFreedMagic ? "DOUBLE_FREE" : "BAD_FREE",
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(ptr)));
    return;
  }
  const size_t oldSize = hdr->size;
  hdr->magic = kHeapFreedMagic;
  hdr->size = 0;
  gHeapLiveBytes = (gHeapLiveBytes >= oldSize) ? (gHeapLiveBytes - oldSize) : 0;
  if (gHeapLiveBlocks > 0) {
    --gHeapLiveBlocks;
  }
  __real__free_r(reent, hdr);
}

void* __wrap__realloc_r(struct _reent* reent, void* ptr, size_t nbytes) {
  if (!ptr) {
    return __wrap__malloc_r(reent, nbytes);
  }
  if (nbytes == 0) {
    __wrap__free_r(reent, ptr);
    return nullptr;
  }
  if (nbytes > UINT32_MAX - sizeof(HeapBlockHeader)) {
    ++gHeapFailedAllocs;
    logHeapFault("OOM_REALLOC_OVERFLOW", static_cast<unsigned>(nbytes));
    return nullptr;
  }
  auto* oldHdr = static_cast<HeapBlockHeader*>(ptr) - 1;
  if (oldHdr->magic != kHeapBlockMagic) {
    ++gHeapBadFrees;
    logHeapFault("BAD_REALLOC", static_cast<unsigned>(reinterpret_cast<uintptr_t>(ptr)));
    return nullptr;
  }
  const size_t oldSize = oldHdr->size;
  auto* newHdr = static_cast<HeapBlockHeader*>(__real__realloc_r(reent, oldHdr, sizeof(HeapBlockHeader) + nbytes));
  if (!newHdr) {
    ++gHeapFailedAllocs;
    logHeapFault("OOM_REALLOC", static_cast<unsigned>(nbytes));
    return nullptr;
  }
  newHdr->magic = kHeapBlockMagic;
  newHdr->size = static_cast<uint32_t>(nbytes);
  gHeapLiveBytes = (gHeapLiveBytes >= oldSize) ? (gHeapLiveBytes - oldSize + nbytes) : nbytes;
  if (gHeapLiveBytes > gHeapPeakBytes) {
    gHeapPeakBytes = gHeapLiveBytes;
  }
  ++gHeapTotalAllocs;
  return newHdr + 1;
}

// Playdate SDK's setup.c redirects _malloc_r/_realloc_r/_free_r to pd->system->realloc
// without overriding newlib's _calloc_r, which otherwise inspects [ptr - 4] expecting a
// dlmalloc chunk header. Allocate through __wrap__malloc_r and zero the exact byte count.
void* __wrap__calloc_r(struct _reent* reent, size_t nmemb, size_t size) {
  if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) {
    ++gHeapFailedAllocs;
    logHeapFault("OOM_CALLOC_OVERFLOW", static_cast<unsigned>(nmemb));
    return nullptr;
  }
  const size_t total = nmemb * size;
  void* ptr = __wrap__malloc_r(reent, total);
  if (ptr && total > 0) {
    std::memset(ptr, 0, total);
  }
  return ptr;
}

}  // extern "C"
#endif

namespace playdate_hal {

PlaydateAPI* pd() { return gPlaydateApi; }

void setPlaydateAPI(PlaydateAPI* api) {
  if (gPlaydateApi != api) {
    gPlaydateApi = api;
    gAppliedRefreshRate = -1.0f;
  }
  BoardConfig::ACTIVE.name = "Panic Playdate";
  BoardConfig::ACTIVE.displayWidth = HalDisplay::DISPLAY_WIDTH;
  BoardConfig::ACTIVE.displayHeight = HalDisplay::DISPLAY_HEIGHT;
  BoardConfig::ACTIVE.viewableInsets = {0, 0, 0, 0};
}

void applyRefreshRate() {
  if (!gPlaydateApi || !gPlaydateApi->display || !gPlaydateApi->display->setRefreshRate) {
    return;
  }
  float targetRate = kRefreshRateInteractive;
  if (gSerialFastMode || gSkipLoopDelay) {
    targetRate = kRefreshRateUncapped;
  } else if (gPowerSavingEnabled) {
    targetRate = kRefreshRateIdle;
  } else if (gForegroundReader) {
    targetRate = kRefreshRateActiveReader;
  }
  if (targetRate != gAppliedRefreshRate) {
    gPlaydateApi->display->setRefreshRate(targetRate);
    gAppliedRefreshRate = targetRate;
  }
}

void setSerialFastMode(bool enabled) {
  if (gSerialFastMode != enabled) {
    gSerialFastMode = enabled;
    applyRefreshRate();
  }
}

void setLoopRefreshHints(bool skipLoopDelay, bool foregroundReader) {
  gSkipLoopDelay = skipLoopDelay;
  gForegroundReader = foregroundReader;
  applyRefreshRate();
}

void resetHeapBaseline() {
#if !defined(TARGET_PLAYDATE) || defined(TARGET_SIMULATOR)
  gSimBaselineBytes = readSimulatorRawAllocatedBytes();
  gSimPeakBytes = 0;
#endif
}

HeapTelemetry getHeapTelemetry() {
  HeapTelemetry out;
  out.budgetBytes = kPlaydateHeapBudgetBytes;
#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
  out.liveBytes = gHeapLiveBytes;
  out.peakBytes = gHeapPeakBytes;
  out.liveBlocks = gHeapLiveBlocks;
  out.totalAllocs = gHeapTotalAllocs;
  out.failedAllocs = gHeapFailedAllocs;
  out.badFrees = gHeapBadFrees;
#else
  const size_t raw = readSimulatorRawAllocatedBytes();
  if (gSimBaselineBytes == 0 && raw > 0) {
    gSimBaselineBytes = raw;
  }
  out.liveBytes = (raw >= gSimBaselineBytes) ? (raw - gSimBaselineBytes) : 0;
  if (out.liveBytes > gSimPeakBytes) {
    gSimPeakBytes = out.liveBytes;
  }
  out.peakBytes = gSimPeakBytes;
#endif
  out.freeBytes = (out.budgetBytes > out.liveBytes) ? (out.budgetBytes - out.liveBytes) : 0;
  out.minFreeBytes = (out.budgetBytes > out.peakBytes) ? (out.budgetBytes - out.peakBytes) : 0;
  return out;
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
    if (millis() - start >= ms) {
      break;
    }
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
    if (c == '\n' || gSerialLineLen + 1 >= kSerialLineBufCap) {
      gSerialLineBuf[gSerialLineLen] = '\0';
      if (gPlaydateApi && gPlaydateApi->system && gPlaydateApi->system->logToConsole) {
        gPlaydateApi->system->logToConsole("%s", gSerialLineBuf);
      }
      gSerialLineLen = 0;
      if (c != '\n') {
        gSerialLineBuf[gSerialLineLen++] = c;
      }
    } else {
      gSerialLineBuf[gSerialLineLen++] = c;
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

uint32_t EspClass::getFreeHeap() const { return static_cast<uint32_t>(playdate_hal::getHeapTelemetry().freeBytes); }

uint32_t EspClass::getHeapSize() const { return static_cast<uint32_t>(playdate_hal::getHeapTelemetry().budgetBytes); }

uint32_t EspClass::getMinFreeHeap() const {
  return static_cast<uint32_t>(playdate_hal::getHeapTelemetry().minFreeBytes);
}

uint32_t EspClass::getMaxAllocHeap() const {
  // Playdate OS does not expose largest-contiguous-block queries; report remaining budget.
  return static_cast<uint32_t>(playdate_hal::getHeapTelemetry().freeBytes);
}

const char* EspClass::getSdkVersion() const { return "PlaydateSDK-3.1.2"; }

const char* EspClass::getChipModel() const { return "Playdate ARM Cortex-M7"; }

namespace HalSystem {

void begin() {
  playdate_hal::resetHeapBaseline();
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
  const playdate_hal::HeapTelemetry t = playdate_hal::getHeapTelemetry();
  return HeapStats{
      .freeBytes = t.freeBytes,
      .totalBytes = t.budgetBytes,
      .minFreeBytes = t.minFreeBytes,
      .largestBlockBytes = t.freeBytes,
  };
}

HalMemory::HeapStats HalMemory::getPsramHeap() { return HeapStats{0, 0, 0, 0}; }

HalPowerManager powerManager;

void HalPowerManager::begin() {
  normalFreq = 168;
  isLowPower = false;
  currentLockMode = None;
  gPowerSavingEnabled = false;
  _batteryCachedPercent = static_cast<int>(getBatteryPercentage());
  _batteryLastPollMs = millis();
  playdate_hal::applyRefreshRate();
}

void HalPowerManager::setPowerSaving(bool enabled) {
  const bool targetLowPower = (currentLockMode == None) && enabled;
  if (isLowPower != targetLowPower || gPowerSavingEnabled != targetLowPower) {
    isLowPower = targetLowPower;
    gPowerSavingEnabled = targetLowPower;
    playdate_hal::applyRefreshRate();
  }
}

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

HalPowerManager::Lock::Lock() {
  if (powerManager.currentLockMode == None) {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
    powerManager.setPowerSaving(false);
  } else {
    valid = false;
  }
}

HalPowerManager::Lock::~Lock() {
  if (valid) {
    powerManager.currentLockMode = None;
  }
}

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
  const uint32_t localPdEpoch = (tzOffset >= 0 || static_cast<uint32_t>(-tzOffset) <= pdEpoch)
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
