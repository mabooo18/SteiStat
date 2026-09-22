// Cyclic Voltammetry (CV) class wrapper. See c_cv.h.

#include "c_cv.h"

// Same globals cv.cpp defines and communication.cpp already declared
// extern for DispatchCV(); declared here too so this file can drive them
// without depending on communication.cpp's declaration order.
extern float    V_start, V_stop, Estep, ScanRate;
extern uint16_t CycleNumber;
extern void     cvSetup(float start, float stop);

void C_CV::Begin(C_DataStorage* pData)
{
    m_pData = pData;
}

/**
 * @brief Synchronizes CV parameters from C_DataStorage into cv.cpp's own
 * globals, then starts the sweep via its existing cvSetup() entry point.
 * Identical to what DispatchCV() did inline before this class existed.
 */
void C_CV::Run()
{
    V_start     = m_pData->GetV_Start();
    V_stop      = m_pData->GetV_Stop();
    Estep       = m_pData->GetEStep();
    ScanRate    = m_pData->GetScanRate();
    CycleNumber = m_pData->GetCycleNumber();
    cvSetup(V_start, V_stop);
}
