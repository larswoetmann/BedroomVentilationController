#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config_contents.h"

constexpr char kPath[] = "/ventilation.cfg";
constexpr char kTemp[] = "/ventilation-install.tmp";

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("Metro SD configuration installer");
  SPI.begin(39, 21, 42, 45); // SCK, MISO, MOSI, CS
  if (!SD.begin(45, SPI, 4000000)) {
    Serial.println("ERROR: Cannot mount SD card. Insert a FAT32-formatted card and reset.");
    return;
  }
  if (SD.exists(kTemp) && !SD.remove(kTemp)) {
    Serial.println("ERROR: Cannot remove previous temporary file.");
    return;
  }
  File file = SD.open(kTemp, FILE_WRITE);
  if (!file) {
    Serial.println("ERROR: Cannot create temporary file.");
    return;
  }
  const size_t expected = sizeof(kConfig) - 1;
  size_t written = file.write(reinterpret_cast<const uint8_t*>(kConfig), expected);
  file.close();
  if (written != expected) {
    Serial.println("ERROR: Incomplete write. Reset to retry.");
    return;
  }
  file = SD.open(kTemp, FILE_READ);
  bool verified = file && file.size() == expected;
  for (size_t i = 0; verified && i < expected; ++i) {
    if (file.read() != static_cast<uint8_t>(kConfig[i])) verified = false;
  }
  file.close();
  if (!verified) {
    Serial.println("ERROR: Verification failed. Existing configuration left unchanged.");
    return;
  }
  if (SD.exists(kPath) && !SD.remove(kPath)) {
    Serial.println("ERROR: Cannot replace existing configuration. Reset to retry.");
    return;
  }
  if (!SD.rename(kTemp, kPath)) {
    Serial.println("ERROR: Rename failed. Verified contents remain in temporary file. Reset to retry.");
    return;
  }
  Serial.println("SUCCESS: /ventilation.cfg written and verified. Upload the controller sketch now.");
  SD.end();
}

void loop() { delay(1000); }
