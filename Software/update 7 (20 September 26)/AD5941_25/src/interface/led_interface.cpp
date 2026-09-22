// LED interface abstraction module. Simplifies updating status indicator NeoPixel colors.

#include "led_interface.h"
#include "../../utilities.h"   // RED/ORANGE/.../WHITE color-index constants

/**
 * @brief Sets the status LED to one of the predefined color indices.
 * @param color One of the RED/ORANGE/YELLOW/GREEN/CYAN/BLUE/PURPLE/MAGENTA/PINK/WHITE indices from utilities.h.
 *
 * Previously this ignored `color` entirely and always rendered white, silently
 * defeating every caller's status color coding (e.g. Utils_SetStatusLed(CYAN)
 * during a CA sweep, GREEN on completion). Table matches the equivalent
 * mapping in the unused, never-called LED() helper in utilities.cpp.
 */
void Interface_SetLed(uint8_t color)
{
    pixels.clear();
    switch (color) {
        case RED:     pixels.setPixelColor(0, pixels.Color(255,   0,   0)); break;
        case ORANGE:  pixels.setPixelColor(0, pixels.Color(255, 128,   0)); break;
        case YELLOW:  pixels.setPixelColor(0, pixels.Color(255, 255,   0)); break;
        case GREEN:   pixels.setPixelColor(0, pixels.Color(  0, 255,   0)); break;
        case CYAN:    pixels.setPixelColor(0, pixels.Color(  0, 255, 255)); break;
        case BLUE:    pixels.setPixelColor(0, pixels.Color(  0,   0, 255)); break;
        case PURPLE:  pixels.setPixelColor(0, pixels.Color(128,   0, 255)); break;
        case MAGENTA: pixels.setPixelColor(0, pixels.Color(255,   0, 255)); break;
        case PINK:    pixels.setPixelColor(0, pixels.Color(255, 102,  78)); break;
        case WHITE:
        default:      pixels.setPixelColor(0, pixels.Color(255, 255, 255)); break;
    }
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
