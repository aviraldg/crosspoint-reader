#include "Lyra3CoversTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
}  // namespace

int Lyra3CoversTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tileWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / 3;
  // Thumbs cover a (0.6*h, h) target box; in landscape the tile is wider than
  // 0.6 aspect, so request a taller thumb and let the draw crop vertically.
  return std::max(metrics.homeCoverHeight, (tileWidth - 2 * hPaddingInSelection) * 5 / 3 + 2);
}

void Lyra3CoversTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tileWidth = (rect.width - 2 * metrics.contentSidePadding) / 3;
  const int tileY = rect.y;
  const int titleLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int coverHeight =
      std::min(metrics.homeCoverHeight, std::max(24, rect.height - 2 * hPaddingInSelection - titleLineHeight - 5));
  const bool hasContinueReading = !recentBooks.empty();

  // Draw book card regardless, fill with message based on `hasContinueReading`
  // Draw cover image as background if available (inside the box)
  // Only load from SD on first render, then use stored buffer
  if (hasContinueReading) {
    if (!coverRendered) {
      for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), metrics.homeRecentBooksCount); i++) {
        std::string coverPath = recentBooks[i].coverBmpPath;
        bool hasCover = true;
        int tileX = rect.x + metrics.contentSidePadding + tileWidth * i;
        if (coverPath.empty()) {
          hasCover = false;
        } else {
          const std::string coverBmpPath = UITheme::getCoverThumbPath(coverPath, homeCoverThumbHeight(renderer));

          // First time: load cover from SD and render
          HalFile file;
          if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              // Fill the fixed tile 1:1 and crop the overflow; the old crop
              // factor went negative for covers narrower than the tile.
              drawCoverThumbFill(renderer, bitmap,
                                 Rect{tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                                      tileWidth - 2 * hPaddingInSelection, coverHeight});
            } else {
              hasCover = false;
            }
            file.close();
          }
        }
        // Draw either way
        renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection, tileWidth - 2 * hPaddingInSelection,
                          coverHeight, true);

        if (!hasCover) {
          drawCoverPlaceholder(renderer, Rect{tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                                              tileWidth - 2 * hPaddingInSelection, coverHeight});
        }
      }

      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }

    const int maxTitleLines = metrics.homeCoverHeight < 100 ? 1 : 3;
    for (int i = 0; i < std::min(static_cast<int>(recentBooks.size()), metrics.homeRecentBooksCount); i++) {
      bool bookSelected = (selectorIndex == i);

      int tileX = rect.x + metrics.contentSidePadding + tileWidth * i;

      const int maxLineWidth = tileWidth - 2 * hPaddingInSelection;

      auto titleLines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), maxLineWidth, maxTitleLines);

      const int dynamicBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight;
      // Add a little padding below the text inside the selection box just like the top padding (5 + hPaddingSelection)
      const int dynamicTitleBoxHeight = dynamicBlockHeight + hPaddingInSelection + 5;

      if (bookSelected) {
        // Draw selection box
        renderer.fillRoundedRect(tileX, tileY, tileWidth, hPaddingInSelection, cornerRadius, true, true, false, false,
                                 Color::LightGray);
        renderer.fillRectDither(tileX, tileY + hPaddingInSelection, hPaddingInSelection, coverHeight, Color::LightGray);
        renderer.fillRectDither(tileX + tileWidth - hPaddingInSelection, tileY + hPaddingInSelection,
                                hPaddingInSelection, coverHeight, Color::LightGray);
        renderer.fillRoundedRect(tileX, tileY + coverHeight + hPaddingInSelection, tileWidth, dynamicTitleBoxHeight,
                                 cornerRadius, false, false, true, true, Color::LightGray);
      }

      int currentY = tileY + coverHeight + hPaddingInSelection + 5;
      for (const auto& line : titleLines) {
        renderer.drawText(SMALL_FONT_ID, tileX + hPaddingInSelection, currentY, line.c_str(), true);
        currentY += titleLineHeight;
      }
    }
  } else {
    drawEmptyRecents(renderer, rect);
  }
}
