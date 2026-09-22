#ifndef C_SWV_H
#define C_SWV_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"

class C_SWV {
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