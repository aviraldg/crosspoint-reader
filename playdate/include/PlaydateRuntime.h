#pragma once

extern "C" {
#include <pd_api.h>
}

namespace playdate_hal {

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

}  // namespace playdate_hal
