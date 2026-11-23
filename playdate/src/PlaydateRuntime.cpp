#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <MinizConfig.h>
#include <PlaydateRuntime.h>
#include <PlaydateSerialBridge.h>
#include <TrustedTime.h>
#include <builtinFonts/all.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(TARGET_SIMULATOR)
#include <ucontext.h>
#endif

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "LcpPassphraseStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsTokenStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/ButtonNavigator.h"
#include "util/PluginEvents.h"
#include "util/Timezones.h"

FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
GfxRenderer renderer(display);
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);

// Built-in fonts matching src/main.cpp
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(14, &notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(12, &notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);

EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(16, &notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);

EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(18, &notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

EpdFont smallFont(&ubuntu_8_regular);
EpdFontFamily smallFontFamily(8, &smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(10, &ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(12, &ui12RegularFont, &ui12BoldFont);

// Restart/frontlight hooks called by activities when leaving heavy screens
void silentRestart() {
  activityManager.goHome();
  activityManager.requestUpdate();
}

void silentRestartToReader() {
  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else {
    activityManager.goHome();
  }
  activityManager.requestUpdate();
}

void silentRestartToSettings() {
  activityManager.goToSettings();
  activityManager.requestUpdate();
}

void silentRestartToJoinNetwork() {
  activityManager.goHome();
  activityManager.requestUpdate();
}

void restartToHomeAfterStorageHandoff() {
  activityManager.goHome();
  activityManager.requestUpdate();
}

void toggleFrontlight() {}

void handleSlideSwitch() {}

namespace {

void setupFonts() {
  fontDecompressor.init();
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  sdFontSystem.begin(renderer);
}

// playdate/tools/build_manual.py bundles the manual and its content stamp in the pdx.
// Storage reads fall back from the Data folder to the pdx, so these paths resolve there.
constexpr char kManualPdxPath[] = "/manual/CrossPoint_Manual.epub";
constexpr char kManualPdxStampPath[] = "/manual/manual.stamp";
constexpr char kManualBookPath[] = "/Books/CrossPoint Manual.epub";
constexpr char kManualTmpPath[] = "/Books/.CrossPoint Manual.epub.tmp";
constexpr char kManualStampPath[] = "/.crosspoint/manual.stamp";
constexpr char kLegacyGuidePath[] = "/Books/CrossPoint_Playdate_Guide.epub";
constexpr size_t kManualStampMax = 32;
constexpr size_t kManualCopyChunk = 4096;

std::string readManualStamp(const char* path) {
  char buf[kManualStampMax + 1]{};
  const size_t n = Storage.readFileToBuffer(path, buf, sizeof(buf));
  std::string stamp(buf, n);
  while (!stamp.empty() && std::isspace(static_cast<unsigned char>(stamp.back()))) stamp.pop_back();
  return stamp;
}

bool copyManualToBooks() {
  HalFile in;
  if (!Storage.openFileForRead("MAIN", kManualPdxPath, in)) return false;
  HalFile out;
  if (!Storage.openFileForWrite("MAIN", kManualTmpPath, out)) {
    in.close();
    return false;
  }
  // One chunk-sized buffer for the whole copy; freed on return.
  auto chunk = makeUniqueNoThrow<uint8_t[]>(kManualCopyChunk);
  bool ok = chunk != nullptr;
  while (ok) {
    const int n = in.read(chunk.get(), kManualCopyChunk);
    if (n < 0) ok = false;
    if (n <= 0) break;
    ok = out.write(chunk.get(), static_cast<size_t>(n)) == static_cast<size_t>(n);
  }
  in.close();
  out.close();
  if (!ok || !Storage.replaceFile(kManualTmpPath, kManualBookPath)) {
    Storage.remove(kManualTmpPath);
    return false;
  }
  return true;
}

// Installs or refreshes the manual when the bundled edition changes. Its reading
// cache is keyed by path, so a replaced edition must drop the old layout cache.
void installBundledManual() {
  Storage.mkdir("/Books");
  if (Storage.exists(kLegacyGuidePath)) {
    removeBookFile(kLegacyGuidePath);
  }

  const std::string bundledStamp = readManualStamp(kManualPdxStampPath);
  if (bundledStamp.empty()) {
    LOG_ERR("MAIN", "Bundled manual is missing from the pdx");
    return;
  }
  // A stored stamp without the book means the reader deleted the manual; keep it deleted.
  const std::string storedStamp = readManualStamp(kManualStampPath);
  const bool installed = Storage.exists(kManualBookPath);
  if (storedStamp == bundledStamp || (!storedStamp.empty() && !installed)) {
    return;
  }

  if (!copyManualToBooks()) {
    LOG_ERR("MAIN", "Could not install the bundled manual");
    return;
  }
  clearBookCache(kManualBookPath);
  if (!Storage.writeFile(kManualStampPath, String(bundledStamp.c_str()))) {
    LOG_ERR("MAIN", "Could not record the manual stamp");
  }
  if (!installed) {
    // Matches the EPUB metadata that build_manual.py writes from pdxinfo.
    RECENT_BOOKS.addBook(kManualBookPath, "CrossPoint Reader Manual", "CrossPoint Reader contributors", "");
    RECENT_BOOKS.saveToFile();
  }
}

unsigned long gLastActivityTimeMs = 0;
unsigned long gLastDiagTickMs = 0;
unsigned long gLastHttpWatchLogMs = 0;
uint32_t gLastLoggedVisit = UINT32_MAX;
size_t gLastLoggedHeapPeak = 0;
size_t gLastLoggedStackPeak = 0;
bool gLoggedCanaryFault = false;

constexpr uint32_t kFiberStackCanary = 0xDEADBEEFU;
constexpr uint8_t kFiberStackWatermarkByte = 0xA5U;
constexpr size_t kHeapPeakLogStepBytes = 64 * 1024;
constexpr size_t kStackPeakLogStepBytes = 4 * 1024;
constexpr unsigned long kDiagTickIntervalMs = 1000;
constexpr unsigned long kHttpWatchLogIntervalMs = 15000;

size_t gFiberStackLowestTouchedOffset = 0;

void paintFiberStack(uint8_t* base, size_t size) {
  if (!base || size < sizeof(kFiberStackCanary)) return;
  std::memset(base, kFiberStackWatermarkByte, size);
  std::memcpy(base, &kFiberStackCanary, sizeof(kFiberStackCanary));
  gFiberStackLowestTouchedOffset = size;
  gLoggedCanaryFault = false;
}

void runPeriodicDiagnostics() {
  const uint32_t currentVisit = activityManager.visit();
  if (currentVisit != gLastLoggedVisit) {
    gLastLoggedVisit = currentVisit;
    playdate_hal::logMemorySnapshot("activity_change");
  }

  const unsigned long now = millis();
  if (now - gLastDiagTickMs < kDiagTickIntervalMs) {
    return;
  }
  gLastDiagTickMs = now;

  // Reap retired HTTP connections in the background even when no further network
  // requests are made (e.g., while reading after an OPDS download or KOReader sync).
  playdate_hal::reapRetiredHttpConnections(/*forceAll=*/false);

  if (!playdate_hal::isFiberStackCanaryIntact() && !gLoggedCanaryFault) {
    gLoggedCanaryFault = true;
    LOG_ERR("MEM", "FIBER STACK CANARY CORRUPTED! stackSize=%u",
            static_cast<unsigned>(playdate_hal::getFiberStackSizeBytes()));
    playdate_hal::logMemorySnapshot("stack_canary_corrupt");
  }

  const playdate_hal::HeapTelemetry heap = playdate_hal::getHeapTelemetry();
  const size_t stackPeak = playdate_hal::getFiberStackPeakUsedBytes();
  const playdate_hal::HttpTelemetry http = playdate_hal::getHttpTelemetry();

  if (heap.peakBytes >= gLastLoggedHeapPeak + kHeapPeakLogStepBytes) {
    gLastLoggedHeapPeak = heap.peakBytes;
    playdate_hal::logMemorySnapshot("heap_peak");
  } else if (stackPeak >= gLastLoggedStackPeak + kStackPeakLogStepBytes) {
    gLastLoggedStackPeak = stackPeak;
    playdate_hal::logMemorySnapshot("stack_peak");
  } else if ((http.activeConnections > 0 || http.retiredConnections > 0) &&
             (now - gLastHttpWatchLogMs >= kHttpWatchLogIntervalMs)) {
    gLastHttpWatchLogMs = now;
    playdate_hal::logMemorySnapshot("http_watch");
  }
}

void stepCrossPointFrame() {
  playdate_serial::pumpSerialBridge();
  mappedInputManager.update();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.rawInputActive() || activityManager.preventAutoSleep()) {
    gLastActivityTimeMs = millis();
    powerManager.setPowerSaving(false);
  }

  activityManager.loop();
  runPeriodicDiagnostics();

  const bool skipLoopDelay = activityManager.skipLoopDelay();
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);
  } else if (millis() - gLastActivityTimeMs >= HalPowerManager::IDLE_POWER_SAVING_MS) {
    powerManager.setPowerSaving(true);
  }
  playdate_hal::setLoopRefreshHints(skipLoopDelay, activityManager.isForegroundReader());
}

#if defined(TARGET_SIMULATOR)
constexpr size_t kSimulatorFiberStackSize = 512 * 1024;
ucontext_t gHostCtx{};
ucontext_t gFiberCtx{};
std::vector<uint8_t> gFiberStack;
bool gFiberInitialized = false;
bool gInFiber = false;
bool gYielded = false;

void simulatorFiberEntry() {
  for (;;) {
    gYielded = false;
    stepCrossPointFrame();
    gInFiber = false;
    swapcontext(&gFiberCtx, &gHostCtx);
  }
}
#elif defined(TARGET_PLAYDATE)
struct ArmFiberContext {
  uint32_t regs[9];  // r4-r11, lr
  uint32_t vfp[16];  // s16-s31
  uint32_t sp;
};

// 96 KB fiber stack (~1.1% of Playdate's 8.38 MB RAM) leaves headroom for deep EPUB
// HTML/CSS parsing, font decompression, cover decoding, and nested network callbacks.
constexpr size_t kDeviceFiberStackSize = 96 * 1024;
ArmFiberContext gHostCtx{};
ArmFiberContext gFiberCtx{};
std::unique_ptr<uint8_t[]> gFiberStack;
bool gFiberInitialized = false;
bool gInFiber = false;
bool gYielded = false;

extern "C" {

__attribute__((naked)) void armSwapContext(ArmFiberContext* from, const ArmFiberContext* to) {
  __asm__ volatile(
      "stmia r0!, {r4-r11, lr}\n"
      "vstmia r0!, {s16-s31}\n"
      "str sp, [r0]\n"
      "ldmia r1!, {r4-r11, lr}\n"
      "vldmia r1!, {s16-s31}\n"
      "ldr sp, [r1]\n"
      "bx lr\n");
}

}  // extern "C"

void deviceFiberEntry() {
  for (;;) {
    gYielded = false;
    stepCrossPointFrame();
    gInFiber = false;
    armSwapContext(&gFiberCtx, &gHostCtx);
  }
}

void armInitContext(ArmFiberContext* ctx, void (*entry)(), void* stackTop) {
  std::memset(ctx, 0, sizeof(*ctx));
  const uint32_t alignedSp = reinterpret_cast<uint32_t>(stackTop) & ~7U;
  ctx->regs[8] = reinterpret_cast<uint32_t>(entry);  // lr
  ctx->sp = alignedSp;
}
#else
bool gYielded = false;
#endif

const uint8_t* getFiberStackBaseAndSize(size_t& sizeOut) {
#if defined(TARGET_SIMULATOR)
  if (!gFiberStack.empty()) {
    sizeOut = gFiberStack.size();
    return gFiberStack.data();
  }
#elif defined(TARGET_PLAYDATE)
  if (gFiberStack) {
    sizeOut = kDeviceFiberStackSize;
    return gFiberStack.get();
  }
#endif
  sizeOut = 0;
  return nullptr;
}

void onMenuReaderMenu(void* userdata) {
  (void)userdata;
  if (gYielded) {
    return;
  }
  gLastActivityTimeMs = millis();
  powerManager.setPowerSaving(false);
  if (!activityManager.remoteShortcut(/*readerMenu=*/true)) {
    mappedInputManager.injectRemoteClick(RemoteButton::Confirm);
  }
  activityManager.requestUpdate();
}

void onMenuGoHome(void* userdata) {
  (void)userdata;
  gLastActivityTimeMs = millis();
  powerManager.setPowerSaving(false);
  if (gYielded) {
    mappedInputManager.injectRemoteClick(RemoteButton::Back);
    return;
  }
  activityManager.goHome();
  activityManager.requestUpdate();
}

void onMenuDarkMode(void* userdata) {
  (void)userdata;
  gLastActivityTimeMs = millis();
  powerManager.setPowerSaving(false);
  SETTINGS.screenInverted = (SETTINGS.screenInverted == 0) ? 1 : 0;
  SETTINGS.saveToFile();
  display.setInverted(SETTINGS.screenInverted != 0);
  activityManager.requestUpdate(/*immediate=*/true);
}

void registerSystemMenuItems(PlaydateAPI* pd) {
  if (!pd || !pd->system) return;
  pd->system->removeAllMenuItems();
  pd->system->addMenuItem("Reader Menu", onMenuReaderMenu, nullptr);
  pd->system->addMenuItem("Go Home", onMenuGoHome, nullptr);
  pd->system->addMenuItem("Dark Mode", onMenuDarkMode, nullptr);
}

int updateCallback(void* userdata) {
  (void)userdata;
#if defined(TARGET_SIMULATOR)
  if (!gFiberInitialized) {
    if (getcontext(&gFiberCtx) == 0) {
      if (gFiberStack.size() != kSimulatorFiberStackSize) {
        gFiberStack.resize(kSimulatorFiberStackSize);
        paintFiberStack(gFiberStack.data(), gFiberStack.size());
      }
      gFiberCtx.uc_stack.ss_sp = gFiberStack.data();
      gFiberCtx.uc_stack.ss_size = gFiberStack.size();
      gFiberCtx.uc_link = &gHostCtx;
      makecontext(&gFiberCtx, simulatorFiberEntry, 0);
      gFiberInitialized = true;
    }
  }
  if (gFiberInitialized) {
    gInFiber = true;
    swapcontext(&gHostCtx, &gFiberCtx);
    gInFiber = false;
  } else {
    stepCrossPointFrame();
  }
#elif defined(TARGET_PLAYDATE)
  if (!gFiberInitialized && gFiberStack != nullptr) {
    armInitContext(&gFiberCtx, deviceFiberEntry, gFiberStack.get() + kDeviceFiberStackSize);
    gFiberInitialized = true;
  }
  if (gFiberInitialized) {
    gInFiber = true;
    armSwapContext(&gHostCtx, &gFiberCtx);
    gInFiber = false;
  } else {
    stepCrossPointFrame();
  }
#else
  stepCrossPointFrame();
#endif
  return 1;
}

void initCrossPointOnPlaydate(PlaydateAPI* pd) {
  playdate_hal::resetHeapBaseline();
#if defined(TARGET_SIMULATOR)
  if (gFiberStack.size() != kSimulatorFiberStackSize) {
    gFiberStack.resize(kSimulatorFiberStackSize);
    paintFiberStack(gFiberStack.data(), gFiberStack.size());
  }
#elif defined(TARGET_PLAYDATE)
  if (!gFiberStack) {
    gFiberStack = makeUniqueNoThrow<uint8_t[]>(kDeviceFiberStackSize);
    if (gFiberStack) {
      paintFiberStack(gFiberStack.get(), kDeviceFiberStackSize);
    } else {
      LOG_ERR("MEM", "Failed to allocate %u-byte device fiber stack", static_cast<unsigned>(kDeviceFiberStackSize));
    }
  }
#endif
  HalSystem::begin();
  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();
  Storage.begin();

  APP_STATE.loadFromFile();

  // Default to compact 12pt font, LandscapeCCW orientation, and 1-bit Bayer-dithered images on Playdate
  SETTINGS.fontPointSize = 12;
  SETTINGS.orientation = CrossPointSettings::ORIENTATION::LANDSCAPE_CCW;
  SETTINGS.frontButtonFollowOrientation = 0;
  SETTINGS.sideButtonFollowOrientation = 0;
  SETTINGS.imageRendering = CrossPointSettings::IMAGES_DISPLAY_BW;
  SETTINGS.loadFromFile();
  SETTINGS.frontButtonFollowOrientation = 0;
  SETTINGS.sideButtonFollowOrientation = 0;

  timezones::applyToClock();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  OPDS_TOKENS.loadFromFile();
  LCP_PASSPHRASES.loadFromFile();

  display.begin(/*seamless=*/false);
  renderer.begin();
  setupFonts();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  pluginevents::refreshSubscriptions();

  struct tm rtcLocal{};
  if (halClock.localTime(rtcLocal)) {
    trustedtime::adopt(static_cast<int64_t>(mktime(&rtcLocal)));
  }
  trustedtime::init();

  installBundledManual();
  registerSystemMenuItems(pd);
  playdate_serial::registerSerialBridge(pd);

  activityManager.begin();
  gLastActivityTimeMs = millis();
  if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else {
    activityManager.goHome();
  }
  activityManager.loop();
  gLastLoggedVisit = activityManager.visit();
  gLastDiagTickMs = millis();
  gLastLoggedHeapPeak = playdate_hal::getHeapTelemetry().peakBytes;
  gLastLoggedStackPeak = playdate_hal::getFiberStackPeakUsedBytes();
  playdate_hal::logMemorySnapshot("boot_done");
  playdate_hal::setLoopRefreshHints(activityManager.skipLoopDelay(), activityManager.isForegroundReader());
}

}  // namespace

WifiSelectionActivity::WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const bool autoConnect)
    : Activity("WifiSelection", renderer, mappedInput), UiAppHost(renderer), allowAutoConnect(autoConnect) {}

void WifiSelectionActivity::onEnter() {
  Activity::onEnter();
  ActivityResult result;
  result.isCancelled = false;
  result.data = WifiResult{true, "Playdate OS", "127.0.0.1"};
  setResult(std::move(result));
  finish();
}

void WifiSelectionActivity::onExit() { Activity::onExit(); }
void WifiSelectionActivity::loop() {}
void WifiSelectionActivity::render(RenderLock&&) {}

namespace playdate_hal {

size_t getFiberStackSizeBytes() {
  size_t sz = 0;
  getFiberStackBaseAndSize(sz);
  return sz;
}

bool isFiberStackCanaryIntact() {
  size_t sz = 0;
  const uint8_t* base = getFiberStackBaseAndSize(sz);
  if (!base || sz < sizeof(kFiberStackCanary)) {
    return true;
  }
  uint32_t canary = 0;
  std::memcpy(&canary, base, sizeof(canary));
  return canary == kFiberStackCanary;
}

size_t getFiberStackPeakUsedBytes() {
  size_t sz = 0;
  const uint8_t* base = getFiberStackBaseAndSize(sz);
  if (!base || sz <= sizeof(kFiberStackCanary)) {
    return 0;
  }
  if (!isFiberStackCanaryIntact()) {
    gFiberStackLowestTouchedOffset = 0;
    return sz;
  }
  const size_t limit = std::min(gFiberStackLowestTouchedOffset, sz);
  size_t offset = sizeof(kFiberStackCanary);
  while (offset < limit && base[offset] == kFiberStackWatermarkByte) {
    ++offset;
  }
  if (offset < gFiberStackLowestTouchedOffset) {
    gFiberStackLowestTouchedOffset = offset;
  }
  const size_t clamped = std::min(gFiberStackLowestTouchedOffset, sz);
  return sz - clamped;
}

size_t getFiberStackMinFreeBytes() {
  const size_t sz = getFiberStackSizeBytes();
  const size_t used = getFiberStackPeakUsedBytes();
  return (sz > used) ? (sz - used) : 0;
}

void logMemorySnapshot(const char* reason) {
  const HeapTelemetry heap = getHeapTelemetry();
  const HttpTelemetry http = getHttpTelemetry();
  const size_t stackSize = getFiberStackSizeBytes();
  const size_t stackPeak = getFiberStackPeakUsedBytes();
  const bool canaryOk = isFiberStackCanaryIntact();
  LOG_INF("MEM",
          "reason=%s visit=%u heapLive=%u heapPeak=%u heapFree=%u blocks=%u fails=%u badFrees=%u "
          "stackPeak=%u/%u canary=%d httpActive=%u httpRetired=%u httpReaped=%u",
          reason ? reason : "periodic", static_cast<unsigned>(activityManager.visit()),
          static_cast<unsigned>(heap.liveBytes), static_cast<unsigned>(heap.peakBytes),
          static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.liveBlocks),
          static_cast<unsigned>(heap.failedAllocs), static_cast<unsigned>(heap.badFrees),
          static_cast<unsigned>(stackPeak), static_cast<unsigned>(stackSize), canaryOk ? 1 : 0,
          static_cast<unsigned>(http.activeConnections), static_cast<unsigned>(http.retiredConnections),
          static_cast<unsigned>(http.reapedConnections));
}

void pumpMainLoop() {
#if defined(TARGET_SIMULATOR)
  if (gInFiber) {
    gInFiber = false;
    gYielded = true;
    swapcontext(&gFiberCtx, &gHostCtx);
    gYielded = false;
    gInFiber = true;
  }
#elif defined(TARGET_PLAYDATE)
  if (gInFiber) {
    gInFiber = false;
    gYielded = true;
    armSwapContext(&gFiberCtx, &gHostCtx);
    gYielded = false;
    gInFiber = true;
  }
#endif
  mappedInputManager.update();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.rawInputActive()) {
    gLastActivityTimeMs = millis();
    powerManager.setPowerSaving(false);
  }
}

}  // namespace playdate_hal

#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
#include <sys/time.h>
#endif

extern "C" {

#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
using InitFunc = void (*)();
extern InitFunc __preinit_array_start[];
extern InitFunc __preinit_array_end[];
extern InitFunc __init_array_start[];
extern InitFunc __init_array_end[];

void* __dso_handle = nullptr;
uintptr_t __exidx_start = 0;
uintptr_t __exidx_end = 0;

void _fini(void) {}
void _exit(int status) {
  (void)status;
  while (true) {
  }
}
int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  return -1;
}
int _getpid(void) { return 1; }
int _close(int fd) {
  (void)fd;
  return -1;
}
int _fstat(int fd, void* st) {
  (void)fd;
  (void)st;
  return 0;
}
int _isatty(int fd) {
  (void)fd;
  return 1;
}
int _lseek(int fd, int ptr, int dir) {
  (void)fd;
  (void)ptr;
  (void)dir;
  return 0;
}
int _read(int fd, char* ptr, int len) {
  (void)fd;
  (void)ptr;
  (void)len;
  return 0;
}
int _write(int fd, const char* ptr, int len) {
  (void)fd;
  PlaydateAPI* pd = playdate_hal::pd();
  if (pd && pd->system && pd->system->logToConsole && ptr && len > 0) {
    pd->system->logToConsole("%.*s", len, ptr);
  }
  return len;
}
int _gettimeofday(struct timeval* tv, void* tz) {
  (void)tz;
  if (tv) {
    PlaydateAPI* pd = playdate_hal::pd();
    unsigned int ms = 0;
    unsigned int secs =
        (pd && pd->system && pd->system->getSecondsSinceEpoch) ? pd->system->getSecondsSinceEpoch(&ms) : 0;
    // Playdate epoch is 2000-01-01 00:00:00 UTC (946684800 Unix time)
    tv->tv_sec = static_cast<time_t>(secs) + 946684800;
    tv->tv_usec = static_cast<long>(ms) * 1000L;
  }
  return 0;
}

static void initPlaydateCppRuntimeOnce() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;
  for (InitFunc* fn = __preinit_array_start; fn < __preinit_array_end; ++fn) {
    if (*fn) (*fn)();
  }
  for (InitFunc* fn = __init_array_start; fn < __init_array_end; ++fn) {
    if (*fn) (*fn)();
  }
}
#endif

uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  return static_cast<uint32_t>(~mz_crc32(~static_cast<mz_ulong>(crc), static_cast<const unsigned char*>(data), length));
}

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prevSum) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t s1 = prevSum & 0xffffU;
  uint32_t s2 = (prevSum >> 16U) & 0xffffU;
  for (unsigned int i = 0; i < length; ++i) {
    s1 = (s1 + bytes[i]) % 65521U;
    s2 = (s2 + s1) % 65521U;
  }
  return (s2 << 16U) | s1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg) {
  (void)arg;
  playdate_hal::setPlaydateAPI(playdate);

  switch (event) {
    case kEventInit:
#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
      initPlaydateCppRuntimeOnce();
#endif
      initCrossPointOnPlaydate(playdate);
      playdate->system->setUpdateCallback(updateCallback, nullptr);
      break;
    case kEventResume:
    case kEventUnlock:
      gLastActivityTimeMs = millis();
      powerManager.setPowerSaving(false);
      break;
    case kEventPause:
    case kEventLowPower:
      activityManager.prepareForSleep();
      break;
    case kEventTerminate:
      activityManager.prepareForSleep();
      fontCacheManager.clearCache();
      playdate_hal::reapRetiredHttpConnections(/*forceAll=*/true);
      playdate_hal::releasePendingHttpAccessContexts();
#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
      if (!gInFiber) {
        gFiberStack.reset();
        gFiberInitialized = false;
      }
#endif
      playdate_hal::setPlaydateAPI(nullptr);
      break;
    default:
      break;
  }
  return 0;
}

}  // extern "C"
