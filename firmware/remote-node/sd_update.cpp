#include "sd_update.h"

#include <SD.h>
#include <SPI.h>
#include <Update.h>

#include "config.h"
#include "config_store.h"

namespace tuya_lora {
namespace {

void show(SdUpdateDisplay draw, const char *title, const char *detail,
          int secondsLeft) {
  if (draw) draw(title, detail, secondsLeft);
  Serial.printf("[sd] %s: %s", title, detail);
  if (secondsLeft >= 0) Serial.printf(" (%d s)", secondsLeft);
  Serial.println();
}

void closeSd(SPIClass &spi) {
  SD.end();
  spi.end();
}

void fail(SdUpdateDisplay draw, SPIClass &spi, const char *detail) {
  show(draw, "UPDATE FAIL", detail, -1);
  closeSd(spi);
  delay(3000);
}

}  // namespace

bool maybeSdFirmwareUpdate(const char *filename, SdUpdateDisplay draw) {
  if (!filename || !*filename) return false;

  static SPIClass sdSpi(HSPI);
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSpi, 4000000)) {
    Serial.println("[sd] no card");
    sdSpi.end();
    return false;
  }

  if (!SD.exists(filename)) {
    Serial.printf("[sd] %s not found\n", filename);
    closeSd(sdSpi);
    return false;
  }

  const int waitSeconds = static_cast<int>(settings.sdUpdateWaitMs / 1000UL);
  for (int seconds = waitSeconds; seconds > 0; --seconds) {
    show(draw, "SD UPDATE", filename, seconds);
    delay(1000);
  }

  File file = SD.open(filename, FILE_READ);
  if (!file) {
    fail(draw, sdSpi, "cannot open file");
    return false;
  }

  const size_t size = file.size();
  if (size == 0) {
    file.close();
    fail(draw, sdSpi, "empty file");
    return false;
  }

  show(draw, "UPDATING", "Do not power off", -1);

  if (!Update.begin(size)) {
    Update.printError(Serial);
    file.close();
    fail(draw, sdSpi, "begin failed");
    return false;
  }

  const size_t written = Update.writeStream(file);
  file.close();

  if (written != size) {
    Update.printError(Serial);
    Update.abort();
    fail(draw, sdSpi, "write failed");
    return false;
  }

  if (!Update.end()) {
    Update.printError(Serial);
    Update.abort();
    fail(draw, sdSpi, "verify failed");
    return false;
  }

  if (!Update.isFinished()) {
    Update.printError(Serial);
    fail(draw, sdSpi, "not finished");
    return false;
  }

  // Mark this binary as applied so the next boot does not flash it again.
  String done = String(filename) + ".done";
  if (SD.exists(done.c_str())) SD.remove(done.c_str());
  if (!SD.rename(filename, done.c_str())) {
    SD.remove(filename);
  }
  closeSd(sdSpi);

  show(draw, "UPDATE OK", "rebooting...", -1);
  delay(2000);
  ESP.restart();
  return true;
}

}  // namespace tuya_lora
