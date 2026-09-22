#ifndef C_EIS_H
#define C_EIS_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"

class C_EIS {
public:
    void Begin(C_DataStorage* pData);
    void Run();           // eisScan(EIS_Mode)
    void RunSeeedStat();  // SeeedStatScan
    void CalculateNyquistCurve();
private:
    C_DataStorage* m_pData;
    void   CalculateMagAndPhase(float* pRealAndImag, float& mag, float& phase);
    void   CalculateNyquistPoint(float* pRZ, float* pRCAL);
};
#endif