#pragma once

#include <PlaydateRuntime.h>

namespace playdate_serial {

void registerSerialBridge(PlaydateAPI* pd);
void pumpSerialBridge();

}  // namespace playdate_serial
