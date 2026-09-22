#ifndef DC_POTENTIOSTAT_METHOD_H
#define DC_POTENTIOSTAT_METHOD_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"
#include "electrochemical_method.h"

// Shared low-power-loop DC measurement primitive for every DC-excitation
// electrochemical method (CA, DPV, SWV). AFE register configuration, raw
// ADC sampling, and code-to-current conversion were identical, verbatim
// copies in all three method classes; they live here once instead, and
// each subclass supplies only its own Run() sequencing.
class C_DCPotentiostatMethod : public C_ElectrochemicalMethod {
public:
    void Begin(C_DataStorage* pData) override;
    // Run() stays pure-virtual here; C_CA/C_SWV/C_DPV each provide their
    // own method-specific sequencing.
protected:
    C_DataStorage* m_pData;
    bool m_lastTimedOut = false;   // set by MeasureCurrentRaw()
    void     ConfigDCMeasurement(float voltage_mV);
    uint32_t MeasureCurrentRaw();
    float    RawToCurrent(uint32_t rawCode);
};
#endif
