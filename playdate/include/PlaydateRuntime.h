#pragma once

extern "C" {
#include <pd_api.h>
}

namespace playdate_hal {

PlaydateAPI* pd();
void setPlaydateAPI(PlaydateAPI* api);

// Pumps a single Playdate main-loop step when synchronous callers wait on
// cooperative callbacks or timers.
void pumpMainLoop();

}  // namespace playdate_hal
