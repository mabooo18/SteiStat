#include "command_processing.h"
#include <stdio.h>
#include <string.h>
#include "../../utilities.h"
#include "../../RampTest.h"
#include "../utils/status_utils.h"
#include "../../ad5940.h"
#include "../../AD5940.h"
#include "../data_storage/measurement_buffer.h"
#include "../electrochemical_methods/electrochemical_methods.h"
#include "../ad5940/debug.h"

extern float V_start;
extern float V_stop;
extern float Estep;
extern float ScanRate;
extern uint16_t CycleNumber;
extern void cvSetup(float start, float stop);
extern uint32_t verbose;
extern ADCFilterCfg_Type adc_filter;
extern ADCBaseCfg_Type adc_base;
extern HSLoopCfg_Type HpLoopCfg;
extern bool SeeedStatMode;
extern uint8_t pga_gain;
extern uint8_t tia_rf;
extern uint8_t EIS_mode;
extern uint16_t nfreqs;
extern uint16_t vbias;
extern uint16_t amplitude;
extern uint16_t offset;
extern uint32_t freqlo;
extern uint32_t freqhi;
extern uint32_t _CGmax;
extern uint32_t _CGmin;
extern uint16_t adc_delay_ms;
extern uint16_t settling_delay_ms;
extern uint32_t settling_parameter;
extern float fRcal;
extern float fAmplitude;
extern float fBias;
extern float fOffset;
extern float constA;
extern float constB;
extern bool useConstAB;
extern bool use_variable_gain;
extern uint16_t OCP_npts;
extern uint8_t vzero;
extern float WEmV;
extern BoolFlag ocpCalibration;
extern bool ocpCalibrationCycling;
extern float WEfrom;
extern float WEto;
extern float WEstep;
extern char history[];
extern char* pHistory;
extern float Measurements[];
extern int numberOfMeasurements;
extern uint32_t ADCCON;
extern uint32_t OCP_sum;
extern uint32_t HSDACDAT;
extern uint32_t SetDACLevel(float mV);
extern void AD5940_PGA_Calibration(void);
extern void AD5941_InitAll();
extern void Config_AD5941_OCP_Measurement(float wemV);
extern void Do_AD5941_OCP_Measurement();
extern float CalculateOCP();
extern void eisScan(uint8_t eisMode);
extern void SeeedStatScan();

uint32_t FreqToLabVIEW(float param)
{
    float freq = param * 1000.0;
    return (uint32_t)freq;
}

uint16_t ConvertFloatBiasToUint16(float value)
{
    return 1664 + (int)(-value * (1850.0 - 1664.0) / 100.0 + 0.5);
}

float ConvertUint16BiasToFloat(uint16_t value)
{
    float result = static_cast<float>(value);
    result = (float)((result - 1664.0) * 100.0 / (1850.0 - 1664.0));
    return result;
}

uint16_t ConvertFloatAmplitudeToUint16(float value)
{
    return (uint16_t)(value * 126.0 / 50.0 + 0.5);
}

float ConvertUint16AmplitudeToFloat(uint16_t value)
{
    float result = static_cast<float>(value);
    return (float)(result * 50.0 / 126.0);
}

uint16_t ConvertFloatOffsetToUint16(float value)
{
    return (uint16_t)((int16_t)value - 56) & 0xFFF;
}

float ConvertUint16OffsetToFloat(uint16_t value)
{
    float result = static_cast<float>(value);
    if (value & (1 << 11))
    {
        int16_t val16 = value | 0xF000;
        result = static_cast<float>(val16);
    }
    result += 56.0;
    return result;
}

uint32_t ParseTokenForHex2(const char* token)
{
    uint32_t hex2 = 0;
    char* pc = strchr(token, ',');
    if (pc != NULL)
    {
        char c;
        for (pc += 3; (c = *pc) != 0; ++pc)
        {
            hex2 <<= 4;
            hex2 += c <= '9' ? c - '0' : c <= 'F' ? c - 'A' + 10 : c - 'a' + 10;
        }
    }
    return hex2;
}

void ShowParameter(const char* name, int value, bool verb)
{
    if (verb)
    {
        Serial.print(name);
        Serial.println(value);
    }
}

void ShowParameter8(const char* name, uint8_t value, bool verb)
{
    if (verb)
    {
        Serial.print(name);
        Serial.println(value == 0 ? "Rz" : "Rcal");
    }
}

void ShowParameterF(const char* format, float value, bool verb)
{
    if (verb)
    {
        char buffer[500];
        sprintf(buffer, format, value);
        Serial.println(buffer);
    }
}

void ShowParameter2(const char* format, int value1, float value2, bool verb)
{
    if (verb)
    {
        char buffer[500];
        sprintf(buffer, format, value1, value2);
        Serial.println(buffer);
    }
}

void ShowAction(const char* name, bool verb)
{
    if (verb) Serial.println(name);
}

void ShowParameters()
{
    Serial.println("----------------------");
    ShowParameter("verbose mode        (@)=", verbose, true);
    ShowParameter("SeeedStat mode      (S)=", SeeedStatMode, true);
    ShowParameter("pga_gain            (g)=", pga_gain, true);
    ShowParameter("tia_rf              (r)=", tia_rf, true);
    ShowParameterF("constA              (i)=%.3f", constA, true);
    ShowParameterF("constB              (j)=%.3f", constB, true);
    ShowParameter("useConstAB             =", useConstAB, true);
    ShowParameter("EIS_mode            (m)=", EIS_mode, true);
    ShowParameter("OCP_npts            (n)=", OCP_npts, true);
    ShowParameter("vzero               (a)=", vzero, true);
    ShowParameter("use_variable_gain   (s)=", use_variable_gain, true);
    ShowParameter("nfreqs              (y)=", nfreqs, true);
    ShowParameter2("vbias               (b)=%i (%.1f mV)", vbias, ConvertUint16BiasToFloat(vbias), true);
    ShowParameter2("amplitude           (z)=%i (0-pk %.1f mV)", amplitude, ConvertUint16AmplitudeToFloat(amplitude), true);
    ShowParameter2("offset              (v)=%i (%.1f mV)", offset, ConvertUint16OffsetToFloat(offset), true);
    ShowParameter("freqlo              (w)=", freqlo, true);
    ShowParameter("freqhi              (x)=", freqhi, true);
    ShowParameter("_CGmax              (t)=", _CGmax, true);
    ShowParameter("_CGmin              (u)=", _CGmin, true);
    ShowParameter("adc_delay_ms        (e)=", adc_delay_ms, true);
    ShowParameter("settling_parameter  (d)=", settling_parameter, true);
    ShowParameter("rcal                (c)=", fRcal, true);
    ShowParameter("numberOfMeasurements =", numberOfMeasurements, true);
    Serial.println("Show history:       !");
    Serial.println("Show parameters:    ?");
    Serial.println("Do eisScan:         E");
    Serial.println("Do SeeedStatScan:   P");
    Serial.println("Do stress test:     f");
    Serial.println("----------------------");
    ShowParameterF("Bias      (B)=%.1f mV", ConvertUint16BiasToFloat(vbias), true);
    ShowParameterF("Amplitude (Y)=%.1f mV", ConvertUint16AmplitudeToFloat(amplitude), true);
    ShowParameterF("Offset    (V)=%.1f mV", ConvertUint16OffsetToFloat(offset), true);
    Serial.println("-------------------");
}

bool ProcessCommand2Int(const char command, uint32_t param1, uint32_t param2)
{
    uint16_t RegAddr;
    uint32_t RegData;
    switch (command)
    {
        case 'O':
            RegAddr = (uint16_t)param1;
            RegData = (uint32_t)param2;
            AD5940_WriteReg(RegAddr, RegData);
            char buf[100];
            sprintf(buf, "O 0x%X=0x%X", RegAddr, RegData);
            Serial.println(buf);
            return true;
    }
    return false;
}

bool ProcessCommand2Float(const char command, float param1, float param2)
{
    switch (command)
    {
        case 'D':
            SeeedStatMode = true;
            freqlo = FreqToLabVIEW(param1);
            freqhi = FreqToLabVIEW(param2);
            V_start = param1;
            V_stop = param2;
            Info(1, "V_start=%.1f V_stop=%.1f freqlo=%i freqhi=%i", V_start, V_stop, freqlo, freqhi);
            return true;
        case 'O':
            uint16_t RegAddr = (uint16_t)param1;
            uint32_t RegData = (uint32_t)param2;
            AD5940_WriteReg(RegAddr, RegData);
            char buf[100];
            sprintf(buf, "O 0x%X=0x%X", RegAddr, RegData);
            Serial.println(buf);
            return true;
    }
    return false;
}

bool ProcessCommand1Int(const char command, uint16_t param)
{
    uint16_t RegAddr;
    uint32_t RegData;
    int n = (int)param;
    switch (command)
    {
        case '@':
            verbose = n;
            ShowParameter("verbose mode(@)=", verbose, verbose & 1);
            return true;
        case 'I':
            RegAddr = (uint16_t)param;
            RegData = AD5940_ReadReg(RegAddr);
            char buf[100];
            sprintf(buf, "I 0x%X=0x%X", RegAddr, RegData);
            Serial.println(buf);
            return true;
        case 'M':
            WEmV = param;
            ocpCalibration = bTRUE;
            ShowParameterF("WEmV=%.0f", WEmV, verbose & 1);
            SetDACLevel(WEmV);
            return true;
    }
    return false;
}

bool ProcessCommand1Float(const char command, float param)
{
    int n = (int)param;
    switch (command)
    {
        case '@':
            verbose = n;
            ShowParameter("verbose mode(@)=", verbose, verbose & 1);
            return true;
        case 'a':
            vzero = n;
            ShowParameter("vzero(a)=", vzero, verbose & 1);
            return true;
        case 'B':
            fBias = param;
            n = ConvertFloatBiasToUint16(fBias);
        case 'b':
            vbias = n;
            ShowParameter2("vbias(b)=%i (%.2f mV)", vbias, ConvertUint16BiasToFloat(vbias), verbose & 1);
            return true;
        case 'c':
            fRcal = param;
            ShowParameter("RCal(d)=", fRcal, verbose & 1);
            return true;
        case 'g':
            pga_gain = n;
            adc_base.ADCPga = pga_gain;
            ShowParameter("pga_gain(g)=", pga_gain, verbose & 1);
            return true;
        case 'i':
            constA = param;
            ShowParameterF("constA(i)=%.3f", constA, verbose & 1);
            useConstAB = true;
            return true;
        case 'j':
            constB = param;
            ShowParameterF("constB(j)=%.3f", constB, verbose & 1);
            useConstAB = true;
            return true;
        case 'M':
            WEmV = param;
            ocpCalibration = bTRUE;
            ShowParameterF("WEmV=%.0f", WEmV, verbose & 1);
            SetDACLevel(WEmV);
            return true;
        case 'm':
            EIS_mode = n;
            ShowParameter("EIS_mode(m)=", EIS_mode, verbose & 1);
            return true;
        case 'n':
            OCP_npts = n;
            ShowParameter("OCP_npts(n)=", OCP_npts, verbose & 1);
            return true;
        case 'r':
            tia_rf = n;
            ShowParameter("tia_rf(r)=", tia_rf, verbose & 1);
            return true;
        case 's':
            use_variable_gain = n;
            ShowParameter("use_variable_gain(s)=", use_variable_gain, verbose & 1);
            return true;
        case 'S':
            SeeedStatMode = n ? true : false;
            ShowParameter("SeeedStat mode(S)=", SeeedStatMode, verbose & 1);
            return true;
        case 't':
            _CGmax = n;
            ShowParameter("_CGmax(t)=", _CGmax, verbose & 1);
            return true;
        case 'u':
            _CGmin = n;
            ShowParameter("_CGmin(u)=", _CGmin, verbose & 1);
            return true;
        case 'V':
            fOffset = param;
            n = ConvertFloatOffsetToUint16((uint16_t)fOffset);
        case 'v':
            offset = (uint16_t)n;
            ShowParameter2("offset(v)=%i (%.2f)", offset, ConvertUint16OffsetToFloat(offset), verbose & 1);
            return true;
        case 'W':
            n = FreqToLabVIEW(param);
        case 'w':
            freqlo = n;
            ShowParameter("freqlo(w)=", freqlo, verbose & 1);
            return true;
        case 'X':
            n = FreqToLabVIEW(param);
        case 'x':
            freqhi = n;
            ShowParameter("freqhi(x)=", freqhi, verbose & 1);
            return true;
        case 'y':
            nfreqs = n;
            ShowParameter("nfreqs(y)=", nfreqs, verbose & 1);
            return true;
        case 'Y':
            fAmplitude = param;
            n = ConvertFloatAmplitudeToUint16(fAmplitude);
        case 'z':
            amplitude = n;
            ShowParameter2("amplitude(z)=%i (0-pk %.2f mV)", amplitude, ConvertUint16AmplitudeToFloat(amplitude), verbose & 1);
            return true;
        case '1':
            CA_Voltage_mV = param;
            ShowParameterF("CA_Voltage_mV(1)=%.1f", CA_Voltage_mV, verbose & 1);
            return true;
        case '2':
            CA_Duration_s = param;
            ShowParameterF("CA_Duration_s(2)=%.2f", CA_Duration_s, verbose & 1);
            return true;
        case '3':
            CA_SampleRate_Hz = param;
            ShowParameterF("CA_SampleRate_Hz(3)=%.1f", CA_SampleRate_Hz, verbose & 1);
            return true;
        case '4':
            SWV_Start_mV = param;
            ShowParameterF("SWV_Start_mV(4)=%.1f", SWV_Start_mV, verbose & 1);
            return true;
        case '5':
            SWV_End_mV = param;
            ShowParameterF("SWV_End_mV(5)=%.1f", SWV_End_mV, verbose & 1);
            return true;
        case '6':
            SWV_Step_mV = param;
            ShowParameterF("SWV_Step_mV(6)=%.2f", SWV_Step_mV, verbose & 1);
            return true;
        case '7':
            SWV_Amplitude_mV = param;
            ShowParameterF("SWV_Amplitude_mV(7)=%.1f", SWV_Amplitude_mV, verbose & 1);
            return true;
        case '8':
            SWV_Frequency_Hz = param;
            ShowParameterF("SWV_Frequency_Hz(8)=%.1f", SWV_Frequency_Hz, verbose & 1);
            return true;
        case '9':
            DPV_Start_mV = param;
            ShowParameterF("DPV_Start_mV(9)=%.1f", DPV_Start_mV, verbose & 1);
            return true;
        case '0':
            DPV_End_mV = param;
            ShowParameterF("DPV_End_mV(0)=%.1f", DPV_End_mV, verbose & 1);
            return true;
        case '!':
            DPV_Step_mV = param;
            ShowParameterF("DPV_Step_mV(!)=%.2f", DPV_Step_mV, verbose & 1);
            return true;
        case '#':
            DPV_Amplitude_mV = param;
            ShowParameterF("DPV_Amplitude_mV(#)=%.1f", DPV_Amplitude_mV, verbose & 1);
            return true;
    }
    return false;
}

bool ProcessCommand(const char command)
{
    char buffer[30];
    float ocp_1, ocp_mV;
    switch (command)
    {
        case '?':
            ShowParameters();
            return true;
        case '!':
            Serial.println(history);
            pHistory = history;
            return true;
        case 'C':
            ShowAction("AD5940_PGA_Calibration(C)", verbose & 1);
            AD5940_PGA_Calibration();
            return true;
        case 'E':
            ShowAction("eisScan(E)", verbose & 1);
            eisScan(EIS_mode);
            return true;
        case 'f':
            ShowAction("eisScan(E)", verbose & 1);
            while (!Serial.available())
            {
                pMeasurement = Measurements;
                eisScan(EIS_mode = 0);
            }
            while (Serial.available())
            {
                Serial.read();
            }
            return true;
        case 'I':
            AD5941_InitAll();
            Config_AD5941_OCP_Measurement(WEmV);
            ocpCalibration = bFALSE;
            useConstAB = false;
            return true;
        case 'M':
            Utils_SetStatusLed(RED);
            cvSetup(V_start, V_stop);
            Utils_SetStatusLed(GREEN);
            Utils_SetStatusPixels(0, 255, 0);
            return true;
        case 'O':
            Utils_SetStatusLed(MAGENTA);
            Do_AD5941_OCP_Measurement();
            Utils_SetStatusLed(GREEN);
            Utils_SetStatusPixels(0, 255, 0);
            return true;
        case 'P':
            ShowAction("SeeedStatScan(P)", verbose & 1);
            SeeedStatScan();
            SeeedStatMode = false;
            return true;
        case 'T':
            delay(10);
            ocp_mV = CalculateOCP();
            sprintf(buffer, "%.6f ", ocp_mV);
            Serial.println(buffer);
            return true;
        case 'U':
            delay(10);
            Serial.write((uint8_t *)&OCP_sum, 4);
            if (verbose & 0x80) CalculateOCP();
            return true;
        case 'Z':
            AD5941_InitAll();
            return true;
        case 'A':
            ShowAction("RunCA(A)", verbose & 1);
            RunCA();
            return true;
        case 'W':
            ShowAction("RunSWV(W)", verbose & 1);
            RunSWV();
            return true;
        case 'D':
            ShowAction("RunDPV(D)", verbose & 1);
            RunDPV();
            return true;
    }
    return false;
}

void ProcessToken(char* token)
{
    Log(1, __LINE__, "ProcessToken:%s", token);

    char command;
    float float1;
    float float2;
    uint32_t hex1;
    uint32_t hex2;
    uint32_t int1;
    uint32_t int2;
    uint32_t int3;
    bool success = false;

    if ((sscanf(token, "M%i,%i,%i", &int1, &int2, &int3) == 3))
    {
        WEfrom = static_cast<float>(int1);
        WEto = static_cast<float>(int2);
        WEstep = static_cast<float>(int3);
        ocpCalibration = bTRUE;
        ocpCalibrationCycling = true;
        analogReadResolution(12);
        pinMode(A0, INPUT);
        pinMode(A3, INPUT);
        success = true;
    }
    else if ((sscanf(token, "D %f,%f,%f,%f,%i", &V_start, &V_stop, &Estep, &ScanRate, &CycleNumber) == 5))
    {
        success = true;
    }
    else if (sscanf(token, "ri80x%lX", &hex1) == 1)
    {
        int8_t i = *(int8_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, i);
        Log(1, __LINE__, "0x%lX=0x%X", hex1, i);
        success = true;
    }
    else if (sscanf(token, "ri160x%lX", &hex1) == 1)
    {
        int16_t i = *(int16_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, i);
        Log(1, __LINE__, "0x%lX=0x%X", hex1, i);
        success = true;
    }
    else if (sscanf(token, "ri320x%lX", &hex1) == 1)
    {
        int32_t i = *(int32_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, i);
        Log(1, __LINE__, "0x%lX=0x%lX", hex1, i);
        success = true;
    }
    else if (sscanf(token, "ru80x%lX", &hex1) == 1)
    {
        uint8_t u = *(uint8_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, u);
        Log(1, __LINE__, "0x%lX=0x%X", hex1, u);
        success = true;
    }
    else if (sscanf(token, "ru160x%lX", &hex1) == 1)
    {
        uint16_t u = *(uint16_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, u);
        Log(1, __LINE__, "0x%lX=0x%X", hex1, u);
        success = true;
    }
    else if (sscanf(token, "ru320x%lX", &hex1) == 1)
    {
        uint32_t u = *(uint32_t*)hex1;
        Info(1, "0x%lX=0x%lX", hex1, u);
        Log(1, __LINE__, "0x%lX=0x%lX", hex1, u);
        success = true;
    }
    else if (sscanf(token, "wi80x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(int8_t*)hex1 = (int8_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "wi160x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(int16_t*)hex1 = (int16_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "wi320x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(int32_t*)hex1 = (int32_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "wu80x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(uint8_t*)hex1 = (uint8_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "wu160x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(uint16_t*)hex1 = (uint16_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "wu320x%lX,0x%lX", &hex1, &hex2) == 2)
    {
        *(uint32_t*)hex1 = (uint32_t)hex2;
        Info(1, "0x%lX <- 0x%lX", hex1, hex2);
        Log(1, __LINE__, "0x%lX <- 0x%lX", hex1, hex2);
        success = true;
    }
    else if (sscanf(token, "%c 0x%lX,0x%lX", &command, &hex1, &hex2) == 3)
    {
        Log(1, __LINE__, "ParseTokenForHex2:%c 0x%lX,0x%lX", command, hex1, hex2);
        hex2 = ParseTokenForHex2(token);
        success = ProcessCommand2Int(command, hex1, hex2);
    }
    else if (sscanf(token, "%c0x%lX,0x%lX", &command, &hex1, &hex2) == 3)
    {
        Log(1, __LINE__, "ParseTokenForHex2:%c0x%lX,0x%lX", command, hex1, hex2);
        hex2 = ParseTokenForHex2(token);
        success = ProcessCommand2Int(command, hex1, hex2);
    }
    else if (sscanf(token, "%c %f,%f", &command, &float1, &float2) == 3)
    {
        Log(1, __LINE__, "ProcessCommand2Float:%c %f,%f", command, float1, float2);
        success = ProcessCommand2Float(command, float1, float2);
    }
    else if (sscanf(token, "%c%f,%f", &command, &float1, &float2) == 3)
    {
        Log(1, __LINE__, "ProcessCommand2Float:%c%f,%f", command, float1, float2);
        success = ProcessCommand2Float(command, float1, float2);
    }
    else if (sscanf(token, "%c 0x%lX", &command, &hex1) == 2)
    {
        Log(1, __LINE__, "ProcessCommand1Int:%c 0x%lX", command, hex1);
        success = ProcessCommand1Int(command, hex1);
    }
    else if (sscanf(token, "%c0x%lX", &command, &hex1) == 2)
    {
        Log(1, __LINE__, "ProcessCommand1Int:%c0x%lX", command, hex1);
        success = ProcessCommand1Int(command, hex1);
    }
    else if (sscanf(token, "%c %f", &command, &float1) == 2)
    {
        Log(1, __LINE__, "ProcessCommand1Float:%c %f", command, float1);
        success = ProcessCommand1Float(command, float1);
    }
    else if (sscanf(token, "%c%f", &command, &float1) == 2)
    {
        Log(1, __LINE__, "ProcessCommand1Float:%c%f", command, float1);
        success = ProcessCommand1Float(command, float1);
    }
    else
    {
        Log(1, __LINE__, "ProcessCommand:%c", command);
        success = ProcessCommand(command);
    }

    if (!success)
    {
        Info(1, "Unrecognized command: %c", command);
        Log(1, __LINE__, "Unrecognized command: %c", command);
    }
}

void SplitAndProcessCommands(char* buf)
{
    char commandDelimiters[] = ";|\r\n";
    for (char* token = strtok(buf, commandDelimiters); token; token = strtok(NULL, commandDelimiters))
    {
        if (strlen(token) > 0)
        {
            AddCommandToHistory(token);
            ProcessToken(token);
        }
    }
}
