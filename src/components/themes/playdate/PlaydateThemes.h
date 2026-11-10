#pragma once

#if defined(CROSSPOINT_PLAYDATE) && CROSSPOINT_PLAYDATE

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace CrossplayMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 22,
                                 .batteryHeight = 11,
                                 .topPadding = 0,
                                 .batteryBarHeight = 38,
                                 .headerHeight = 40,
                                 .verticalSpacing = 6,
                                 .previewPadding = 8,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 12,
                                 .listRowHeight = 32,
                                 .listWithSubtitleRowHeight = 44,
                                 .listRowGap = 3,
                                 .listRowRadius = 7,
                                 .listInset = 12,
                                 .listSidePadding = 12,
                                 .listSelectionStyle = 0,  // solid black pill
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = true,
                                 .headerSidePadding = 12,
                                 .headerUnderlineSize = 2,
                                 .headerTitleAlign = 0,  // left
                                 .headerBatterySide = 0,
                                 .headerShowsClock = true,
                                 .headerClockCentered = false,
                                 .menuRowHeight = 33,
                                 .menuSpacing = 4,
                                 .tabSpacing = 6,
                                 .tabBarHeight = 28,
                                 .tabPillFullSlot = true,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 4,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 120,
                                 .homeCoverTileHeight = 192,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = true,
                                 .homeMenuTopOffset = -192,
                                 .buttonHintsHeight = 0,
                                 .sideButtonHintsWidth = 0,
                                 .progressBarHeight = 10,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 4,
                                 .keyboardKeyHeight = 22,
                                 .keyboardKeySpacing = 2,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 90,
                                 .keyboardWidthPercent = 96,
                                 .popupTopOffsetRatio = 0.12f,
                                 .popupMarginX = 14,
                                 .popupMarginY = 10,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 8,
                                 .popupTextBold = true,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 4,
                                 .optionPopupInnerPadding = 12,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupDialogSideMargin = 16,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 8,
                                 .sheetRadius = 8,
                                 .capsuleRadius = 255};
}  // namespace CrossplayMetrics

namespace PlayfulMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 22,
                                 .batteryHeight = 11,
                                 .topPadding = 0,
                                 .batteryBarHeight = 34,
                                 .headerHeight = 36,
                                 .verticalSpacing = 6,
                                 .previewPadding = 8,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 10,
                                 .listRowHeight = 32,
                                 .listWithSubtitleRowHeight = 44,
                                 .listRowGap = 3,
                                 .listRowRadius = 7,
                                 .listInset = 10,
                                 .listSidePadding = 12,
                                 .listSelectionStyle = 0,  // solid black pill
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = true,
                                 .headerSidePadding = 10,
                                 .headerUnderlineSize = 0,
                                 .headerTitleAlign = 0,  // left
                                 .headerBatterySide = 0,
                                 .headerShowsClock = true,
                                 .headerClockCentered = false,
                                 .menuRowHeight = 92,
                                 .menuSpacing = 10,
                                 .tabSpacing = 6,
                                 .tabBarHeight = 28,
                                 .tabPillFullSlot = true,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 4,
                                 .homeTopPadding = 38,
                                 .homeCoverHeight = 114,
                                 .homeCoverTileHeight = 194,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = -194,
                                 .buttonHintsHeight = 0,
                                 .sideButtonHintsWidth = 0,
                                 .progressBarHeight = 10,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 4,
                                 .keyboardKeyHeight = 22,
                                 .keyboardKeySpacing = 2,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 90,
                                 .keyboardWidthPercent = 96,
                                 .popupTopOffsetRatio = 0.12f,
                                 .popupMarginX = 14,
                                 .popupMarginY = 10,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 8,
                                 .popupTextBold = true,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 4,
                                 .optionPopupInnerPadding = 12,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupDialogSideMargin = 16,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 8,
                                 .sheetRadius = 8,
                                 .capsuleRadius = 255};
}  // namespace PlayfulMetrics

namespace PlaydateThemeAnim {
// Records user input activity so the auto-hiding bottom button bar wakes up
// and slides away after inactivity.
void noteActivity();
// Returns true when the active theme is Crossplay or Playful and the bottom
// button bar is currently visible or mid-slide.
bool isHudActive();
// Returns true when the bottom HUD slide offset changed since the last frame
// and needs a redraw.
bool shouldUpdateHud();
// Returns true when a Playdate theme is active on Home and the 30 FPS animation
// tick has advanced since the last rendered frame.
bool shouldUpdateHome();
// Returns true when the active theme is Crossplay or Playful.
bool isPlaydateThemeActive();
// Returns true when the current frame's BTN_UP or BTN_DOWN edge came from a
// Playdate crank detent rather than the physical D-Pad.
bool isCrankPulseActive();
}  // namespace PlaydateThemeAnim

class CrossplayTheme : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  bool showsFileIcons() const override { return true; }
  int homeCoverThumbHeight(const GfxRenderer&) const override { return 120; }
};

class PlayfulTheme : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int getMenuRowHeight(const GfxRenderer& renderer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  bool showsFileIcons() const override { return true; }
  int homeCoverThumbHeight(const GfxRenderer&) const override { return 120; }
};

#endif  // defined(CROSSPOINT_PLAYDATE) && CROSSPOINT_PLAYDATE
