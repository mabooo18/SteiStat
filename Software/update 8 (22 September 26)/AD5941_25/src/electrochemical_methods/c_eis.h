#ifndef C_EIS_H
#define C_EIS_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"
#include "electrochemical_method.h"

class C_EIS : public C_ElectrochemicalMethod {
public:
    void Begin(C_DataStorage* pData) override;
    void Run() override;  // eisScan(EIS_Mode)
    void RunSeeedStat();  // SeeedStatScan
    void CalculateNyquistCurve();
private:
    C_DataStorage* m_pData;
    void   CalculateMagAndPhase(float* pRealAndImag, float& mag, float& phase);
    void   CalculateNyquistPoint(float* pRZ, float* pRCAL);
};
#endif