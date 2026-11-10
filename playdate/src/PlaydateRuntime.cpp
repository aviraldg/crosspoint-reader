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
#include <MinizConfig.h>
#include <PlaydateRuntime.h>
#include <PlaydateSerialBridge.h>
#include <TrustedTime.h>
#include <builtinFonts/all.h>

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

void seedSampleBooksIfNeeded() {
  Storage.mkdir("/Books");
  const char* sampleEpubPath = "/Books/CrossPoint_Playdate_Guide.epub";
  if (Storage.exists(sampleEpubPath)) {
    return;
  }

  static constexpr char kMimetype[] = "application/epub+zip";
  static constexpr char kContainerXml[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
      "  <rootfiles>\n"
      "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
      "  </rootfiles>\n"
      "</container>\n";

  static constexpr char kContentOpf[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"bookid\" version=\"2.0\">\n"
      "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
      "    <dc:title>CrossPoint on Playdate</dc:title>\n"
      "    <dc:creator>CrossPoint Contributors</dc:creator>\n"
      "    <dc:language>en</dc:language>\n"
      "    <dc:identifier id=\"bookid\">urn:uuid:crosspoint-playdate-guide-001</dc:identifier>\n"
      "  </metadata>\n"
      "  <manifest>\n"
      "    <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
      "    <item id=\"ch1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
      "    <item id=\"ch2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
      "  </manifest>\n"
      "  <spine toc=\"ncx\">\n"
      "    <itemref idref=\"ch1\"/>\n"
      "    <itemref idref=\"ch2\"/>\n"
      "  </spine>\n"
      "</package>\n";

  static constexpr char kTocNcx[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
      "  <head><meta name=\"dtb:uid\" content=\"urn:uuid:crosspoint-playdate-guide-001\"/></head>\n"
      "  <docTitle><text>CrossPoint on Playdate</text></docTitle>\n"
      "  <navMap>\n"
      "    <navPoint id=\"navPoint-1\" playOrder=\"1\">\n"
      "      <navLabel><text>1. Welcome to CrossPoint</text></navLabel>\n"
      "      <content src=\"ch1.xhtml\"/>\n"
      "    </navPoint>\n"
      "    <navPoint id=\"navPoint-2\" playOrder=\"2\">\n"
      "      <navLabel><text>2. Controls &amp; Crank</text></navLabel>\n"
      "      <content src=\"ch2.xhtml\"/>\n"
      "    </navPoint>\n"
      "  </navMap>\n"
      "</ncx>\n";

  static constexpr char kCh1Xhtml[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
      "<head><title>Welcome to CrossPoint</title></head>\n"
      "<body>\n"
      "  <h1>Welcome to CrossPoint</h1>\n"
      "  <p>CrossPoint Reader brings a full EPUB, XTC, and TXT e-book reading engine to the Panic Playdate handheld.</p>\n"
      "  <p>Rendered crisply at 1:1 native 400x240 resolution on Playdate's Sharp Memory LCD, CrossPoint supports multi-font typography, hyphenation, tables of contents, bookmarks, highlights, StarDict dictionaries, and OPDS catalog downloads.</p>\n"
      "  <p>Turn the Crank clockwise or press Right/Down to advance to the next page.</p>\n"
      "</body>\n"
      "</html>\n";

  static constexpr char kCh2Xhtml[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
      "<head><title>Controls &amp; Crank</title></head>\n"
      "<body>\n"
      "  <h1>Controls &amp; Crank</h1>\n"
      "  <p><b>A Button:</b> Confirm selection or open Reader Menu.</p>\n"
      "  <p><b>B Button:</b> Go Back or exit current screen.</p>\n"
      "  <p><b>D-Pad:</b> Navigate menus and turn pages.</p>\n"
      "  <p><b>Crank:</b> Detented 36-degree steps (10 steps per full rotation) scroll lists and turn pages smoothly forward and backward.</p>\n"
      "  <p><b>Playdate Menu Button:</b> Quick access to <i>Reader Menu</i>, <i>Go Home</i>, and <i>Dark Mode</i>.</p>\n"
      "  <p>Add your own <code>.epub</code>, <code>.txt</code>, or <code>.xtc</code> books to the Playdate Data folder under <code>Data/com.crosspoint.reader/Books/</code>.</p>\n"
      "</body>\n"
      "</html>\n";

  struct ZipEntry {
    const char* name;
    const char* data;
    uint32_t size;
  };
  const ZipEntry entries[] = {
      {"mimetype", kMimetype, static_cast<uint32_t>(sizeof(kMimetype) - 1)},
      {"META-INF/container.xml", kContainerXml, static_cast<uint32_t>(sizeof(kContainerXml) - 1)},
      {"OEBPS/content.opf", kContentOpf, static_cast<uint32_t>(sizeof(kContentOpf) - 1)},
      {"OEBPS/toc.ncx", kTocNcx, static_cast<uint32_t>(sizeof(kTocNcx) - 1)},
      {"OEBPS/ch1.xhtml", kCh1Xhtml, static_cast<uint32_t>(sizeof(kCh1Xhtml) - 1)},
      {"OEBPS/ch2.xhtml", kCh2Xhtml, static_cast<uint32_t>(sizeof(kCh2Xhtml) - 1)},
  };

  std::vector<uint8_t> archive;
  archive.reserve(4096);
  auto appendU16 = [&archive](uint16_t v) {
    archive.push_back(static_cast<uint8_t>(v & 0xFFU));
    archive.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
  };
  auto appendU32 = [&archive](uint32_t v) {
    archive.push_back(static_cast<uint8_t>(v & 0xFFU));
    archive.push_back(static_cast<uint8_t>((v >> 8) & 0xFFU));
    archive.push_back(static_cast<uint8_t>((v >> 16) & 0xFFU));
    archive.push_back(static_cast<uint8_t>((v >> 24) & 0xFFU));
  };
  auto appendBytes = [&archive](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    archive.insert(archive.end(), b, b + n);
  };

  constexpr size_t kNumEntries = sizeof(entries) / sizeof(entries[0]);
  uint32_t localOffsets[kNumEntries]{};
  uint32_t crcs[kNumEntries]{};

  for (size_t i = 0; i < kNumEntries; ++i) {
    localOffsets[i] = static_cast<uint32_t>(archive.size());
    crcs[i] = static_cast<uint32_t>(
        mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const mz_uint8*>(entries[i].data), entries[i].size));
    const uint16_t nameLen = static_cast<uint16_t>(std::strlen(entries[i].name));
    appendU32(0x04034b50U);  // Local file header signature
    appendU16(20);           // Version needed to extract
    appendU16(0);            // General purpose bit flag
    appendU16(0);            // Compression method: 0 (stored)
    appendU16(0);            // Last mod file time
    appendU16(0);            // Last mod file date
    appendU32(crcs[i]);
    appendU32(entries[i].size);
    appendU32(entries[i].size);
    appendU16(nameLen);
    appendU16(0);  // Extra field length
    appendBytes(entries[i].name, nameLen);
    appendBytes(entries[i].data, entries[i].size);
  }

  const uint32_t centralDirOffset = static_cast<uint32_t>(archive.size());
  for (size_t i = 0; i < kNumEntries; ++i) {
    const uint16_t nameLen = static_cast<uint16_t>(std::strlen(entries[i].name));
    appendU32(0x02014b50U);  // Central directory file header signature
    appendU16(20);           // Version made by
    appendU16(20);           // Version needed to extract
    appendU16(0);            // General purpose bit flag
    appendU16(0);            // Compression method: 0 (stored)
    appendU16(0);            // Last mod file time
    appendU16(0);            // Last mod file date
    appendU32(crcs[i]);
    appendU32(entries[i].size);
    appendU32(entries[i].size);
    appendU16(nameLen);
    appendU16(0);  // Extra field length
    appendU16(0);  // File comment length
    appendU16(0);  // Disk number start
    appendU16(0);  // Internal file attributes
    appendU32(0);  // External file attributes
    appendU32(localOffsets[i]);
    appendBytes(entries[i].name, nameLen);
  }

  const uint32_t centralDirSize = static_cast<uint32_t>(archive.size()) - centralDirOffset;
  appendU32(0x06054b50U);  // End of central directory signature
  appendU16(0);            // Number of this disk
  appendU16(0);            // Disk where central directory starts
  appendU16(static_cast<uint16_t>(kNumEntries));
  appendU16(static_cast<uint16_t>(kNumEntries));
  appendU32(centralDirSize);
  appendU32(centralDirOffset);
  appendU16(0);  // Comment length

  HalFile out;
  if (Storage.openFileForWrite("MAIN", sampleEpubPath, out)) {
    out.write(archive.data(), archive.size());
    out.close();
    RECENT_BOOKS.addBook(sampleEpubPath, "CrossPoint on Playdate", "CrossPoint Contributors", "");
    RECENT_BOOKS.saveToFile();
  }
}

unsigned long gLastActivityTimeMs = 0;

void stepCrossPointFrame() {
  playdate_serial::pumpSerialBridge();
  mappedInputManager.update();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.rawInputActive() ||
      activityManager.preventAutoSleep()) {
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
  uint32_t regs[9];   // r4-r11, lr
  uint32_t vfp[16];   // s16-s31
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

  seedSampleBooksIfNeeded();
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
    unsigned int secs = (pd && pd->system && pd->system->getSecondsSinceEpoch)
                            ? pd->system->getSecondsSinceEpoch(&ms)
                            : 0;
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
  return static_cast<uint32_t>(
      ~mz_crc32(~static_cast<mz_ulong>(crc), static_cast<const unsigned char*>(data), length));
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
