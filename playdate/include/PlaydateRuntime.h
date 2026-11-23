#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include <pd_api.h>
}

namespace playdate_hal {

struct HeapTelemetry {
  size_t liveBytes = 0;
  size_t peakBytes = 0;
  size_t budgetBytes = 0;
  size_t freeBytes = 0;
  size_t minFreeBytes = 0;
  uint32_t liveBlocks = 0;
  uint32_t totalAllocs = 0;
  uint32_t failedAllocs = 0;
  uint32_t badFrees = 0;
};

struct HttpTelemetry {
  uint32_t totalRequests = 0;
  uint32_t activeConnections = 0;
  uint32_t retiredConnections = 0;
  uint32_t reapedConnections = 0;
  uint32_t failedRequests = 0;
};

PlaydateAPI* pd();
void setPlaydateAPI(PlaydateAPI* api);

// Yields the current Playdate updateCallback frame back to Playdate OS (when
// called from inside updateCallback) and pumps input when synchronous callers
// wait on cooperative Playdate OS callbacks, dialogs, or network I/O.
void pumpMainLoop();

// True when the current frame's BTN_UP or BTN_DOWN edge was synthesized from a
// Playdate crank detent rather than the physical D-Pad.
bool isCrankPulseActive();

// Coordinates Playdate display->setRefreshRate() with CrossPoint's HalPowerManager,
// Activity::skipLoopDelay(), foreground reader state, and serial bridge transfers.
void setSerialFastMode(bool enabled);
void setLoopRefreshHints(bool skipLoopDelay, bool foregroundReader);
void applyRefreshRate();

// Heap, fiber stack, and HTTP connection diagnostics and deferred cleanup.
void resetHeapBaseline();
HeapTelemetry getHeapTelemetry();
HttpTelemetry getHttpTelemetry();
size_t getFiberStackSizeBytes();
size_t getFiberStackPeakUsedBytes();
size_t getFiberStackMinFreeBytes();
bool isFiberStackCanaryIntact();
void reapRetiredHttpConnections(bool forceAll = false);
void releasePendingHttpAccessContexts();
void logMemorySnapshot(const char* reason);

}  // namespace playdate_hal
