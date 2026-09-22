#ifndef C_CA_H
#define C_CA_H
#include <Arduino.h>
#include "dc_potentiostat_method.h"

class C_CA : public C_DCPotentiostatMethod {
public:
    void Run() override;
};
#endif
