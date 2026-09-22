// Hardware setup module for resetting and calibrating the AD5941 analog front-end chip.

#include "ad5941_setup.h"
#include "../interface/led_interface.h"
#include "../../utilities.h"

// Access global NeoPixel driver instance
extern Adafruit_NeoPixel pixels;

/**
 * @brief Bootstraps hardware resources (NeoPixels, status LEDs, debugging pins) and initializes/calibrates the AFE.
 */
void C_AD5941_Setup::Begin()
{
    // Initialize status indicator NeoPixel
    pixels.begin();
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);

    // Initialize logic analyzer debug pins
    pinMode(DEBUG_PIN_A, OUTPUT);
    digitalWrite(DEBUG_PIN_A, LOW);
    pinMode(DEBUG_PIN_B, OUTPUT);
    digitalWrite(DEBUG_PIN_B, LOW);

    // Boot AD5941 chip registers
    AD5941_InitAll_Standalone();

    // Visual feedback: Display RED during calibration phase
    Interface_SetLed(RED);
    delay(250);
    AD5940_PGA_Calibration_Standalone();
    delay(250);
    
    // Calibration success: Display GREEN
    Interface_SetLed(GREEN);
    Interface_SetPixelsColor(0, 255, 0);
}

/**
 * @brief Exposes standalone driver reset and initialization API.
 */
void C_AD5941_Setup::InitAll()
{
    AD5941_InitAll_Standalone();
}

/**
 * @brief Exposes standalone PGA calibration API.
 */
void C_AD5941_Setup::PGACalibration()
{
    AD5940_PGA_Calibration_Standalone();
}

/**
 * @brief Retrieves a reference to the encapsulated NeoPixel driver.
 */
Adafruit_NeoPixel* C_AD5941_Setup::GetPixels()
{
    return &m_Pixels;
}

/**
 * @brief Resets and boots the AD5941 SPI and interrupt configurations.
 */
void AD5941_InitAll_Standalone()
{
    AD5940_HWReset();          // Assert reset pin LOW then HIGH
    AD5940_MCUResourceInit(0); // Setup SPI registers and attach interrupt ISR
    AD5940_Initialize();       // Load chip default values
}

/**
 * @brief Performs internal ADC PGA offset and gain calibration.
 */
void AD5940_PGA_Calibration_Standalone()
{
    AD5940Err err;
    ADCPGACal_Type pgacal;
    
    // Load calibration settings
    pgacal.AdcClkFreq = 16e6;                // Clocks run at 16 MHz reference speed
    pgacal.ADCSinc2Osr = ADCSINC2OSR_1333;   // Standard OSR settings
    pgacal.ADCSinc3Osr = ADCSINC3OSR_4;
    pgacal.SysClkFreq = 16e6;
    pgacal.TimeOut10us = 1000;
    pgacal.VRef1p11 = 1.11f;                 // On-board low-drift 1.11V reference
    pgacal.VRef1p82 = 1.82f;                 // On-board low-drift 1.82V reference
    pgacal.PGACalType = PGACALTYPE_OFFSETGAIN;// Calibrate both offset and gain errors
    pgacal.ADCPga = 1;                       // Perform calibration for PGA gain level 1 (1.5x)
    
    // Call AD5940 driver calibration routine
    err = AD5940_ADCPGACal(&pgacal);
    if (err != AD5940ERR_OK) {
        Serial.println("AD5940 PGA calibration failed.");
    }
}
