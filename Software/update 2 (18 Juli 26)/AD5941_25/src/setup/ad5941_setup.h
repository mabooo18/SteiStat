/******************************************************************************
 * @file:    ad5941_setup.h
 * @brief:   Hardware initialization dan kalibrasi AD5941
 *
 * Membungkus: AD5941_InitAll(), AD5940_PGA_Calibration(), setup NeoPixel,
 * setup pin D4/D5, dan Serial.begin().
 *****************************************************************************/

#ifndef AD5941_SETUP_H
#define AD5941_SETUP_H

#include <Arduino.h>
#include "../ad5940/ad5940.h"
#include "../../ad5940.h"
#include "../../AD5940.h"
#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_POWER_PIN  11
#define NEOPIXEL_DATA_PIN   12
#define NEOPIXEL_COUNT       1

#define DEBUG_PIN_A  D4     // pulse = measurement in progress
#define DEBUG_PIN_B  D5     // pulse = ADC out of range

class C_AD5941_Setup {
public:
    /**
     * @brief  Inisialisasi penuh: Serial, GPIO, AD5941, NeoPixel, kalibrasi PGA.
     *         Setelah Begin() selesai, hardware siap untuk perintah pengukuran.
     */
    void Begin();

    /**
     * @brief  Reset dan init ulang AD5941 (dipanggil sebelum setiap scan EIS).
     */
    void InitAll();

    /**
     * @brief  Kalibrasi PGA AD5940 (dipanggil dari C_Communication 'C' command).
     */
    void PGACalibration();

    /**
     * @brief  Akses NeoPixel (dipakai oleh class EC methods untuk LED status).
     */
    Adafruit_NeoPixel* GetPixels();

private:
    Adafruit_NeoPixel m_Pixels;
};

// Fungsi standalone untuk backward-compat dengan cv.cpp dan utilities
void AD5941_InitAll_Standalone();
void AD5940_PGA_Calibration_Standalone();

inline void AD5941_InitAll()
{
    AD5941_InitAll_Standalone();
}

inline void AD5940_PGA_Calibration()
{
    AD5940_PGA_Calibration_Standalone();
}

#endif /* AD5941_SETUP_H */