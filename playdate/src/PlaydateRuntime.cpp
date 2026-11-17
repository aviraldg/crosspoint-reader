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

void stepCrossPointFrame() {
  playdate_serial::pumpSerialBridge();
  mappedInputManager.update();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.rawInputActive() || activityManager.preventAutoSleep()) {
    gLastActivityTimeMs = millis();
    powerManager.setPowerSaving(false);
  }

  activityManager.loop();

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

constexpr size_t kDeviceFiberStackSize = 64 * 1024;
ArmFiberContext gHostCtx{};
ArmFiberContext gFiberCtx{};
uint8_t* gFiberStack = nullptr;
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
      gFiberStack.resize(kSimulatorFiberStackSize);
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
    armInitContext(&gFiberCtx, deviceFiberEntry, gFiberStack + kDeviceFiberStackSize);
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
#if defined(TARGET_PLAYDATE) && !defined(TARGET_SIMULATOR)
  if (!gFiberStack) {
    gFiberStack = static_cast<uint8_t*>(std::malloc(kDeviceFiberStackSize));
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
      playdate_hal::setPlaydateAPI(nullptr);
      break;
    default:
      break;
  }
  return 0;
}

}  // extern "C"
