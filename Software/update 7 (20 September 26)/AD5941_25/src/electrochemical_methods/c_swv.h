#ifndef C_SWV_H
#define C_SWV_H
#include <Arduino.h>
#include "dc_potentiostat_method.h"

class C_SWV : public C_DCPotentiostatMethod {
public:
    void Run();
};
#endif
