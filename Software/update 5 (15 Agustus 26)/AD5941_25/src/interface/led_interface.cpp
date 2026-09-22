// LED interface abstraction module. Simplifies updating status indicator NeoPixel colors.

#include "led_interface.h"

/**
 * @brief Sets the status LED to a static default color (white).
 * @param color Unused color index.
 */
void Interface_SetLed(uint8_t color)
{
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(255, 255, 255)); // Set color channels to full intensity white
    pixels.show();
}

/**
 * @brief Sets the status LED to custom Red, Green, and Blue intensities.
 * @param red Red component intensity (0-255).
 * @param green Green component intensity (0-255).
 * @param blue Blue component intensity (0-255).
 */
void Interface_SetPixelsColor(uint8_t red, uint8_t green, uint8_t blue)
{
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(red, green, blue)); // Set specific RGB levels
    pixels.show();
}
