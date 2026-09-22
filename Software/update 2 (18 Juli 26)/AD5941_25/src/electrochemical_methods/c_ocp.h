#ifndef C_OCP_H
#define C_OCP_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"

class C_OCP {
public:
    void  Begin(C_DataStorage* pData);
    void  Configure();
    void  Measure();
    float Calculate();
private:
    C_DataStorage* m_pData;
    uint32_t       Do1Measurement();
};
#endif