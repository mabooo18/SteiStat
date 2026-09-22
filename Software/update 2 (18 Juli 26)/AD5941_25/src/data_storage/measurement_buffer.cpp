// Buffer manager module for caching measurement coordinate datasets (e.g. real/imaginary impedance pairs) before streaming or Nyquist math.

#include "measurement_buffer.h"

// Define maximum allocation space: 4 fields * 7 elements * 20 frequency steps = 560 floats
#define MEASUREMENT_BUFFER_SIZE 4 * 7 * 20

float Measurements[MEASUREMENT_BUFFER_SIZE]; // Global array holding raw float data coordinates
float* pMeasurement = Measurements;           // Pointer tracking current write position in array
int numberOfMeasurements = 0;                 // Counts currently stored coordinates

/**
 * @brief Resets write pointer back to buffer start and clears count.
 */
void ResetMeasurementBuffer()
{
    pMeasurement = Measurements;
    numberOfMeasurements = 0;
}

/**
 * @brief Appends a coordinate pair (real and imaginary float components) to the buffer.
 * @param real Real component float value.
 * @param imag Imaginary component float value.
 */
void AppendMeasurement(float real, float imag)
{
    // Prevent array index out of bounds errors
    if (numberOfMeasurements < MEASUREMENT_BUFFER_SIZE / 2)
    {
        *pMeasurement++ = real;
        *pMeasurement++ = imag;
        ++numberOfMeasurements;
    }
}
