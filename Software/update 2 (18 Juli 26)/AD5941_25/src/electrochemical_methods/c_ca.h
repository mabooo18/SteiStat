#ifndef C_CA_H
#define C_CA_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"

class C_CA {
public:
    void Begin(C_DataStorage* pData);
    void Run();
private:
    C_DataStorage* m_pData;
    void     ConfigDCMeasurement(float voltage_mV);
    uint32_t MeasureCurrentRaw();
    float    RawToCurrent(uint32_t rawCode);
};
#endif