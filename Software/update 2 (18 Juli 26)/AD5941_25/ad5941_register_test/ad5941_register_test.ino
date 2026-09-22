#include <Arduino.h>
#include "ad5940.h"
#include "XIAOPort.h"

static const unsigned long SERIAL_BAUD = 115200;
static const uint16_t REG_ADIID = 0x0400;
static const uint16_t REG_CHIPID = 0x0404;
static const uint16_t REG_CLKCON0 = 0x0408;
static const uint16_t REG_CLKEN1 = 0x0410;
static const uint16_t REG_AFECON = 0x2000;

void printHex16(uint32_t value) {
  if (value < 0x1000) Serial.print('0');
  if (value < 0x100) Serial.print('0');
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printRegister(const char* label, uint16_t regAddr) {
  uint32_t value = AD5940_ReadReg(regAddr);
  Serial.print(label);
  Serial.print(" [0x");
  Serial.print(regAddr, HEX);
  Serial.print("] = 0x");
  if (regAddr >= 0x1000 && regAddr <= 0x3014) {
    Serial.println(value, HEX);
  } else {
    printHex16(value & 0xFFFFu);
    Serial.println();
  }
}

void runLibraryDetect() {
  Serial.println();
  Serial.println("AD5941 register test for XIAO RP2040");
  Serial.print("Pins: CS=");
  Serial.print(csPin);
  Serial.print(" RESET=");
  Serial.print(resetPin);
  Serial.print(" INT=");
  Serial.println(intPin);
  Serial.print("Levels: RESET=");
  Serial.print(digitalRead(resetPin));
  Serial.print(" INT=");
  Serial.print(digitalRead(intPin));
  Serial.print(" CS=");
  Serial.println(digitalRead(csPin));

  AD5940_MCUResourceInit(0);
  AD5940_HWReset();
  delay(10);
  AD5940_Initialize();

  printRegister("ADIID  ", REG_ADIID);
  printRegister("CHIPID ", REG_CHIPID);
  printRegister("CLKCON0", REG_CLKCON0);
  printRegister("CLKEN1 ", REG_CLKEN1);
  printRegister("AFECON ", REG_AFECON);

  uint32_t chipId = AD5940_GetChipID();
  Serial.print("AD5940_GetChipID() = 0x");
  printHex16(chipId & 0xFFFFu);
  Serial.println();

  if (chipId == 0x5500 || chipId == 0x5501 || chipId == 0x5502) {
    Serial.println("STATUS: chip terdeteksi oleh library.");
  } else if (chipId == 0x0000 || chipId == 0xFFFF) {
    Serial.println("STATUS: CHIPID tidak terbaca. Cek wiring SPI, reset, power, dan CS.");
  } else {
    Serial.println("STATUS: CHIPID terbaca, tetapi nilainya tidak standar.");
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Perintah:");
  Serial.println("  r : jalankan ulang deteksi library");
  Serial.println("  h : tampilkan bantuan");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) {
    delay(10);
  }

  Serial.println("Booting AD5941 library-backed detect...");
  runLibraryDetect();
  printHelp();
}

void loop() {
  if (!Serial.available()) {
    return;
  }

  char command = static_cast<char>(Serial.read());
  if (command == '\r' || command == '\n') {
    return;
  }

  switch (command) {
    case 'r':
    case 'R':
      runLibraryDetect();
      break;
    case 'h':
    case 'H':
    case '?':
      printHelp();
      break;
    default:
      Serial.print("Command tidak dikenal: ");
      Serial.println(command);
      printHelp();
      break;
  }
}