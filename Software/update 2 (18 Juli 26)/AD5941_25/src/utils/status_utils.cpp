// Utility status indicator routines. Translates generic system status calls into LED actions.

#include "status_utils.h"
#include "../interface/led_interface.h"

/**
 * @brief Sets the status LED to a target predefined color.
 * @param color Predefined color index.
 */
void Utils_SetStatusLed(uint8_t color)
{
    Interface_SetLed(color); // Call low-level LED interface
}

/**
 * @brief Sets the status LED to specific Red, Green, and Blue intensity channels.
 * @param red Red component intensity (0-255).
 * @param green Green component intensity (0-255).
 * @param blue Blue component intensity (0-255).
 */
void Utils_SetStatusPixels(uint8_t red, uint8_t green, uint8_t blue)
{
    Interface_SetPixelsColor(red, green, blue); // Call low-level RGB interface
}
