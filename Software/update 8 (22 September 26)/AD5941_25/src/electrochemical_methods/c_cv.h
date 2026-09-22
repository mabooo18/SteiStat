#ifndef C_CV_H
#define C_CV_H
#include <Arduino.h>
#include "../data_storage/data_storage.h"
#include "electrochemical_method.h"

// Thin class wrapper around the legacy, AD5940_Ramp-derived CV sweep engine
// (cv.cpp/rampTest.cpp). Run() does exactly what C_Communication::DispatchCV()
// used to do inline: sync the CV parameters from C_DataStorage into cv.cpp's
// own extern globals, then call its existing cvSetup() entry point. No
// change to cv.cpp/rampTest.cpp's internals -- this only gives CV the same
// Begin()/Run() shape every other method class already has, so it can be
// dispatched the same uniform way.
class C_CV : public C_ElectrochemicalMethod {
public:
    void Begin(C_DataStorage* pData) override;
    void Run() override;
private:
    C_DataStorage* m_pData;
};
#endif
