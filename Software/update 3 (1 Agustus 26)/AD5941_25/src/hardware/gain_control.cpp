// Combined Gain (CG) optimization finder.
// Logarithmically interpolates target gain based on frequency, scanning all TIA Rf and PGA stages to select the closest combination.

#include "gain_control.h"

/**
 * @brief Interpolates target Combined Gain (CG) for the current frequency and finds the closest hardware settings.
 *        The logarithmic combined gain CG = TIA_Rf * PGA_gain.
 * @param CGmax Maximum combined gain limit (typically at low frequency, e.g. 0.1 Hz).
 * @param CGmin Minimum combined gain limit (typically at high frequency, e.g. 100 kHz).
 * @param freq Operating excitation frequency.
 * @param TIA_Rf [out] Index of the optimal TIA feedback resistor found (0 to 7).
 * @param PGA_gain [out] Index of the optimal PGA gain found (0 to 4).
 * @param closest_CG [out] Evaluated combined gain achieved under optimal settings.
 */
void Hardware_FindOptimum_Rf_PGA(uint32_t CGmax, uint32_t CGmin, float freq, uint8_t* TIA_Rf, uint8_t* PGA_gain, float* closest_CG)
{
    // Hardware resistor lookup table
    uint32_t rf_values[] = {200, 1000, 5000, 10000, 20000, 40000, 80000, 160000};
    // Hardware PGA gain lookup table
    float pga_values[] = {1.0, 1.5, 2.0, 4.0, 9.0};

    float logmax, logmin, logf, logCG, m, c, CG, target_CG, best_error, output_CG, error;

    // Convert variables to logarithmic scales
    logf = log10(freq);
    logmax = log10(CGmax);
    logmin = log10(CGmin);

    // Compute logarithmic slope (m) and intercept (c) mapping log(CG) = m * log(frequency) + c
    m = (logmin - logmax) / 6.0;
    c = (5 * logmax + logmin) / 6.0;

    // Calculate optimal target combined gain
    logCG = m * logf + c;
    CG = pow(10, logCG);

    target_CG = CG;
    best_error = 1000000.0;
    *closest_CG = 0.0;

    // Scan all 8 TIA Rf choices and 5 PGA gain steps to find the closest match
    for (uint8_t i = 0; i < 8; i++) {
        for (uint8_t j = 0; j < 5; j++) {
            output_CG = rf_values[i] * pga_values[j]; // CG = Rf * PGA
            error = abs(target_CG - output_CG);
            
            // Retain values yielding the lowest error
            if (error < best_error) {
                best_error = error;
                *TIA_Rf = i;
                *PGA_gain = j;
                *closest_CG = output_CG;
            }
        }
    }
}
