#pragma once

#include <Arduino.h>

namespace tuya_lora {

// Called during the SD update flow. `secondsLeft` is >= 0 during the countdown
// and -1 for status messages that should stay on screen.
using SdUpdateDisplay = void (*)(const char *title, const char *detail,
                                 int secondsLeft);

// Looks for `filename` on the SD card. If present, shows a 10 s countdown,
// flashes it with Update, renames it to `filename.done`, and reboots.
//
// Returns false when there is no update or the update failed, so the caller can
// continue the normal boot. On success this function does not return.
bool maybeSdFirmwareUpdate(const char *filename, SdUpdateDisplay draw);

}  // namespace tuya_lora
