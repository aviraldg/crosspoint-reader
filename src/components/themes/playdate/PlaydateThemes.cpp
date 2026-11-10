#include "PlaydateThemes.h"

#if defined(CROSSPOINT_PLAYDATE) && CROSSPOINT_PLAYDATE

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <PlaydateRuntime.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"

namespace {

constexpr unsigned long kHudHoldMs = 2800;
constexpr unsigned long kHudSlideMs = 300;
constexpr int kHudHeight = 24;

// Byte-aligned 2:3 cover slot (covX = 48 is divisible by 8, covW = 80 is divisible by 8)
// so GfxRenderer::copyRegionToBuffer / copyBufferToRegion copies exact bytes without
// touching surrounding card borders or shadows.
constexpr int kCoverSlotX = 48;
constexpr int kCoverSlotW = 80;
constexpr int kCoverSlotMaxH = 120;
constexpr size_t kCoverSlotBufferBytes = (kCoverSlotW / 8) * kCoverSlotMaxH;

struct CachedCoverSlot {
  std::string bmpPath;
  std::string progressBookPath;
  std::string captionKey;
  std::string captionLines[2];
  std::string captionAuthor;
  int captionLineCount = 0;
  int captionMaxW = 0;
  int progressPercent = -1;
  int height = 0;
  bool valid = false;
  uint8_t pixels[kCoverSlotBufferBytes] = {};
};

CachedCoverSlot g_cachedCoverSlot;
unsigned long g_lastHudInputMs = 0;
unsigned long g_playfulSelectStartMs = 0;
int g_lastPlayfulSelectedIndex = -99;
bool g_hudInitialized = false;
int g_lastHudSlideOffset = -1;
int g_lastHomeAnimTick = -1;

bool isSelectionAnimActive(unsigned long now) {
  if (!g_hudInitialized) {
    return true;
  }
  return (now - g_lastHudInputMs) <= (kHudHoldMs + kHudSlideMs);
}

int animationTick() {
  const unsigned long now = millis();
  if (!isSelectionAnimActive(now)) {
    return 0;
  }
  return static_cast<int>(now / 33UL) + 1;
}

// 1-bit Bayer / halftone helper
void fillHalftoneRect(const GfxRenderer& renderer, int x, int y, int w, int h, int density, bool blackDots = true) {
  const int xEnd = x + w;
  const int yEnd = y + h;
  for (int py = y; py < yEnd; ++py) {
    for (int px = x; px < xEnd; ++px) {
      bool dot = false;
      if (density == 1) {
        // 25% sparse halftone
        dot = ((px & 1) == 0) && ((py & 1) == 0);
      } else if (density == 2) {
        // 50% checkerboard
        dot = ((px + py) & 1) == 0;
      } else if (density == 3) {
        // Horizontal pin-stripe every 3rd row, alternating columns
        dot = (py % 3 == 0) && ((px & 1) == 0);
      }
      if (dot) {
        renderer.drawPixel(px, py, blackDots);
      }
    }
  }
}

void drawCheckerHLine(const GfxRenderer& renderer, int x0, int x1, int y, bool state = true) {
  for (int x = x0; x <= x1; ++x) {
    if (((x + y) & 1) == 0) {
      renderer.drawPixel(x, y, state);
    }
  }
}

void fillCircle(const GfxRenderer& renderer, int cx, int cy, int r, bool state = true) {
  const int r2 = r * r;
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      if (dx * dx + dy * dy <= r2) {
        renderer.drawPixel(cx + dx, cy + dy, state);
      }
    }
  }
}

void drawCircle(const GfxRenderer& renderer, int cx, int cy, int r, bool state = true) {
  const int rMin2 = (r - 1) * (r - 1);
  const int rMax2 = r * r + r;
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 >= rMin2 && d2 <= rMax2) {
        renderer.drawPixel(cx + dx, cy + dy, state);
      }
    }
  }
}

// Animated marching-ants selection rectangle
void drawMarchingAntsRect(const GfxRenderer& renderer, int x, int y, int w, int h, int tick, bool state) {
  const int phase = (tick / 2) & 7;
  for (int i = 2; i < w - 2; ++i) {
    if (((i + phase) & 7) < 4) {
      renderer.drawPixel(x + i, y, state);
    }
    if (((i - phase + 64) & 7) < 4) {
      renderer.drawPixel(x + i, y + h - 1, state);
    }
  }
  for (int j = 2; j < h - 2; ++j) {
    if (((j - phase + 64) & 7) < 4) {
      renderer.drawPixel(x, y + j, state);
    }
    if (((j + phase) & 7) < 4) {
      renderer.drawPixel(x + w - 1, y + j, state);
    }
  }
}

// Hard-shadow Playdate card (2px border + 3px solid black offset shadow)
void drawHardShadowCard(const GfxRenderer& renderer, int x, int y, int w, int h, int radius, bool inverted,
                        int shadowOffset = 3) {
  if (shadowOffset > 0) {
    renderer.fillRoundedRect(x + shadowOffset, y + shadowOffset, w, h, radius, Color::Black);
  }
  renderer.fillRoundedRect(x, y, w, h, radius, inverted ? Color::Black : Color::White);
  renderer.drawRoundedRect(x, y, w, h, 2, radius, true);
}

// Graphical Playdate battery icon (22x11 body + 2x5 positive terminal nub + proportional fill)
// plus percentage and wall clock to its left. Used identically in both Crossplay and Playful.
// Returns the leftmost X coordinate occupied by the status cluster.
int drawPlaydateBatteryAndClock(const GfxRenderer& renderer, int rightEdgeX, int centerY, bool onBlackBar) {
  constexpr int kBattW = 22;
  constexpr int kBattH = 11;
  constexpr int kNubW = 2;
  constexpr int kNubH = 5;

  const bool inkState = !onBlackBar;  // true = black ink on white, false = white ink on black
  const uint16_t percentage = std::min<uint16_t>(100, powerManager.getBatteryPercentage());
  const bool charging = gpio.isUsbConnected();

  const int battX = rightEdgeX - kBattW - kNubW;
  const int battY = centerY - kBattH / 2;

  // Outer battery body (22x11 with 1px chamfered corners)
  renderer.drawLine(battX + 1, battY, battX + kBattW - 2, battY, inkState);
  renderer.drawLine(battX + 1, battY + kBattH - 1, battX + kBattW - 2, battY + kBattH - 1, inkState);
  renderer.drawLine(battX, battY + 1, battX, battY + kBattH - 2, inkState);
  renderer.drawLine(battX + kBattW - 1, battY + 1, battX + kBattW - 1, battY + kBattH - 2, inkState);
  // Inner 2px border reinforcement on top/bottom/sides for crisp Playdate look
  renderer.drawRect(battX + 1, battY + 1, kBattW - 2, kBattH - 2, inkState);

  // Positive terminal nub on right (2x5)
  renderer.fillRect(battX + kBattW, battY + (kBattH - kNubH) / 2, kNubW, kNubH, inkState);

  // Proportional charge fill bar inside (max 16x5 inside the 2px frame + 1px gap)
  constexpr int kMaxFillW = kBattW - 6;
  constexpr int kFillH = kBattH - 6;
  int fillW = (static_cast<int>(percentage) * kMaxFillW + 99) / 100;
  if (percentage > 0 && fillW < 1) fillW = 1;
  if (charging && fillW < 8) fillW = 8;
  if (fillW > 0) {
    renderer.fillRect(battX + 3, battY + 3, fillW, kFillH, inkState);
  }
  if (charging) {
    // Small lightning cut-out inside the fill
    const int bx = battX + 6;
    const int by = battY + 2;
    renderer.drawLine(bx + 3, by + 1, bx + 1, by + 3, !inkState);
    renderer.drawLine(bx + 1, by + 3, bx + 4, by + 3, !inkState);
    renderer.drawLine(bx + 4, by + 3, bx + 2, by + 5, !inkState);
  }

  // Percentage and optional clock to the left of the battery icon
  char statusBuf[32] = {};
  char clockBuf[12] = {};
  const bool showClock = SETTINGS.clockShowInHeader && halClock.isAvailable() &&
                         halClock.formatTime(clockBuf, sizeof(clockBuf), SETTINGS.clockFormat == 1);
  const bool showPercent = SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;

  if (showClock && showPercent) {
    snprintf(statusBuf, sizeof(statusBuf), "%s  %u%%", clockBuf, static_cast<unsigned>(percentage));
  } else if (showClock) {
    snprintf(statusBuf, sizeof(statusBuf), "%s", clockBuf);
  } else if (showPercent) {
    snprintf(statusBuf, sizeof(statusBuf), "%u%%", static_cast<unsigned>(percentage));
  }

  int leftX = battX;
  if (statusBuf[0] != '\0') {
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, statusBuf, EpdFontFamily::BOLD);
    const int textH = renderer.getLineHeight(SMALL_FONT_ID);
    const int textX = battX - 6 - textW;
    const int textY = centerY - textH / 2;
    renderer.drawText(SMALL_FONT_ID, textX, textY, statusBuf, inkState, EpdFontFamily::BOLD);
    leftX = textX;
  }
  return leftX;
}

// Hanging 3D isometric cube icon from Playdate Settings.pdx
void drawHangingCubeIcon(const GfxRenderer& renderer, int cx, int baseCy, int tick) {
  const int bob = static_cast<int>(std::round(std::sin(tick * 0.10) * 1.4));
  const int cy = baseCy + bob;

  // Hanging cord from top screen edge
  renderer.fillRect(cx - 1, 0, 2, std::max(1, cy - 11), true);

  // Ground shadow ellipse at y = 33
  renderer.fillRect(cx - 6, 33, 12, 2, true);

  // Isometric hexagon vertices around (cx, cy):
  // Top: (cx, cy - 11), Upper-Right: (cx + 11, cy - 5), Lower-Right: (cx + 11, cy + 6)
  // Bottom: (cx, cy + 12), Lower-Left: (cx - 11, cy + 6), Upper-Left: (cx - 11, cy - 5)
  // Fill right face black for 3D shading
  const int rightX[4] = {cx, cx + 11, cx + 11, cx};
  const int rightY[4] = {cy, cy - 5, cy + 6, cy + 12};
  renderer.fillPolygon(rightX, rightY, 4, true);

  // Fill left face with 50% checkerboard dither
  for (int dy = -5; dy <= 11; ++dy) {
    for (int dx = -10; dx < 0; ++dx) {
      const int topY = -5 + (-dx * 0) / 11;
      const int midY = (dx * 5) / 11;
      const int botY = 12 + (dx * 6) / 11;
      if (dy >= std::min(topY, midY) && dy <= botY) {
        const int px = cx + dx;
        const int py = cy + dy;
        renderer.drawPixel(px, py, ((px + py) & 1) == 0);
      }
    }
  }

  // Outer 2px isometric outline + inner Y-edges
  renderer.drawLine(cx, cy - 11, cx + 11, cy - 5, 2, true);
  renderer.drawLine(cx + 11, cy - 5, cx + 11, cy + 6, 2, true);
  renderer.drawLine(cx + 11, cy + 6, cx, cy + 12, 2, true);
  renderer.drawLine(cx, cy + 12, cx - 11, cy + 6, 2, true);
  renderer.drawLine(cx - 11, cy + 6, cx - 11, cy - 5, 2, true);
  renderer.drawLine(cx - 11, cy - 5, cx, cy - 11, 2, true);

  // Inner Y-seams
  renderer.drawLine(cx - 10, cy - 5, cx, cy, true);
  renderer.drawLine(cx + 10, cy - 5, cx, cy, true);
  renderer.drawLine(cx, cy, cx, cy + 11, true);
}

// Draws a procedural 1-bit 2:3 book cover inside (covX, covY, covW, covH) when no BMP cover exists
void drawProceduralBookCover(const GfxRenderer& renderer, int covX, int covY, int covW, int covH,
                             bool hasContinueReading) {
  renderer.fillRect(covX, covY, covW, covH, false);
  renderer.drawRect(covX, covY, covW, covH, true);
  // Book spine on left (7px wide with vertical ridges)
  renderer.fillRect(covX, covY, 7, covH, true);
  renderer.drawLine(covX + 2, covY + 2, covX + 2, covY + covH - 3, false);

  // Halftone art area in upper cover
  const int artX = covX + 11;
  const int artY = covY + 6;
  const int artW = covW - 16;
  const int artH = covH - 12;
  fillHalftoneRect(renderer, artX, artY, artW, artH, 1, true);
  renderer.drawRect(artX, artY, artW, artH, true);

  // Geometric emblem in center of cover
  const int midX = artX + artW / 2;
  const int midY = artY + artH / 2 - 6;
  fillCircle(renderer, midX, midY, 16, true);
  drawCircle(renderer, midX, midY, 13, false);
  if (hasContinueReading) {
    // Open book / geometric diamond inside circle
    const int dx[4] = {midX, midX + 8, midX, midX - 8};
    const int dy[4] = {midY - 8, midY, midY + 8, midY};
    renderer.fillPolygon(dx, dy, 4, false);
    fillCircle(renderer, midX, midY, 3, true);
  } else {
    renderer.drawLine(midX - 5, midY, midX + 5, midY, false);
    renderer.drawLine(midX, midY - 5, midX, midY + 5, false);
  }

  // Decorative title band on cover
  renderer.fillRect(artX + 5, covY + covH - 26, artW - 10, 14, false);
  renderer.drawRect(artX + 5, covY + covH - 26, artW - 10, 14, true);
  renderer.fillRect(artX + 10, covY + covH - 21, artW - 20, 2, true);
  renderer.fillRect(artX + 14, covY + covH - 17, artW - 28, 2, true);
}

// Draws the 2:3 portrait book cover at byte-aligned (kCoverSlotX=48, covY, kCoverSlotW=80, covH)
// with 3D page edges on the right and bottom. Caches the rendered 80xcovH region in RAM so
// 30 FPS animations never hit SD card I/O.
void drawBookCover2x3WithEdges(GfxRenderer& renderer, int covY, int covH, const std::vector<RecentBook>& recentBooks,
                               bool cardInverted, bool& coverRendered, bool& coverBufferStored,
                               const std::function<bool()>& storeCoverBuffer) {
  (void)coverBufferStored;
  (void)storeCoverBuffer;

  const bool hasContinueReading = !recentBooks.empty();
  const char* rawCoverPath = hasContinueReading ? recentBooks[0].coverBmpPath.c_str() : "";
  const std::string cacheKey =
      !hasContinueReading
          ? "__empty__"
          : (rawCoverPath[0] == '\0' ? "__procedural__" : UITheme::getCoverThumbPath(rawCoverPath, 120));

  if (!coverRendered) {
    g_cachedCoverSlot.valid = false;
    g_cachedCoverSlot.captionLineCount = 0;
  }

  if (hasContinueReading) {
    if (!coverRendered || g_cachedCoverSlot.progressBookPath != recentBooks[0].path) {
      g_cachedCoverSlot.progressBookPath = recentBooks[0].path;
      g_cachedCoverSlot.progressPercent = loadBookProgress(recentBooks[0].path);
    }
  } else {
    g_cachedCoverSlot.progressBookPath.clear();
    g_cachedCoverSlot.progressPercent = -1;
  }

  bool restoredFromSlotCache = false;
  if (g_cachedCoverSlot.valid && g_cachedCoverSlot.bmpPath == cacheKey && g_cachedCoverSlot.height == covH) {
    restoredFromSlotCache = renderer.copyBufferToRegion(kCoverSlotX, covY, kCoverSlotW, covH, g_cachedCoverSlot.pixels,
                                                        sizeof(g_cachedCoverSlot.pixels));
  }

  if (!restoredFromSlotCache) {
    bool drewBmp = false;
    if (hasContinueReading && rawCoverPath[0] != '\0') {
      HalFile file;
      if (Storage.openFileForRead("HOME", cacheKey, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.fillRect(kCoverSlotX, covY, kCoverSlotW, covH, false);
          BaseTheme::drawCoverThumbFill(renderer, bitmap, Rect{kCoverSlotX, covY, kCoverSlotW, covH});
          renderer.drawRect(kCoverSlotX, covY, kCoverSlotW, covH, true);
          drewBmp = true;
        }
        file.close();
      }
    }
    if (!drewBmp) {
      drawProceduralBookCover(renderer, kCoverSlotX, covY, kCoverSlotW, covH, hasContinueReading);
    }

    if (covH <= kCoverSlotMaxH) {
      if (renderer.copyRegionToBuffer(kCoverSlotX, covY, kCoverSlotW, covH, g_cachedCoverSlot.pixels,
                                      sizeof(g_cachedCoverSlot.pixels))) {
        g_cachedCoverSlot.bmpPath = cacheKey;
        g_cachedCoverSlot.height = covH;
        g_cachedCoverSlot.valid = true;
      }
    }
    coverRendered = true;
  }

  // Ensure crisp border around the cover art (white outer hairline when card is black)
  if (cardInverted) {
    renderer.drawRect(kCoverSlotX - 1, covY - 1, kCoverSlotW + 2, covH + 2, false);
  }
  renderer.drawRect(kCoverSlotX, covY, kCoverSlotW, covH, true);

  // 3D page block edges on right (4px) and bottom (3px)
  renderer.fillRect(kCoverSlotX + kCoverSlotW, covY + 2, 4, covH, false);
  renderer.drawRect(kCoverSlotX + kCoverSlotW, covY + 2, 4, covH, true);
  for (int py = covY + 5; py < covY + covH; py += 3) {
    renderer.drawPixel(kCoverSlotX + kCoverSlotW + 1, py, true);
    renderer.drawPixel(kCoverSlotX + kCoverSlotW + 2, py, true);
  }
  renderer.fillRect(kCoverSlotX + 2, covY + covH, kCoverSlotW + 2, 3, false);
  renderer.drawRect(kCoverSlotX + 2, covY + covH, kCoverSlotW + 2, 3, true);
}

// Draws up to 2 lines of the book title (or 1 line title + 1 line author) and a 1-bit progress bar
// at the bottom of the left book card.
void drawBookCardCaptionAndProgress(const GfxRenderer& renderer, int cardX, int cardW, int textTopY, int progressBarY,
                                    const std::vector<RecentBook>& recentBooks, bool cardInverted) {
  const bool ink = !cardInverted;
  const int maxTextW = cardW - 16;

  if (!recentBooks.empty()) {
    const std::string& title = recentBooks[0].title;
    const std::string& author = recentBooks[0].author;
    if (g_cachedCoverSlot.captionLineCount == 0 || g_cachedCoverSlot.captionMaxW != maxTextW ||
        g_cachedCoverSlot.captionKey != recentBooks[0].path) {
      g_cachedCoverSlot.captionKey = recentBooks[0].path;
      g_cachedCoverSlot.captionMaxW = maxTextW;
      auto lines = renderer.wrappedText(SMALL_FONT_ID, title.c_str(), maxTextW, 2, EpdFontFamily::BOLD);
      g_cachedCoverSlot.captionLineCount = std::min<int>(2, static_cast<int>(lines.size()));
      for (int i = 0; i < g_cachedCoverSlot.captionLineCount; ++i) {
        g_cachedCoverSlot.captionLines[i] = lines[i];
      }
      g_cachedCoverSlot.captionAuthor =
          (g_cachedCoverSlot.captionLineCount == 1 && !author.empty())
              ? renderer.truncatedText(SMALL_FONT_ID, author.c_str(), maxTextW, EpdFontFamily::REGULAR)
              : std::string{};
    }

    const int lineStep = 13;
    int curY = textTopY;
    for (int i = 0; i < g_cachedCoverSlot.captionLineCount; ++i) {
      const int w = renderer.getTextWidth(SMALL_FONT_ID, g_cachedCoverSlot.captionLines[i].c_str(), EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, cardX + (cardW - w) / 2, curY, g_cachedCoverSlot.captionLines[i].c_str(), ink,
                        EpdFontFamily::BOLD);
      curY += lineStep;
    }
    if (g_cachedCoverSlot.captionLineCount == 1 && !g_cachedCoverSlot.captionAuthor.empty()) {
      const int aw = renderer.getTextWidth(SMALL_FONT_ID, g_cachedCoverSlot.captionAuthor.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, cardX + (cardW - aw) / 2, curY, g_cachedCoverSlot.captionAuthor.c_str(), ink,
                        EpdFontFamily::REGULAR);
    }
  } else {
    const std::string noBook = renderer.truncatedText(SMALL_FONT_ID, tr(STR_NO_OPEN_BOOK), maxTextW, EpdFontFamily::BOLD);
    const int w1 = renderer.getTextWidth(SMALL_FONT_ID, noBook.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, cardX + (cardW - w1) / 2, textTopY, noBook.c_str(), ink, EpdFontFamily::BOLD);

    const std::string startTxt =
        renderer.truncatedText(SMALL_FONT_ID, tr(STR_START_READING), maxTextW, EpdFontFamily::REGULAR);
    const int w2 = renderer.getTextWidth(SMALL_FONT_ID, startTxt.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, cardX + (cardW - w2) / 2, textTopY + 13, startTxt.c_str(), ink,
                      EpdFontFamily::REGULAR);
  }

  // Rounded 1-bit progress bar at progressBarY
  const int barX = cardX + 16;
  const int barW = cardW - 32;
  constexpr int barH = 6;
  renderer.fillRoundedRect(barX, progressBarY, barW, barH, 3, cardInverted ? Color::Black : Color::White);
  renderer.drawRoundedRect(barX, progressBarY, barW, barH, 1, 3, ink);
  if (!recentBooks.empty() && g_cachedCoverSlot.progressPercent >= 0) {
    const int clampedPct = std::min(100, g_cachedCoverSlot.progressPercent);
    const int fillW = std::max(clampedPct > 0 ? 4 : 2, (barW - 4) * clampedPct / 100);
    if (fillW > 0) {
      renderer.fillRect(barX + 2, progressBarY + 2, fillW, barH - 4, ink);
    }
  }
}

// Larger (~56x44px) 1-bit icons for the Playful 2x2 tile grid that animate in response to selection
void drawPlayfulAnimatedTileIcon(const GfxRenderer& renderer, UIIcon icon, int cx, int cy, int tick, bool selected) {
  const bool fg = !selected;
  const bool bg = selected;
  const bool animating = selected && tick > 0;
  // Snappy 2px upward pop on the first frames when a tile becomes selected
  const int selectPopY = (animating && tick <= 4) ? -2 : 0;
  cy += selectPopY;

  switch (icon) {
    case UIIcon::Library: {
      // Large 3-volume hardcover stack (56x44px) that fans and levitates when selected
      const int floatY = animating ? static_cast<int>(std::round(std::sin(tick * 0.16) * 2.0)) : 0;
      const int midShift = animating ? static_cast<int>(std::round(std::sin(tick * 0.22) * 3.0)) : 0;
      const int topShift = -midShift;
      const int by = cy + floatY;

      // Bottom hardcover volume (w = 56, h = 13)
      renderer.fillRoundedRect(cx - 28, by + 8, 56, 13, 2, fg ? Color::Black : Color::White);
      renderer.fillRect(cx - 21, by + 10, 47, 9, bg);
      renderer.drawLine(cx - 25, by + 10, cx - 25, by + 18, bg);
      renderer.drawLine(cx - 19, by + 13, cx + 23, by + 13, fg);
      renderer.drawLine(cx - 19, by + 16, cx + 21, by + 16, fg);

      // Middle hardcover volume (w = 52, h = 13, spine on right)
      const int mx = cx - 25 + midShift;
      renderer.fillRoundedRect(mx, by - 5, 52, 13, 2, fg ? Color::Black : Color::White);
      renderer.fillRect(mx + 2, by - 3, 43, 9, bg);
      renderer.drawLine(mx + 48, by - 3, mx + 48, by + 5, bg);
      renderer.drawLine(mx + 5, by, mx + 42, by, fg);
      renderer.drawLine(mx + 7, by + 3, mx + 42, by + 3, fg);

      // Top hardcover volume (w = 54, h = 13, spine on left)
      const int tx = cx - 27 + topShift;
      renderer.fillRoundedRect(tx, by - 18, 54, 13, 2, fg ? Color::Black : Color::White);
      renderer.fillRect(tx + 7, by - 16, 45, 9, bg);
      renderer.drawLine(tx + 3, by - 16, tx + 3, by - 8, bg);
      renderer.drawLine(tx + 9, by - 13, tx + 49, by - 13, fg);
      renderer.drawLine(tx + 9, by - 10, tx + 47, by - 10, fg);

      // Swaying forked bookmark ribbon hanging from top volume
      const int sway = animating ? static_cast<int>(std::round(std::cos(tick * 0.22) * 2.5)) : 0;
      const int rx = tx + 35 + sway;
      const int ry = by - 11;
      renderer.fillRect(rx - 1, ry, 9, 15, bg);
      renderer.fillRect(rx, ry, 7, 14, fg);
      renderer.drawPixel(rx + 3, ry + 13, bg);
      renderer.drawLine(rx + 2, ry + 12, rx + 4, ry + 12, bg);
      break;
    }

    case UIIcon::Folder: {
      // Large SD file folder (56x42px) with a dog-eared document sheet that rises when selected
      const int peek = animating ? (-5 + static_cast<int>(std::round(std::sin(tick * 0.20) * 3.5))) : 0;

      // Folder back tab & back plate
      renderer.fillRoundedRect(cx - 28, cy - 20, 22, 8, 2, fg ? Color::Black : Color::White);
      renderer.fillRoundedRect(cx - 28, cy - 14, 56, 35, 3, fg ? Color::Black : Color::White);

      // Peeking document sheet (w = 42, h = 24) with folded corner and ruled text lines
      const int docX = cx - 21;
      const int docY = cy - 15 + peek;
      renderer.fillRect(docX, docY, 42, 24, bg);
      renderer.drawRect(docX, docY, 42, 24, fg);
      renderer.drawRect(docX + 1, docY + 1, 40, 22, fg);
      // Dog-ear fold in top-right of document
      renderer.fillRect(docX + 34, docY + 2, 6, 6, fg);
      renderer.drawLine(docX + 34, docY + 7, docX + 39, docY + 2, bg);
      // Text lines on document (shimmering length when animating)
      const int lineGrow = animating ? ((tick / 4) % 6) : 4;
      renderer.fillRect(docX + 6, docY + 5, 22, 2, fg);
      renderer.fillRect(docX + 6, docY + 10, 28, 2, fg);
      renderer.fillRect(docX + 6, docY + 15, 20 + lineGrow, 2, fg);

      // Folder front pocket with double stitched border
      renderer.fillRoundedRect(cx - 28, cy - 1, 56, 22, 3, fg ? Color::Black : Color::White);
      renderer.drawRect(cx - 25, cy + 2, 50, 16, bg);
      renderer.drawLine(cx - 24, cy + 3, cx + 23, cy + 3, bg);
      break;
    }

    case UIIcon::Plugins:
    case UIIcon::Transfer: {
      // Large wireless sync transceiver (58x44px) with outward-pulsing 2px signal waves when selected
      const int wavePhase = animating ? ((tick / 4) % 4) : 2;

      // Base transceiver housing (w = 30, h = 16)
      renderer.fillRoundedRect(cx - 15, cy + 5, 30, 16, 3, fg ? Color::Black : Color::White);
      renderer.fillRect(cx - 12, cy + 8, 24, 10, bg);
      fillCircle(renderer, cx - 6, cy + 13, 2, fg);
      fillCircle(renderer, cx, cy + 13, 2, (animating && (tick / 3) % 2 == 0) ? bg : fg);
      fillCircle(renderer, cx + 6, cy + 13, 2, fg);

      // Antenna mast + transmitter orb at (cx, cy - 7)
      renderer.fillRect(cx - 2, cy - 6, 4, 12, fg);
      fillCircle(renderer, cx, cy - 7, 5, fg);
      fillCircle(renderer, cx, cy - 7, 2, bg);

      // 3 concentric left & right 2px-thick signal arcs (r = 11, 18, 25)
      for (int ring = 1; ring <= 3; ++ring) {
        if (wavePhase >= ring || (!animating && ring <= 2)) {
          const int r = 4 + ring * 7;
          for (int side = -1; side <= 1; side += 2) {
            const double centerDeg = (side < 0) ? 180.0 : 0.0;
            for (int a = -46; a <= 46; a += 4) {
              const double rad = (centerDeg + a) * 3.14159265 / 180.0;
              const int px = cx + static_cast<int>(std::round(std::cos(rad) * r));
              const int py = (cy - 7) + static_cast<int>(std::round(std::sin(rad) * r));
              renderer.fillRect(px - 1, py - 1, 2, 2, fg);
            }
          }
        }
      }
      break;
    }

    case UIIcon::Settings:
    default: {
      // Large 8-tooth Playdate mechanical gear (48x48px) that rotates when selected
      const double baseAngle = animating ? (tick * 0.08) : 0.0;
      for (int t = 0; t < 8; ++t) {
        const double a = baseAngle + t * (3.14159265 / 4.0);
        const int tx1 = cx + static_cast<int>(std::round(std::cos(a) * 16.0));
        const int ty1 = cy + static_cast<int>(std::round(std::sin(a) * 16.0));
        const int tx2 = cx + static_cast<int>(std::round(std::cos(a) * 19.0));
        const int ty2 = cy + static_cast<int>(std::round(std::sin(a) * 19.0));
        fillCircle(renderer, tx1, ty1, 5, fg);
        fillCircle(renderer, tx2, ty2, 4, fg);
      }
      fillCircle(renderer, cx, cy, 16, fg);
      fillCircle(renderer, cx, cy, 12, bg);
      fillCircle(renderer, cx, cy, 9, fg);
      // 4 rotating spoke rivets inside the recessed channel
      for (int s = 0; s < 4; ++s) {
        const double sa = baseAngle + s * (3.14159265 / 2.0) + (3.14159265 / 8.0);
        const int rx = cx + static_cast<int>(std::round(std::cos(sa) * 10.5));
        const int ry = cy + static_cast<int>(std::round(std::sin(sa) * 10.5));
        fillCircle(renderer, rx, ry, 2, fg);
      }
      fillCircle(renderer, cx, cy, 5, bg);
      drawCircle(renderer, cx, cy, 3, fg);
      break;
    }
  }
}

int computeHudSlideOffset(unsigned long now) {
  if (!g_hudInitialized) {
    return 0;
  }
  const unsigned long elapsed = now - g_lastHudInputMs;
  if (elapsed <= kHudHoldMs) {
    return 0;
  }
  if (elapsed >= kHudHoldMs + kHudSlideMs) {
    return kHudHeight;
  }
  const int offset = static_cast<int>(((elapsed - kHudHoldMs) * kHudHeight) / kHudSlideMs);
  return std::min(kHudHeight, std::max(0, offset));
}

// Auto-hiding bottom (B)/(A) button bar shared by Crossplay and Playful.
// Wakes on any button/crank activity and slides down off the bottom edge after 2.8s.
void drawAutoHidePlaydateHud(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                             const char* btn4, bool darkStyle) {
  if ((btn1 == nullptr || btn1[0] == '\0') && (btn2 == nullptr || btn2[0] == '\0')) {
    g_lastHudSlideOffset = kHudHeight;
    return;
  }

  const unsigned long now = millis();
  if (!g_hudInitialized || gpio.rawInputActive() || gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    g_lastHudInputMs = now;
    g_hudInitialized = true;
  }

  const int slideOffset = computeHudSlideOffset(now);
  g_lastHudSlideOffset = slideOffset;
  if (slideOffset >= kHudHeight) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int hy = screenH - kHudHeight + slideOffset;

  const bool bgBlack = darkStyle;
  const bool fgInk = !bgBlack;

  renderer.fillRect(0, hy, screenW, kHudHeight, bgBlack);
  renderer.fillRect(0, hy, screenW, 2, true);

  const int centerY = hy + kHudHeight / 2;
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  const int textY = centerY - textH / 2;
  const int badgeLetterY = textY - 2;

  // Left: (B) circle badge + btn1
  int leftRightEdge = 12;
  if (btn1 != nullptr && btn1[0] != '\0') {
    const int badgeCx = 20;
    fillCircle(renderer, badgeCx, centerY, 8, fgInk);
    const int bW = renderer.getTextWidth(SMALL_FONT_ID, "B", EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, badgeCx - bW / 2, badgeLetterY, "B", bgBlack, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, 33, textY - 1, btn1, fgInk, EpdFontFamily::BOLD);
    leftRightEdge = 33 + renderer.getTextWidth(SMALL_FONT_ID, btn1, EpdFontFamily::BOLD) + 12;
  }

  // Right: (A) circle badge + btn2
  int rightLeftEdge = screenW - 12;
  if (btn2 != nullptr && btn2[0] != '\0') {
    const int labelW = renderer.getTextWidth(SMALL_FONT_ID, btn2, EpdFontFamily::BOLD);
    const int labelX = screenW - 14 - labelW;
    const int badgeCx = labelX - 13;
    fillCircle(renderer, badgeCx, centerY, 8, fgInk);
    const int aW = renderer.getTextWidth(SMALL_FONT_ID, "A", EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, badgeCx - aW / 2, badgeLetterY, "A", bgBlack, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, labelX, textY - 1, btn2, fgInk, EpdFontFamily::BOLD);
    rightLeftEdge = badgeCx - 16;
  }

  // Center: D-Pad / Crank navigation hint if space allows
  if ((btn3 != nullptr && btn3[0] != '\0') || (btn4 != nullptr && btn4[0] != '\0')) {
    const char* navHint = tr(STR_PLAYDATE_DPAD_CRANK);
    const int navW = renderer.getTextWidth(SMALL_FONT_ID, navHint, EpdFontFamily::REGULAR);
    const int availW = rightLeftEdge - leftRightEdge;
    if (availW >= navW + 8) {
      renderer.drawText(SMALL_FONT_ID, leftRightEdge + (availW - navW) / 2, textY - 1, navHint, fgInk,
                        EpdFontFamily::REGULAR);
    }
  }
}

}  // namespace

namespace PlaydateThemeAnim {

void noteActivity() {
  g_lastHudInputMs = millis();
  g_hudInitialized = true;
}

bool isPlaydateThemeActive() {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CROSSPLAY ||
         SETTINGS.uiTheme == CrossPointSettings::UI_THEME::PLAYFUL;
}

bool isHudActive() {
  if (!isPlaydateThemeActive()) {
    return false;
  }
  const unsigned long now = millis();
  if (!g_hudInitialized || gpio.rawInputActive() || gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    g_lastHudInputMs = now;
    g_hudInitialized = true;
    return true;
  }
  return computeHudSlideOffset(now) < kHudHeight;
}

bool shouldUpdateHud() {
  if (!isPlaydateThemeActive()) {
    return false;
  }
  const unsigned long now = millis();
  if (gpio.rawInputActive() || gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    g_lastHudInputMs = now;
    g_hudInitialized = true;
  }
  if (!g_hudInitialized) {
    return false;
  }
  const int offset = computeHudSlideOffset(now);
  return offset != g_lastHudSlideOffset;
}

bool shouldUpdateHome() {
  if (!isPlaydateThemeActive()) {
    return false;
  }
  return animationTick() != g_lastHomeAnimTick || shouldUpdateHud();
}

bool isCrankPulseActive() { return playdate_hal::isCrankPulseActive(); }

}  // namespace PlaydateThemeAnim

// ============================================================================
// CrossplayTheme (Concept A: Playdate OS Hybrid)
// ============================================================================

void CrossplayTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                                const bool backButton) const {
  (void)rect;
  (void)subtitle;

  const int screenW = renderer.getScreenWidth();
  renderer.fillRect(0, 0, screenW, 40, false);

  const int tick = animationTick();
  // Hanging 3D cube icon on the left at cx = 22
  drawHangingCubeIcon(renderer, 22, 18, tick);

  // Shared Playdate graphical battery + percentage + clock on the right
  const int statusLeftX = drawPlaydateBatteryAndClock(renderer, screenW - 12, 19, false);

  // Title at x = 50 ("CrossPoint" on Home when !backButton, or screen title on sub-screens)
  constexpr int kTitleX = 50;
  const int maxTitleW = std::max(40, statusLeftX - kTitleX - 8);
  const char* displayTitle = (!backButton || title == nullptr || title[0] == '\0') ? tr(STR_CROSSPOINT) : title;
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const std::string truncTitle = renderer.truncatedText(UI_12_FONT_ID, displayTitle, maxTitleW, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, kTitleX, (38 - titleH) / 2, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Crisp 2px horizontal divider rule at y = 38
  renderer.fillRect(12, 38, screenW - 24, 2, true);
}

void CrossplayTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                         const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                         bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)rect;
  (void)bufferRestored;

  g_lastHomeAnimTick = animationTick();
  if (gpio.rawInputActive() || gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    PlaydateThemeAnim::noteActivity();
  }

  constexpr int cardX = 12;
  constexpr int cardY = 44;
  constexpr int cardW = 154;
  constexpr int cardH = 186;
  constexpr int covY = 50;
  constexpr int covH = 120;

  const bool hasContinueReading = !recentBooks.empty();
  const bool bookSelected =
      hasContinueReading && selectorIndex >= 0 && selectorIndex < static_cast<int>(recentBooks.size());

  // Hard-shadow Playdate card on the left
  drawHardShadowCard(renderer, cardX, cardY, cardW, cardH, 8, false, 3);

  // 2:3 portrait book cover (80x120 at byte-aligned x = 48) with 3D page edges
  drawBookCover2x3WithEdges(renderer, covY, covH, recentBooks, false, coverRendered, coverBufferStored,
                            storeCoverBuffer);

  // 2-line book title / author + rounded 1-bit progress bar underneath
  drawBookCardCaptionAndProgress(renderer, cardX, cardW, 175, 212, recentBooks, false);

  // Animated marching-ants dashed selection border inside the book card when Continue Reading is focused
  if (bookSelected) {
    drawMarchingAntsRect(renderer, cardX + 3, cardY + 3, cardW - 6, cardH - 6, g_lastHomeAnimTick, true);
  }
}

int CrossplayTheme::getMenuRowHeight(const GfxRenderer&) const { return CrossplayMetrics::values.menuRowHeight; }

void CrossplayTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rect;
  (void)rowIcon;

  constexpr int listX = 176;
  constexpr int listY = 44;
  constexpr int listW = 212;
  constexpr int listH = 186;

  const int rowGap = 4;
  const int rowH = buttonCount > 0 ? std::min(36, (listH - (buttonCount - 1) * rowGap) / buttonCount) : 33;
  const int step = rowH + rowGap;
  const int tick = animationTick();
  const int chevronBounce = static_cast<int>(std::round(std::sin(tick * 0.22) * 2.0));
  const int menuFontId = UI_10_FONT_ID;
  const int textH = renderer.getLineHeight(menuFontId);

  for (int i = 0; i < buttonCount; ++i) {
    const int ry = listY + i * step;
    const bool isSelected = (i == selectedIndex);
    const std::string label = buttonLabel(i);
    const std::string trunc = renderer.truncatedText(menuFontId, label.c_str(), listW - 34, EpdFontFamily::BOLD);
    const int ty = ry + (rowH - textH) / 2;

    if (isSelected) {
      renderer.fillRoundedRect(listX, ry, listW, rowH, 7, Color::Black);
      renderer.drawText(menuFontId, listX + 12, ty, trunc.c_str(), false, EpdFontFamily::BOLD);

      // Animated bouncing '>' chevron on right of selected pill
      const int cx = listX + listW - 16 + chevronBounce;
      const int cy = ry + rowH / 2;
      renderer.drawLine(cx - 4, cy - 4, cx, cy, 2, false);
      renderer.drawLine(cx, cy, cx - 4, cy + 4, 2, false);
    } else {
      renderer.fillRoundedRect(listX, ry, listW, rowH, 7, Color::White);
      renderer.drawText(menuFontId, listX + 12, ty, trunc.c_str(), true, EpdFontFamily::BOLD);

      // Subtle right chevron + 50% checkerboard separator line
      const int cx = listX + listW - 16;
      const int cy = ry + rowH / 2;
      renderer.drawLine(cx - 3, cy - 3, cx, cy, true);
      renderer.drawLine(cx, cy, cx - 3, cy + 3, true);
      if (i < buttonCount - 1) {
        drawCheckerHLine(renderer, listX + 8, listX + listW - 8, ry + rowH + 1, true);
      }
    }
  }
}

void CrossplayTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  drawAutoHidePlaydateHud(renderer, btn1, btn2, btn3, btn4, false);
}

// ============================================================================
// PlayfulTheme (Concept B: Playdate Launcher / Deck)
// ============================================================================

void PlayfulTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                              const bool backButton) const {
  (void)rect;
  (void)subtitle;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (!backButton) {
    // On the Home screen, paint the full 1-bit horizontal pin-striped Playdate Launcher backdrop
    fillHalftoneRect(renderer, 0, 0, screenW, screenH, 3, true);
  } else {
    renderer.fillRect(0, 0, screenW, 36, false);
  }

  // Solid black rounded header pill (x = 8, y = 4, w = screenW - 16, h = 28, r = 7)
  renderer.fillRoundedRect(8, 4, screenW - 16, 28, 7, Color::Black);

  // Exact same graphical Playdate battery icon + percentage + clock as Crossplay (rendered in white on black)
  const int statusLeftX = drawPlaydateBatteryAndClock(renderer, screenW - 16, 18, true);

  constexpr int kTitleX = 18;
  const int maxTitleW = std::max(40, statusLeftX - kTitleX - 8);
  char upperBrand[32] = {};
  const char* displayTitle = title;
  if (!backButton || title == nullptr || title[0] == '\0') {
    const char* rawBrand = tr(STR_CROSSPOINT);
    size_t i = 0;
    for (; rawBrand[i] != '\0' && i + 1 < sizeof(upperBrand); ++i) {
      upperBrand[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(rawBrand[i])));
    }
    upperBrand[i] = '\0';
    displayTitle = upperBrand;
  }
  const int titleH = renderer.getLineHeight(UI_10_FONT_ID);
  const std::string truncTitle = renderer.truncatedText(UI_10_FONT_ID, displayTitle, maxTitleW, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, kTitleX, 4 + (28 - titleH) / 2, truncTitle.c_str(), false, EpdFontFamily::BOLD);
}

void PlayfulTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)rect;
  (void)bufferRestored;

  g_lastHomeAnimTick = animationTick();
  if (gpio.rawInputActive() || gpio.wasAnyPressed() || gpio.wasAnyReleased()) {
    PlaydateThemeAnim::noteActivity();
  }

  constexpr int cardX = 10;
  constexpr int cardY = 38;
  constexpr int cardW = 152;
  constexpr int cardH = 194;
  constexpr int covY = 64;
  constexpr int covH = 113;

  const bool hasContinueReading = !recentBooks.empty();
  const bool bookSelected =
      hasContinueReading && selectorIndex >= 0 && selectorIndex < static_cast<int>(recentBooks.size());

  // Tall full-book Continue Reading card on the left (inverts to solid black when selected)
  drawHardShadowCard(renderer, cardX, cardY, cardW, cardH, 8, bookSelected, 3);

  // Top mini-pill badge inside the tall card
  renderer.fillRoundedRect(cardX + 8, cardY + 5, cardW - 16, 15, 4, bookSelected ? Color::White : Color::Black);
  const char* fullContinue = tr(STR_CONTINUE_READING);
  const char* rawBadge =
      renderer.getTextWidth(SMALL_FONT_ID, fullContinue, EpdFontFamily::BOLD) <= (cardW - 22) ? fullContinue : tr(STR_RESUME);
  const std::string badgeText = renderer.truncatedText(SMALL_FONT_ID, rawBadge, cardW - 22, EpdFontFamily::BOLD);
  const int badgeW = renderer.getTextWidth(SMALL_FONT_ID, badgeText.c_str(), EpdFontFamily::BOLD);
  const int badgeH = renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, cardX + (cardW - badgeW) / 2, cardY + 5 + (15 - badgeH) / 2 - 1, badgeText.c_str(),
                    bookSelected, EpdFontFamily::BOLD);

  // 2:3 portrait book cover (80x113 at byte-aligned x = 48) with 3D page edges
  drawBookCover2x3WithEdges(renderer, covY, covH, recentBooks, bookSelected, coverRendered, coverBufferStored,
                            storeCoverBuffer);

  // 2-line book title / author + rounded 1-bit progress bar underneath
  drawBookCardCaptionAndProgress(renderer, cardX, cardW, 181, 217, recentBooks, bookSelected);

  // Animated marching-ants selection border when focused
  if (bookSelected) {
    drawMarchingAntsRect(renderer, cardX + 3, cardY + 3, cardW - 6, cardH - 6, g_lastHomeAnimTick, false);
  }
}

int PlayfulTheme::getMenuRowHeight(const GfxRenderer&) const { return PlayfulMetrics::values.menuRowHeight; }

void PlayfulTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                  const std::function<std::string(int index)>& buttonLabel,
                                  const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rect;

  constexpr int gridX = 172;
  constexpr int gridY = 38;
  constexpr int tileW = 104;
  constexpr int tileH = 92;
  constexpr int gapX = 10;
  constexpr int gapY = 10;
  constexpr int labelStripH = 22;

  const unsigned long now = millis();
  if (selectedIndex != g_lastPlayfulSelectedIndex) {
    g_lastPlayfulSelectedIndex = selectedIndex;
    g_playfulSelectStartMs = now;
    PlaydateThemeAnim::noteActivity();
  }

  const int tick = animationTick();
  const int selectedTick =
      (tick > 0) ? (static_cast<int>((now - g_playfulSelectStartMs) / 33UL) + 1) : 0;
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  const int count = std::min(buttonCount, 4);

  for (int i = 0; i < count; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int tx = gridX + col * (tileW + gapX);
    const int ty = gridY + row * (tileH + gapY);
    const bool isSelected = (i == selectedIndex);
    const int tileTick = isSelected ? selectedTick : 0;

    drawHardShadowCard(renderer, tx, ty, tileW, tileH, 8, isSelected, 3);

    // 25% halftone dot backdrop in upper icon area
    fillHalftoneRect(renderer, tx + 4, ty + 4, tileW - 8, tileH - labelStripH - 6, 1, !isSelected);

    // Larger 1-bit icon centered in the upper area; animates in response to tile selection
    const UIIcon icon = rowIcon ? rowIcon(i) : UIIcon::Settings;
    drawPlayfulAnimatedTileIcon(renderer, icon, tx + tileW / 2, ty + (tileH - labelStripH) / 2, tileTick, isSelected);

    // Bottom label strip (h = 22px)
    const int stripY = ty + tileH - labelStripH;
    renderer.fillRect(tx + 2, stripY, tileW - 4, labelStripH - 2, !isSelected);
    renderer.drawLine(tx + 2, stripY, tx + tileW - 3, stripY, isSelected);

    const std::string label = buttonLabel(i);
    const std::string trunc = renderer.truncatedText(SMALL_FONT_ID, label.c_str(), tileW - 10, EpdFontFamily::BOLD);
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, trunc.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, tx + (tileW - lw) / 2, stripY + (labelStripH - textH) / 2, trunc.c_str(),
                      isSelected, EpdFontFamily::BOLD);

    if (isSelected) {
      drawMarchingAntsRect(renderer, tx + 3, ty + 3, tileW - 6, tileH - 6, tick, false);
    }
  }
}

void PlayfulTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4) const {
  drawAutoHidePlaydateHud(renderer, btn1, btn2, btn3, btn4, true);
}

#endif  // defined(CROSSPOINT_PLAYDATE) && CROSSPOINT_PLAYDATE
