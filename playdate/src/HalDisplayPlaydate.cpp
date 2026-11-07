#include <HalDisplay.h>
#include <PlaydateRuntime.h>

#include <cstring>

HalDisplay display;

namespace {

alignas(4) uint8_t gFrameBufferStorage[HalDisplay::BUFFER_SIZE];
uint8_t* gActiveFrameBuffer = gFrameBufferStorage;
bool gStorageLent = false;
bool gInverted = false;

void blitImageToFrameBuffer(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool transparent) {
  if (!gActiveFrameBuffer || !imageData) return;
  const uint16_t imageWidthBytes = (w + 7) / 8;

  if ((x & 7) == 0) {
    const uint16_t xByte = x / 8;
    for (uint16_t row = 0; row < h; row++) {
      const uint16_t destY = y + row;
      if (destY >= HalDisplay::DISPLAY_HEIGHT) break;
      const uint32_t destOffset = static_cast<uint32_t>(destY) * HalDisplay::DISPLAY_WIDTH_BYTES + xByte;
      const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
      for (uint16_t col = 0; col < imageWidthBytes; col++) {
        if ((xByte + col) >= HalDisplay::DISPLAY_WIDTH_BYTES) break;
        const uint8_t srcByte = imageData[srcOffset + col];
        uint8_t& destByte = gActiveFrameBuffer[destOffset + col];
        const bool isPartialFinalByte = (col + 1 == imageWidthBytes) && ((w & 7) != 0);
        if (!isPartialFinalByte) {
          if (transparent) {
            destByte &= srcByte;
          } else {
            destByte = srcByte;
          }
          continue;
        }
        const uint8_t validMask = static_cast<uint8_t>(0xFFU << (8U - (w & 7)));
        if (transparent) {
          destByte &= static_cast<uint8_t>(srcByte | static_cast<uint8_t>(~validMask));
        } else {
          destByte = static_cast<uint8_t>((destByte & static_cast<uint8_t>(~validMask)) | (srcByte & validMask));
        }
      }
    }
    return;
  }

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= HalDisplay::DISPLAY_HEIGHT) break;
    const uint32_t srcRow = static_cast<uint32_t>(row) * imageWidthBytes;
    const uint32_t destRow = static_cast<uint32_t>(destY) * HalDisplay::DISPLAY_WIDTH_BYTES;
    for (uint16_t col = 0; col < w; col++) {
      const uint16_t destX = x + col;
      if (destX >= HalDisplay::DISPLAY_WIDTH) break;
      const uint8_t srcByte = imageData[srcRow + (col >> 3)];
      const bool white = ((srcByte >> (7 - (col & 7))) & 1) != 0;
      const uint8_t mask = static_cast<uint8_t>(0x80U >> (destX & 7));
      uint8_t& cell = gActiveFrameBuffer[destRow + (destX >> 3)];
      if (transparent) {
        if (!white) cell &= static_cast<uint8_t>(~mask);
      } else if (white) {
        cell |= mask;
      } else {
        cell &= static_cast<uint8_t>(~mask);
      }
    }
  }
}

void flushFrameBufferToPlaydate() {
  PlaydateAPI* pd = playdate_hal::pd();
  if (!pd || !pd->graphics || !gActiveFrameBuffer) return;

  uint8_t* lcdFrame = pd->graphics->getFrame();
  if (!lcdFrame) return;

  for (uint16_t y = 0; y < HalDisplay::DISPLAY_HEIGHT; ++y) {
    const uint8_t* srcRow = gActiveFrameBuffer + static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES;
    uint8_t* dstRow = lcdFrame + static_cast<size_t>(y) * LCD_ROWSIZE;
    if (!gInverted) {
      std::memcpy(dstRow, srcRow, HalDisplay::DISPLAY_WIDTH_BYTES);
    } else {
      for (uint16_t b = 0; b < HalDisplay::DISPLAY_WIDTH_BYTES; ++b) {
        dstRow[b] = static_cast<uint8_t>(~srcRow[b]);
      }
    }
  }

  pd->graphics->markUpdatedRows(0, HalDisplay::DISPLAY_HEIGHT - 1);
  pd->graphics->display();
}

}  // namespace

HalDisplay::HalDisplay() {
  std::memset(gFrameBufferStorage, 0xFF, sizeof(gFrameBufferStorage));
  gActiveFrameBuffer = gFrameBufferStorage;
}

HalDisplay::~HalDisplay() = default;

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }

const char* HalDisplay::controllerName() const { return "Playdate Sharp Memory LCD"; }

void HalDisplay::begin(bool seamless) {
  (void)seamless;
  gStorageLent = false;
  gActiveFrameBuffer = gFrameBufferStorage;
  std::memset(gFrameBufferStorage, 0xFF, sizeof(gFrameBufferStorage));
}

void HalDisplay::clearScreen(uint8_t color) const {
  if (gActiveFrameBuffer) {
    std::memset(gActiveFrameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  (void)fromProgmem;
  blitImageToFrameBuffer(imageData, x, y, w, h, /*transparent=*/false);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  (void)fromProgmem;
  blitImageToFrameBuffer(imageData, x, y, w, h, /*transparent=*/true);
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  (void)mode;
  (void)turnOffScreen;
  flushFrameBufferToPlaydate();
}

void HalDisplay::displayBufferAsync(RefreshMode mode) { displayBuffer(mode, false); }

void HalDisplay::waitRefreshComplete() {}

bool HalDisplay::supportsAsyncRefresh() const { return false; }

HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode mode) const {
  (void)mode;
  return GrayscaleCapabilities{};
}

bool HalDisplay::supportsAsyncGrayscaleBase() const { return false; }

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::setInverted(bool inverted) { gInverted = inverted; }

bool HalDisplay::toggleInverted() {
  gInverted = !gInverted;
  return gInverted;
}

bool HalDisplay::isInverted() const { return gInverted; }

void HalDisplay::deepSleep() {}

uint8_t* HalDisplay::getFrameBuffer() const { return gActiveFrameBuffer; }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (gStorageLent) {
    if (sizeOut) *sizeOut = 0;
    return nullptr;
  }
  gStorageLent = true;
  gActiveFrameBuffer = nullptr;
  if (sizeOut) *sizeOut = BUFFER_SIZE;
  return gFrameBufferStorage;
}

void HalDisplay::returnFrameBufferStorage() {
  if (!gStorageLent) return;
  gStorageLent = false;
  gActiveFrameBuffer = gFrameBufferStorage;
  std::memset(gFrameBufferStorage, 0xFF, BUFFER_SIZE);
}

bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode, RefreshMode fallback, bool turnOffScreen) {
  (void)mode;
  displayBuffer(fallback, turnOffScreen);
  return false;
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  displayBuffer(fallback, turnOffScreen);
}

void HalDisplay::preconditionGrayscale() {}

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  (void)lsbBuffer;
  (void)msbBuffer;
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { (void)lsbBuffer; }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { (void)msbBuffer; }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { (void)bwBuffer; }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { (void)turnOffScreen; }

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  (void)lsbPlane;
  (void)rows;
  (void)yStart;
  (void)numRows;
}

bool HalDisplay::supportsStripGrayscale() const { return false; }

bool HalDisplay::combinesGrayscaleBase() const { return false; }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }

uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }

uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }

uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
