#include <HalGPIO.h>
#include <PlaydateRuntime.h>

#include <algorithm>
#include <cmath>

InputManager::InputManager() = default;

HalGPIO gpio;

namespace {

constexpr uint8_t kNumButtons = 7;
constexpr float kCrankDetentDegrees = 36.0f;

uint8_t gCurrState = 0;
uint8_t gPrevState = 0;
uint8_t gPressedEvents = 0;
uint8_t gReleasedEvents = 0;
unsigned long gPressStartMs = 0;
unsigned long gLastHeldMs = 0;
float gCrankAccumulatorDeg = 0.0f;
int gPendingCrankSteps = 0;
uint8_t gCrankPulseButton = HalGPIO::BTN_DOWN;
uint8_t gCrankPulseFramesRemaining = 0;

uint8_t mapPlaydateButtons(PDButtons buttons) {
  uint8_t mask = 0;
  if (buttons & kButtonB) mask |= (1U << HalGPIO::BTN_BACK);
  if (buttons & kButtonA) mask |= (1U << HalGPIO::BTN_CONFIRM);
  if (buttons & kButtonLeft) mask |= (1U << HalGPIO::BTN_LEFT);
  if (buttons & kButtonRight) mask |= (1U << HalGPIO::BTN_RIGHT);
  if (buttons & kButtonUp) mask |= (1U << HalGPIO::BTN_UP);
  if (buttons & kButtonDown) mask |= (1U << HalGPIO::BTN_DOWN);
  return mask;
}

}  // namespace

bool HalGPIO::isXteinkDevice() const { return false; }

bool HalGPIO::hasEdgeSideButtons() const { return false; }

void HalGPIO::begin() {
  _deviceType = DeviceType::X4;
  gCurrState = 0;
  gPrevState = 0;
  gPressedEvents = 0;
  gReleasedEvents = 0;
  gPressStartMs = 0;
  gLastHeldMs = 0;
  gCrankAccumulatorDeg = 0.0f;
  gPendingCrankSteps = 0;
  gCrankPulseFramesRemaining = 0;
}

void HalGPIO::prepareForDeepSleep() {}

void HalGPIO::update() {
  PlaydateAPI* pd = playdate_hal::pd();
  PDButtons current = static_cast<PDButtons>(0);
  if (pd && pd->system) {
    pd->system->getButtonState(&current, nullptr, nullptr);
    if (!pd->system->isCrankDocked()) {
      gCrankAccumulatorDeg += pd->system->getCrankChange();
      while (gCrankAccumulatorDeg >= kCrankDetentDegrees) {
        gCrankAccumulatorDeg -= kCrankDetentDegrees;
        if (gPendingCrankSteps < 8) gPendingCrankSteps++;
      }
      while (gCrankAccumulatorDeg <= -kCrankDetentDegrees) {
        gCrankAccumulatorDeg += kCrankDetentDegrees;
        if (gPendingCrankSteps > -8) gPendingCrankSteps--;
      }
    } else {
      gCrankAccumulatorDeg = 0.0f;
    }
  }

  uint8_t nextState = mapPlaydateButtons(current);

  if (gCrankPulseFramesRemaining > 0) {
    gCrankPulseFramesRemaining--;
  } else if (gPendingCrankSteps != 0 && (nextState & ((1U << BTN_UP) | (1U << BTN_DOWN))) == 0) {
    if (gPendingCrankSteps > 0) {
      gCrankPulseButton = BTN_DOWN;
      gPendingCrankSteps--;
    } else {
      gCrankPulseButton = BTN_UP;
      gPendingCrankSteps++;
    }
    nextState |= (1U << gCrankPulseButton);
    gCrankPulseFramesRemaining = 1;
  }

  gPrevState = gCurrState;
  gCurrState = nextState;
  gPressedEvents = static_cast<uint8_t>(gCurrState & ~gPrevState);
  gReleasedEvents = static_cast<uint8_t>(~gCurrState & gPrevState);

  const unsigned long now = millis();
  if (gPressedEvents != 0 && gPrevState == 0) {
    gPressStartMs = now;
  }
  if (gCurrState != 0) {
    gLastHeldMs = now - gPressStartMs;
  } else if (gReleasedEvents != 0) {
    gLastHeldMs = now - gPressStartMs;
  } else {
    gLastHeldMs = 0;
  }
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  return buttonIndex < kNumButtons && (gCurrState & (1U << buttonIndex)) != 0;
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  return buttonIndex < kNumButtons && (gPressedEvents & (1U << buttonIndex)) != 0;
}

bool HalGPIO::wasAnyPressed() const { return gPressedEvents != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  return buttonIndex < kNumButtons && (gReleasedEvents & (1U << buttonIndex)) != 0;
}

bool HalGPIO::wasAnyReleased() const { return gReleasedEvents != 0; }

unsigned long HalGPIO::getHeldTime() const { return gLastHeldMs; }

unsigned long HalGPIO::getPowerButtonHeldTime() const { return 0; }

bool HalGPIO::rawInputActive() { return gCurrState != 0 || gPendingCrankSteps != 0; }

bool HalGPIO::hasTouch() const { return false; }

bool HalGPIO::hasHomeKey() const { return false; }

bool HalGPIO::hasToggleSwitch() const { return false; }

bool HalGPIO::isToggleSwitchOn() const { return false; }

bool HalGPIO::wasHomeKeyPressed() const { return false; }

bool HalGPIO::wasHomeKeyTapped() const { return false; }

bool HalGPIO::wasHomeKeyLongPressed() const { return false; }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  (void)nx;
  (void)ny;
  return false;
}

bool HalGPIO::wasTouchDown(float& nx, float& ny) const {
  (void)nx;
  (void)ny;
  return false;
}

bool HalGPIO::wasTouchReleased() const { return false; }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  (void)nx;
  (void)ny;
  (void)heldMs;
  return false;
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const {
  (void)nx;
  (void)ny;
  return false;
}

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const {
  (void)nx;
  (void)ny;
  return false;
}

void HalGPIO::suppressTouchContact() {}

unsigned long HalGPIO::lastTouchHeldMs() const { return 0; }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  (void)nxStart;
  (void)nyStart;
  (void)nxEnd;
  (void)nyEnd;
  return false;
}

bool HalGPIO::wasTouchActivity() const { return false; }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool enabled) { (void)enabled; }

bool HalGPIO::verifyPowerButtonWakeup() { return true; }

bool HalGPIO::isUsbConnected() const { return false; }

bool HalGPIO::coldBootImpliesPowerButton() const { return false; }

bool HalGPIO::wasUsbStateChanged() const { return false; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::Other; }
