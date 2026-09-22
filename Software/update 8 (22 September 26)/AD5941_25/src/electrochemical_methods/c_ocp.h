#ifndef C_OCP_H
#define C_OCP_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"
#include "electrochemical_method.h"

class C_OCP : public C_ElectrochemicalMethod {
public:
    void  Begin(C_DataStorage* pData) override;
    void  Run() override;   // Configure() + Measure(): a normal OCP session.
                             // Calculate() stays separate: the 'T' command
                             // site (communication.cpp) calls it alone,
                             // without a fresh Configure()/Measure(), to
                             // report the last-measured result.
    void  Configure();
    void  Measure();
    float Calculate();
private:
    C_DataStorage* m_pData;
    uint32_t       Do1Measurement();
};
#endif