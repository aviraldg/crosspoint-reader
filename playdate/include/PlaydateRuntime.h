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

}  // namespace playdate_hal
