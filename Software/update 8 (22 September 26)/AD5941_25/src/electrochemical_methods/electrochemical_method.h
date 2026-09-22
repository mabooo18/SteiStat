#ifndef ELECTROCHEMICAL_METHOD_H
#define ELECTROCHEMICAL_METHOD_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"

// Common polymorphic interface for every electrochemical measurement method
// (CA, SWV, DPV, EIS, OCP, CV). Each Dispatch<Method>() call site in
// C_Communication follows the same shape through this interface:
//   C_<Method> c_<Method>; c_<Method>.Begin(m_pData); c_<Method>.Run();
// Method-specific configuration/results (e.g. C_EIS::RunSeeedStat(),
// C_OCP::Calculate()) remain on the concrete classes, beyond this shared
// entry point.
class C_ElectrochemicalMethod {
public:
    virtual ~C_ElectrochemicalMethod() {}
    virtual void Begin(C_DataStorage* pData) = 0;
    virtual void Run() = 0;
};
#endif
