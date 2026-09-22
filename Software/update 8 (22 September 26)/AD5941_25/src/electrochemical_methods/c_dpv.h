#ifndef C_DPV_H
#define C_DPV_H
#include <Arduino.h>
#include "dc_potentiostat_method.h"

class C_DPV : public C_DCPotentiostatMethod {
public:
    void Run() override;
};
#endif
