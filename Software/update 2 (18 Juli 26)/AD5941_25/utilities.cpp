#include <arduino.h>
#include <Adafruit_NeoPixel.h>
#include "utilities.h"

// Expose variables configured in the main orchestrator
extern Adafruit_NeoPixel pixels;
extern uint8_t pixelsRed, pixelsGreen, pixelsBlue;

// Verbose detail flags:
// Bit 0 (0x01) -> prints Info calls to Serial output
// Other bits (0x02, 0x04...) -> enables log buffers
uint32_t verbose = 0x0;

#define HISTORY_LENGTH  150000
char history[HISTORY_LENGTH]; // Global history buffer storing command logs
char* pHistory = history;

#define BLINK_PERIOD    100
long nextBlinkTime = 0;       // Keeps track of when the status LED needs to toggle

const int markerPin = D4;     // Pin D4: Diagnostic pulse output pin
const int PINB = 25;          // Pin 25: Onboard Blue LED driver pin (for RP2040)
const int PING = 16;          // Pin 16: Onboard Green LED driver pin
const int PINR = 17;          // Pin 17: Onboard Red LED driver pin

uint8_t pixelsColor = 0;
uint8_t pixelsRed = 0;
uint8_t pixelsGreen = 0;
uint8_t pixelsBlue = 0;

/**
 * @brief Prints formatted text to serial if verbosity and mask criteria match.
 * @param mask Log mask bit to match against 'verbose'.
 * @param format Printf format string.
 */
void Info(uint32_t mask, const char* format, ...)
{
    if ((verbose & 1) && (verbose & mask))
    {
        char buffer[300];

        va_list args;
        va_start(args, format);
        vsprintf(buffer, format, args);
        va_end(args);

        Serial.println(buffer);
    }
}

/**
 * @brief Converts AD5941 18-bit two's complement numbers (like DFT results) to float values.
 * @param num Raw 32-bit register value containing the 18-bit word.
 * @return Decoded floating-point value.
 */
float ToFloat(uint32_t num)
{
    // The AD5941 DFT real/imaginary values are represented as an 18-bit word:
    // Bit 17 is the sign bit, and the lowest 2 bits (bits 1:0) are the fractional component.
    num &= 0x3ffff;                     // Mask to isolate the lower 18 bits
    int8_t fraction = num & 3;          // Extract the lowest 2 bits
    int16_t integer = num >> 2;         // Shift right by 2 to extract the integer component (bits 17:2)
    
    // Divide the fractional count by 4 to get the decimal portion, and add to integer
    return static_cast<float>(integer) + (static_cast<float>(fraction) / 4.0);
}

/**
 * @brief Appends text strings to the persistent history buffer.
 * @param text Source character array to copy.
 * @return True if successful, false if the history buffer is full.
 */
bool AddTextToHistory(const char* text)
{
    size_t length = strlen(text);
    if (pHistory + length >= history + sizeof(history) - 1)
    {
        return false; // Buffer overflow safety check
    }

    strcpy(pHistory, text);
    pHistory += length;
    *pHistory = '\0';

    return true;
}

/**
 * @brief Appends DFT measurement coordinates to the history logs.
 * @param dftReal Raw real component register.
 * @param dftImag Raw imaginary component register.
 */
void AddMeasurementToHistory(uint32_t dftReal, uint32_t dftImag)
{
    float real = ToFloat(dftReal);
    float imag = ToFloat(dftImag);
    char buf[100];
    sprintf(buf, "\n[%.2f,%.2f]", real, imag);
    AddTextToHistory(buf);
}

/**
 * @brief Records a processed serial command string in the history logs.
 * @param pCommand Source command token.
 */
void AddCommandToHistory(const char* pCommand)
{
    if (AddTextToHistory(pCommand))
    {
        AddTextToHistory(";");
    }
}

/**
 * @brief Records structured diagnostic log messages into the history buffer.
 * @param mask Log mask bit to evaluate.
 * @param lineNum Code line number where log occurred.
 * @param format Format string.
 */
void Log(uint32_t mask, int lineNum, const char* format, ...)
{
    if ((verbose & mask) != 0)
    {
        char buffer[500];
        sprintf(buffer, "\n#%i ", lineNum);

        va_list args;
        va_start(args, format);
        vsprintf(buffer + strlen(buffer), format, args);
        va_end(args);

        AddTextToHistory(buffer);
    }
}

/**
 * @brief Generates a transient digital pulse on a specified pin.
 * @param pin Target GPIO pin to pulse.
 * @param usec Pulse duration in microseconds.
 * @param invert If true, pulses active HIGH instead of active LOW.
 */
void OutputPulse(int pin, int usec, bool invert)
{
    bool state = invert ? digitalRead(pin) : LOW;
    digitalWrite(pin, !state);
    delayMicroseconds(usec);
    digitalWrite(pin, state);
}

/**
 * @brief Updates the onboard NeoPixel color channels.
 * @param red Red component intensity (0-255).
 * @param green Green component intensity (0-255).
 * @param blue Blue component intensity (0-255).
 */
void SetPixelsColor(uint8_t red, uint8_t green, uint8_t blue)
{
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(red, green, blue));
    pixels.show();
}

/**
 * @brief Steps the RGB NeoPixel colors by incremental scales.
 */
void ChangePixelsColor(int16_t& red, int16_t redInc, int16_t& green, int16_t greenInc, int16_t& blue, int16_t blueInc)
{
    red += redInc;
    if (red > pixelsRed)    red = 0;
    else if (red < 0)       red = pixelsRed;

    green += greenInc;
    if (green > pixelsGreen)  green = 0;
    else if (green < 0)       green = pixelsGreen;

    blue += blueInc;
    if (blue > pixelsBlue) blue = 0;
    else if (blue < 0)     blue = pixelsBlue;

    SetPixelsColor(red, green, blue);
}

/**
 * @brief Blocks execution for a specified duration while cycling NeoPixel colors.
 * @param time Sleep duration in milliseconds.
 */
void Delay(uint32_t time, int16_t redInc, int16_t greenInc, int16_t blueInc)
{
    uint32_t until = millis() + time;

    int16_t red = 0;
    int16_t green = 0;
    int16_t blue = 0;

    while (millis() < until)
    {
        ChangePixelsColor(red, redInc, green, greenInc, blue, blueInc);

        uint32_t diff = until - millis();
        delay(diff < 100 ? diff : 100);
    }
}

/**
 * @brief Spins execution until an evaluator function returns false, cycling NeoPixel colors.
 * @param func Boolean predicate function pointer.
 */
void Delay(bool (*func)(), int16_t redInc, int16_t greenInc, int16_t blueInc)
{
    int16_t red = 0;
    int16_t green = 0;
    int16_t blue = 0;

    while ((*func)())
    {
        ChangePixelsColor(red, redInc, green, greenInc, blue, blueInc);
        delay(100);
    }
}

/**
 * @brief Sets the NeoPixel LED to predefined solid colors.
 * @param colorval Predefined color index (RED, ORANGE, GREEN, etc.).
 */
void LED(uint8_t colorval)
{
    pixels.clear();
    switch(colorval)
    {
        case RED:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 0,   pixelsBlue = 0));
            break;
        case ORANGE:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 128, pixelsBlue = 0));
            break;
        case YELLOW:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 255, pixelsBlue = 0));
            break;
        case GREEN:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 0,   pixelsGreen = 255, pixelsBlue = 0));
            break;
        case CYAN:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 0,   pixelsGreen = 255, pixelsBlue = 255));
            break;
        case BLUE:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 0,   pixelsGreen = 0,   pixelsBlue = 255));
            break;
        case PURPLE:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 128, pixelsGreen = 0,   pixelsBlue = 255));
            break;
        case MAGENTA:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 0,   pixelsBlue = 255));
            break;
        case PINK:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 102, pixelsBlue = 78));
            break;
        case WHITE:
            pixels.setPixelColor(0, pixels.Color(pixelsRed = 255, pixelsGreen = 255, pixelsBlue = 255));
            break;
    }

    pixels.show();
    pixelsColor = colorval;
}

/**
 * @brief Toggles the diagnostics pulse marker output pin state.
 */
void markerToggle()
{
    digitalWrite(markerPin, digitalRead(markerPin) ? LOW : HIGH);
    BlinkLed();
}

/**
 * @brief Asserts the marker pin HIGH.
 */
void markerOn()
{
    digitalWrite(markerPin, HIGH);
    BlinkLed();
}

/**
 * @brief Asserts the marker pin LOW.
 */
void markerOff()
{
    digitalWrite(markerPin, LOW);
}

/**
 * @brief Sets the direct GPIO pins for Red, Green, and Blue LED indicators.
 */
void LightLed(bool red, bool green, bool blue)
{
    digitalWrite(PINB, blue);
    digitalWrite(PING, green);
    digitalWrite(PINR, red);
}

/**
 * @brief Non-blocking function that blinks the discrete onboard indicator LEDs periodically.
 */
void BlinkLed()
{
#define LED_ON  LOW
#define LED_OFF HIGH

    static int color = 0;
    if (millis() > nextBlinkTime)
    {
        nextBlinkTime = millis() + BLINK_PERIOD;
        switch(color % 8)
        {
            case 0: LightLed(LED_OFF, LED_OFF, LED_OFF);         break;
            case 1: LightLed(LED_OFF, LED_OFF, LED_ON);          break;
            case 2: LightLed(LED_OFF, LED_ON,  LED_OFF);         break;
            case 3: LightLed(LED_OFF, LED_ON,  LED_ON);          break;
            case 4: LightLed(LED_ON,  LED_OFF, LED_OFF);         break;
            case 5: LightLed(LED_ON,  LED_OFF, LED_ON);          break;
            case 6: LightLed(LED_ON,  LED_ON,  LED_OFF);         break;
            case 7: LightLed(LED_ON,  LED_ON,  LED_ON);          break;
        }
        ++color;
    }
}
