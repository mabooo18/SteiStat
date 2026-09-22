/******************************************************************************
 * @file:    communication.cpp
 * @brief:   Implementasi C_Communication - Parser and Dispatcher of Serial Commands
 *****************************************************************************/

#include "communication.h"
#include "../electrochemical_methods/c_eis.h"
#include "../electrochemical_methods/c_ocp.h"
#include "../electrochemical_methods/c_ca.h"
#include "../electrochemical_methods/c_swv.h"
#include "../electrochemical_methods/c_dpv.h"
#include "../electrochemical_methods/c_cv.h"

// CV's own globals are now synchronized and driven from C_CV::Run()
// (c_cv.cpp) instead of here directly.

// Log output dependencies defined in utilities.cpp
extern uint32_t verbose;
extern void     Info(uint32_t level, const char* fmt, ...);
extern void     Log(uint32_t level, int line, const char* fmt, ...);
extern void     AddMeasurementToHistory(uint32_t real, uint32_t imag);
extern void     OutputPulse(int pin, int ms);

// =============================================================================
// Lifecycle and Startup Configs
// =============================================================================

/**
 * @brief Initializes Serial port baudrate, state references, and clears buffers.
 * @param baudrate Serial baudrate to open (e.g. 1000000).
 * @param pData Pointer to global shared C_DataStorage instance.
 */
void C_Communication::Begin(uint32_t baudrate, C_DataStorage* pData) {
    m_pData    = pData;
    m_pHistory = m_History;
    memset(m_History, 0, sizeof(m_History));

    Serial.begin(baudrate);

    // Sync verbosity state to data storage module
    m_pData->SetVerbose(verbose);
}


// =============================================================================
// Polling Loop and Buffer Reader
// =============================================================================

/**
 * @brief Polling function that checks for serial data and extracts command frames.
 */
void C_Communication::ReadAndProcess() {
    if (!Serial.available()) return;

    char buf[SERIAL_BUFFER_SIZE];
    int n = Serial.readBytes(buf, sizeof(buf) - 1);
    buf[n] = 0; // Null-terminate string
    
    // Trim newline character at string end
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = 0;

    Info(1, "%s", buf);
    SplitAndProcessCommands(buf); // Dispatch tokens
}


// =============================================================================
// Delimiter Tokenizer and command Dispatcher
// =============================================================================

/**
 * @brief Tokenizes input string buffer using predefined separators (';', '|', '\r', '\n') and dispatches.
 * @param buf Null-terminated command string buffer.
 */
void C_Communication::SplitAndProcessCommands(char* buf) {
    char delimiters[] = ";|\r\n";
    for (char* token = strtok(buf, delimiters);
         token != nullptr;
         token = strtok(nullptr, delimiters)) {
        if (strlen(token) > 0) {
            AddCommandToHistory(token); // Add to history log
            ProcessToken(token);       // Parse and execute
        }
    }
}

/**
 * @brief Parses individual command tokens using sscanf patterns and executes matched cases.
 * @param token Single parsed command string.
 */
void C_Communication::ProcessToken(char* token) {
    Log(1, __LINE__, "ProcessToken:%s", token);

    char    command;
    float   float1, float2;
    float   cv_vstart, cv_vstop, cv_estep, cv_scanrate;
    uint32_t hex1, hex2;
    uint32_t int1, int2, int3;
    bool    success = false;

    // --- Dynamic OCP Calibration Format: M<from>,<to>,<step> ---
    if (sscanf(token, "M%i,%i,%i", &int1, &int2, &int3) == 3) {
        m_pData->SetWEFrom_mV((float)int1);
        m_pData->SetWETo_mV((float)int2);
        m_pData->SetWEStep_mV((float)int3);
        m_pData->SetOCP_Calibration(true);
        m_pData->SetOCP_CalibrationCycling(true);

        // Prepare RP2040 micro ADC
        analogReadResolution(12);
        pinMode(A0, INPUT);
        pinMode(A3, INPUT);
        success = true;
    }
    // --- CV Configuration Format: D <Vstart>,<Vstop>,<Estep>,<ScanRate>,<CycleNumber> ---
    else if (sscanf(token, "D %f,%f,%f,%f,%i",
                    &cv_vstart, &cv_vstop, &cv_estep, &cv_scanrate,
                    &int1) == 5) {
        m_pData->SetV_Start(cv_vstart);
        m_pData->SetV_Stop(cv_vstop);
        m_pData->SetEStep(cv_estep);
        m_pData->SetScanRate(cv_scanrate);
        m_pData->SetCycleNumber((uint16_t)int1);
        success = true;
    }
    // --- Memory Read (Hex Addresses): ri8/ri16/ri32/ru8/ru16/ru32 0x<addr> ---
    else if (sscanf(token, "ri80x%lX",  &hex1) == 1) { Info(1,"0x%lX=0x%X",hex1,*(int8_t*)hex1);   success=true; }
    else if (sscanf(token, "ri160x%lX", &hex1) == 1) { Info(1,"0x%lX=0x%X",hex1,*(int16_t*)hex1);  success=true; }
    else if (sscanf(token, "ri320x%lX", &hex1) == 1) { Info(1,"0x%lX=0x%lX",hex1,*(int32_t*)hex1); success=true; }
    else if (sscanf(token, "ru80x%lX",  &hex1) == 1) { Info(1,"0x%lX=0x%X",hex1,*(uint8_t*)hex1);  success=true; }
    else if (sscanf(token, "ru160x%lX", &hex1) == 1) { Info(1,"0x%lX=0x%X",hex1,*(uint16_t*)hex1); success=true; }
    else if (sscanf(token, "ru320x%lX", &hex1) == 1) { Info(1,"0x%lX=0x%lX",hex1,*(uint32_t*)hex1);success=true; }
    
    // --- Memory Write (Hex Addresses): wi8/wi16/wi32/wu8/wu16/wu32 0x<addr>,0x<val> ---
    else if (sscanf(token, "wi80x%lX,0x%lX",  &hex1, &hex2)==2) { *(int8_t*)hex1   =(int8_t)hex2;   success=true; }
    else if (sscanf(token, "wi160x%lX,0x%lX", &hex1, &hex2)==2) { *(int16_t*)hex1  =(int16_t)hex2;  success=true; }
    else if (sscanf(token, "wi320x%lX,0x%lX", &hex1, &hex2)==2) { *(int32_t*)hex1  =(int32_t)hex2;  success=true; }
    else if (sscanf(token, "wu80x%lX,0x%lX",  &hex1, &hex2)==2) { *(uint8_t*)hex1  =(uint8_t)hex2;  success=true; }
    else if (sscanf(token, "wu160x%lX,0x%lX", &hex1, &hex2)==2) { *(uint16_t*)hex1 =(uint16_t)hex2; success=true; }
    else if (sscanf(token, "wu320x%lX,0x%lX", &hex1, &hex2)==2) { *(uint32_t*)hex1 =(uint32_t)hex2; success=true; }
    
    // --- Command + 2 Hex Parameters ---
    else if (sscanf(token, "%c 0x%lX,0x%lX", &command, &hex1, &hex2)==3
          || sscanf(token, "%c0x%lX,0x%lX",  &command, &hex1, &hex2)==3) {
        hex2    = ParseTokenForHex2(token);
        success = ProcessCommand2Int(command, hex1, hex2);
    }
    // --- Command + 2 Float Parameters ---
    else if (sscanf(token, "%c %f,%f", &command, &float1, &float2)==3
          || sscanf(token, "%c%f,%f",  &command, &float1, &float2)==3) {
        success = ProcessCommand2Float(command, float1, float2);
    }
    // --- Command + 1 Hex Parameter ---
    else if (sscanf(token, "%c 0x%lX", &command, &hex1)==2
          || sscanf(token, "%c0x%lX",  &command, &hex1)==2) {
        success = ProcessCommand1Int(command, (uint16_t)hex1);
    }
    // --- Command + 1 Float Parameter ---
    else if (sscanf(token, "%c %f", &command, &float1)==2
          || sscanf(token, "%c%f",  &command, &float1)==2) {
        success = ProcessCommand1Float(command, float1);
    }
    // --- Single Letter Command (No Parameter) ---
    else if (strlen(token) >= 1) {
        command = token[0];
        success = ProcessCommand(command);
    }

    if (!success) {
        Info(1, "Unrecognized command: %s", token);
    }
}


// =============================================================================
// ProcessCommand — Single Letter Trigger Execution Routines
// =============================================================================

/**
 * @brief Processes single-character commands that trigger testing routines or parameter listings.
 * @param cmd The parsed command character.
 * @return True if processed successfully, false if command unrecognized.
 */
bool C_Communication::ProcessCommand(char cmd) {
    switch (cmd) {
        case '?':
            ShowParameters();
            return true;

        case '!':
            PrintHistory();
            return true;

        case 'C':
            // Calibrate PGA Gain stages
            ShowAction("AD5940_PGA_Calibration(C)", m_pData->GetVerbose() & 1);
            extern void AD5940_PGA_Calibration_Standalone();
            AD5940_PGA_Calibration_Standalone();
            return true;

        case 'E':
            // Run standard EIS frequency sweep
            ShowAction("eisScan(E)", m_pData->GetVerbose() & 1);
            DispatchEIS();
            return true;

        case 'f':   // stress loop test
            ShowAction("stress test(f)", m_pData->GetVerbose() & 1);
            while (!Serial.available()) {
                m_pData->ResetMeasurementPointer();
                DispatchEIS();
            }
            while (Serial.available()) Serial.read(); // Flush serial
            return true;

        case 'I':
            // Initialize OCP calibration state parameters
            DispatchOCP();
            m_pData->SetOCP_Calibration(false);
            m_pData->SetUseConstAB(false);
            return true;

        case 'M':   
            // Run Cyclic Voltammetry (CV) sweep
            DispatchCV();
            return true;

        case 'O':   
            // Run standard Open Circuit Potential (OCP) session
            DispatchOCP();
            return true;

        case 'P':
            // Run SeeedStat automated scan routines
            ShowAction("SeeedStatScan(P)", m_pData->GetVerbose() & 1);
            DispatchSeeedStat();
            m_pData->SetSeeedStatMode(false);
            return true;

        case 'T': {
            // Compute OCP millivolts and print results
            C_OCP c_OCP;
            c_OCP.Begin(m_pData);
            float ocp_mV = c_OCP.Calculate();
            char buf[30];
            sprintf(buf, "%.6f ", ocp_mV);
            Serial.println(buf);
            return true;
        }

        case 'U': {
            // Output raw OCP sum accumulated in AD5941
            uint32_t ocpSum = m_pData->GetOCP_Sum();
            Serial.write((uint8_t*)&ocpSum, 4);
            return true;
        }

        case 'Z':
            // Force hard chip re-initialization
            extern void AD5941_InitAll_Standalone();
            AD5941_InitAll_Standalone();
            return true;

        case 'A':
            // Run Chronoamperometry (CA) sweep
            ShowAction("RunCA(A)", m_pData->GetVerbose() & 1);
            DispatchCA();
            return true;

        case 'W':
            // Run Square Wave Voltammetry (SWV) sweep
            ShowAction("RunSWV(W)", m_pData->GetVerbose() & 1);
            DispatchSWV();
            return true;

        case 'D':
            // Run Differential Pulse Voltammetry (DPV) sweep
            ShowAction("RunDPV(D)", m_pData->GetVerbose() & 1);
            DispatchDPV();
            return true;
    }
    return false;
}


// =============================================================================
// ProcessCommand1Float — Updates Param Values with 1 Float Argument
// =============================================================================

/**
 * @brief Processes parameters updates containing one float value parameter.
 * @param cmd Parameter target letter identifier.
 * @param param New configuration value.
 */
bool C_Communication::ProcessCommand1Float(char cmd, float param) {
    int n = (int)param;

    switch (cmd) {
        case '@': m_pData->SetVerbose(n);                                         ShowParameter("verbose(@)=", n, m_pData->GetVerbose() & 1); return true;
        case 'a': m_pData->SetVZero(n);                                           ShowParameter("vzero(a)=",  n, m_pData->GetVerbose() & 1); return true;
        case 'B': m_pData->SetfBias(param); n = ConvertFloatBiasToUint16(param);   // Convert potential before saving
        case 'b': m_pData->SetVBias(n);                                           ShowParameter2("vbias(b)=%i (%.2f mV)", n, ConvertUint16BiasToFloat(n), m_pData->GetVerbose() & 1); return true;
        // The CV (cv.cpp) and EIS (c_eis.cpp) paths read the *global* fRcal, not
        // this parameter store. Writing only m_pData->fRcal made 'c' a silent
        // no-op for every actual measurement while '?' still reported the new
        // value - so a wrong RCAL could not be corrected at run time, and the
        // displayed calibration disagreed with the one in use. Write both.
        case 'c': { extern float fRcal; m_pData->SetfRcal(param); fRcal = param;
                    ShowParameter("Rcal(c)=", (int)param, m_pData->GetVerbose() & 1); return true; }
        case 'k': { extern uint8_t lptia_topology; lptia_topology = (uint8_t)n;
                    ShowParameter("lptia_topology(k)=", n, true); return true; }
        case 'g': m_pData->SetPGA_Gain(n);                                        ShowParameter("pga_gain(g)=", n, m_pData->GetVerbose() & 1); return true;
        case 'i': m_pData->SetConstA(param); m_pData->SetUseConstAB(true);        ShowParameterF("constA(i)=%.3f", param, m_pData->GetVerbose() & 1); return true;
        case 'j': m_pData->SetConstB(param); m_pData->SetUseConstAB(true);        ShowParameterF("constB(j)=%.3f", param, m_pData->GetVerbose() & 1); return true;
        case 'M': m_pData->SetWE_mV(param); m_pData->SetOCP_Calibration(true);    ShowParameterF("WEmV=%.0f", param, m_pData->GetVerbose() & 1); return true;
        case 'm': m_pData->SetEIS_Mode(n);                                        ShowParameter("EIS_mode(m)=", n, m_pData->GetVerbose() & 1); return true;
        case 'n': m_pData->SetOCP_Npts(n);                                        ShowParameter("OCP_npts(n)=", n, m_pData->GetVerbose() & 1); return true;
        case 'r': m_pData->SetTIA_Rf(n);                                          ShowParameter("tia_rf(r)=", n, m_pData->GetVerbose() & 1); return true;
        case 's': m_pData->SetUseVariableGain(n);                                 ShowParameter("use_variable_gain(s)=", n, m_pData->GetVerbose() & 1); return true;
        case 'S': m_pData->SetSeeedStatMode(n != 0);                              ShowParameter("SeeedStat(S)=", n, m_pData->GetVerbose() & 1); return true;
        case 't': m_pData->SetCGMax(n);                                           ShowParameter("CGmax(t)=", n, m_pData->GetVerbose() & 1); return true;
        case 'u': m_pData->SetCGMin(n);                                           ShowParameter("CGmin(u)=", n, m_pData->GetVerbose() & 1); return true;
        case 'V': m_pData->SetfOffset(param); n = ConvertFloatOffsetToUint16(param); // Convert offset before saving
        case 'v': m_pData->SetOffset(n);                                          ShowParameter2("offset(v)=%i (%.2f)", n, ConvertUint16OffsetToFloat(n), m_pData->GetVerbose() & 1); return true;
        case 'W': n = FreqToLabVIEW(param);                                       // Convert minimum frequency to mHz
        case 'w': m_pData->SetFreqLo(n);                                          ShowParameter("freqlo(w)=", n, m_pData->GetVerbose() & 1); return true;
        case 'X': n = FreqToLabVIEW(param);                                       // Convert maximum frequency to mHz
        case 'x': m_pData->SetFreqHi(n);                                          ShowParameter("freqhi(x)=", n, m_pData->GetVerbose() & 1); return true;
        case 'y': m_pData->SetNFreqs(n);                                          ShowParameter("nfreqs(y)=", n, m_pData->GetVerbose() & 1); return true;
        case 'Y': m_pData->SetfAmplitude(param); n = ConvertFloatAmplitudeToUint16(param); // Convert AC amplitude before saving
        case 'z': m_pData->SetAmplitude(n);                                       ShowParameter2("amplitude(z)=%i (0-pk %.1f mV)", n, ConvertUint16AmplitudeToFloat(n), m_pData->GetVerbose() & 1); return true;

        // --- CA target parameters ---
        case '1': m_pData->SetCA_Voltage_mV(param);  ShowParameterF("CA_Voltage_mV(1)=%.1f",  param, m_pData->GetVerbose() & 1); return true;
        case '2': m_pData->SetCA_Duration_s(param);  ShowParameterF("CA_Duration_s(2)=%.2f",  param, m_pData->GetVerbose() & 1); return true;
        case '3': m_pData->SetCA_SampleRate_Hz(param); ShowParameterF("CA_SampleRate(3)=%.1f", param, m_pData->GetVerbose() & 1); return true;

        // --- SWV target parameters ---
        case '4': m_pData->SetSWV_Start_mV(param);     ShowParameterF("SWV_Start(4)=%.1f",     param, m_pData->GetVerbose() & 1); return true;
        case '5': m_pData->SetSWV_End_mV(param);       ShowParameterF("SWV_End(5)=%.1f",       param, m_pData->GetVerbose() & 1); return true;
        case '6': m_pData->SetSWV_Step_mV(param);      ShowParameterF("SWV_Step(6)=%.2f",      param, m_pData->GetVerbose() & 1); return true;
        case '7': m_pData->SetSWV_Amplitude_mV(param); ShowParameterF("SWV_Amplitude(7)=%.1f", param, m_pData->GetVerbose() & 1); return true;
        case '8': m_pData->SetSWV_Frequency_Hz(param); ShowParameterF("SWV_Freq(8)=%.1f",      param, m_pData->GetVerbose() & 1); return true;

        // --- DPV target parameters ---
        case '9': m_pData->SetDPV_Start_mV(param);     ShowParameterF("DPV_Start(9)=%.1f",     param, m_pData->GetVerbose() & 1); return true;
        case '0': m_pData->SetDPV_End_mV(param);       ShowParameterF("DPV_End(0)=%.1f",       param, m_pData->GetVerbose() & 1); return true;
        case '!': m_pData->SetDPV_Step_mV(param);      ShowParameterF("DPV_Step(!)=%.2f",      param, m_pData->GetVerbose() & 1); return true;
        case '#': m_pData->SetDPV_Amplitude_mV(param); ShowParameterF("DPV_Amplitude(#)=%.1f", param, m_pData->GetVerbose() & 1); return true;
    }
    return false;
}


// =============================================================================
// ProcessCommand2Float — Updates Param Values with 2 Float Arguments
// =============================================================================

/**
 * @brief Configures settings containing two floating point parameters.
 */
bool C_Communication::ProcessCommand2Float(char cmd, float p1, float p2) {
    switch (cmd) {
        case 'D':   // SeeedStat format setup: configure scan frequency limits
            m_pData->SetSeeedStatMode(true);
            m_pData->SetFreqLo(FreqToLabVIEW(p1));
            m_pData->SetFreqHi(FreqToLabVIEW(p2));
            m_pData->SetV_Start(p1);
            m_pData->SetV_Stop(p2);
            return true;

        case 'O': { // Direct MMR write to AD5941 register
            extern void AD5940_WriteReg(uint16_t addr, uint32_t data);
            uint16_t addr = (uint16_t)p1;
            uint32_t data = (uint32_t)p2;
            AD5940_WriteReg(addr, data);
            char buf[100];
            sprintf(buf, "O 0x%X=0x%X", addr, data);
            Serial.println(buf);
            return true;
        }
    }
    return false;
}


// =============================================================================
// ProcessCommand1Int — Updates registers or parameters with 1 Int Argument
// =============================================================================

/**
 * @brief Processes commands containing one integer argument.
 */
bool C_Communication::ProcessCommand1Int(char cmd, uint16_t param) {
    switch (cmd) {
        case '@': m_pData->SetVerbose(param); return true;

        case 'I': { // Direct read of AD5941 registers
            extern uint32_t AD5940_ReadReg(uint16_t addr);
            uint32_t data = AD5940_ReadReg(param);
            char buf[100];
            sprintf(buf, "I 0x%X=0x%X", param, data);
            Serial.println(buf);
            return true;
        }

        case 'M':
            m_pData->SetWE_mV((float)param);
            m_pData->SetOCP_Calibration(true);
            return true;
    }
    return false;
}


// =============================================================================
// ProcessCommand2Int — Updates registers with 2 Int Arguments
// =============================================================================

/**
 * @brief Processes commands containing two integer arguments.
 */
bool C_Communication::ProcessCommand2Int(char cmd, uint32_t p1, uint32_t p2) {
    switch (cmd) {
        case 'O': { // MMR register write
            extern void AD5940_WriteReg(uint16_t addr, uint32_t data);
            AD5940_WriteReg((uint16_t)p1, p2);
            char buf[100];
            sprintf(buf, "O 0x%X=0x%X", (uint16_t)p1, p2);
            Serial.println(buf);
            return true;
        }
    }
    return false;
}


// =============================================================================
// Method Dispatch Wrappers (Instantiates and runs C++ electrochemical classes)
// =============================================================================

void C_Communication::DispatchEIS() {
    C_EIS c_EIS;
    c_EIS.Begin(m_pData);
    c_EIS.Run();
}

void C_Communication::DispatchSeeedStat() {
    C_EIS c_EIS;
    c_EIS.Begin(m_pData);
    c_EIS.RunSeeedStat();
}

void C_Communication::DispatchOCP() {
    C_OCP c_OCP;
    c_OCP.Begin(m_pData);
    c_OCP.Run();
}

void C_Communication::DispatchCA() {
    C_CA c_CA;
    c_CA.Begin(m_pData);
    c_CA.Run();
}

void C_Communication::DispatchSWV() {
    C_SWV c_SWV;
    c_SWV.Begin(m_pData);
    c_SWV.Run();
}

void C_Communication::DispatchDPV() {
    C_DPV c_DPV;
    c_DPV.Begin(m_pData);
    c_DPV.Run();
}

void C_Communication::DispatchCV() {
    C_CV c_CV;
    c_CV.Begin(m_pData);
    c_CV.Run();
}


// =============================================================================
// Output Status Helper Functions
// =============================================================================

/**
 * @brief Prints current parameter settings list to serial output.
 */
void C_Communication::ShowParameters() {
    Serial.println("----------------------");
    ShowParameter ("verbose          (@)=", m_pData->GetVerbose(),          true);
    ShowParameter ("SeeedStat mode   (S)=", m_pData->GetSeeedStatMode(),    true);
    ShowParameter ("pga_gain         (g)=", m_pData->GetPGA_Gain(),         true);
    ShowParameter ("tia_rf           (r)=", m_pData->GetTIA_Rf(),           true);
    ShowParameterF("constA           (i)=%.3f", m_pData->GetConstA(),       true);
    ShowParameterF("constB           (j)=%.3f", m_pData->GetConstB(),       true);
    ShowParameter ("useConstAB          =", m_pData->GetUseConstAB(),       true);
    ShowParameter ("EIS_mode         (m)=", m_pData->GetEIS_Mode(),         true);
    ShowParameter ("OCP_npts         (n)=", m_pData->GetOCP_Npts(),         true);
    ShowParameter ("vzero            (a)=", m_pData->GetVZero(),            true);
    ShowParameter ("use_variable_gain(s)=", m_pData->GetUseVariableGain(),  true);
    ShowParameter ("nfreqs           (y)=", m_pData->GetNFreqs(),           true);
    ShowParameter2("vbias            (b)=%i (%.1f mV)",
                   m_pData->GetVBias(), ConvertUint16BiasToFloat(m_pData->GetVBias()), true);
    ShowParameter2("amplitude        (z)=%i (0-pk %.1f mV)",
                   m_pData->GetAmplitude(), ConvertUint16AmplitudeToFloat(m_pData->GetAmplitude()), true);
    ShowParameter2("offset           (v)=%i (%.1f mV)",
                   m_pData->GetOffset(), ConvertUint16OffsetToFloat(m_pData->GetOffset()), true);
    ShowParameter ("freqlo           (w)=", m_pData->GetFreqLo(),           true);
    ShowParameter ("freqhi           (x)=", m_pData->GetFreqHi(),           true);
    ShowParameter ("CGmax            (t)=", m_pData->GetCGMax(),            true);
    ShowParameter ("CGmin            (u)=", m_pData->GetCGMin(),            true);
    { extern float fRcal;   // report the value the measurement path actually uses
      ShowParameter ("rcal             (c)=", (int)fRcal,             true); }
    ShowParameter ("measurements        =", m_pData->GetNumberOfMeasurements(), true);
    Serial.println("----------------------");
    Serial.println("E=eisScan  P=SeeedStat  A=CA  W=SWV  D=DPV  M=CV  O=OCP  ?=params");
    Serial.println("----------------------");
}

void C_Communication::ShowParameter(const char* name, int value, bool verb) {
    if (!verb) return;
    Serial.print(name);
    Serial.println(value);
}

void C_Communication::ShowParameter8(const char* name, uint8_t value, bool verb) {
    if (!verb) return;
    Serial.print(name);
    Serial.println(value == 0 ? "Rz" : "Rcal");
}

void C_Communication::ShowParameterF(const char* format, float value, bool verb) {
    if (!verb) return;
    char buf[200];
    sprintf(buf, format, value);
    Serial.println(buf);
}

void C_Communication::ShowParameter2(const char* format, int v1, float v2, bool verb) {
    if (!verb) return;
    char buf[200];
    sprintf(buf, format, v1, v2);
    Serial.println(buf);
}

void C_Communication::ShowAction(const char* name, bool verb) {
    if (!verb) return;
    Serial.println(name);
}


// =============================================================================
// History Log Managers
// =============================================================================

/**
 * @brief Records individual command strings to the cyclic history log buffer.
 * @param token Command token string.
 */
void C_Communication::AddCommandToHistory(const char* token) {
    size_t remaining = HISTORY_BUFFER_SIZE - (m_pHistory - m_History) - 1;
    size_t len       = strlen(token);
    if (len + 2 > remaining) {
        // Buffer filled: wrap around back to start
        m_pHistory = m_History;
    }
    strncpy(m_pHistory, token, remaining);
    m_pHistory += len;
    *m_pHistory++ = '\n';
    *m_pHistory   = 0;
}

/**
 * @brief Prints history logs to serial and clears the index.
 */
void C_Communication::PrintHistory() {
    Serial.println(m_History);
    m_pHistory = m_History;
}


// =============================================================================
// Conversion Helper Functions (Static mapping calculations)
// =============================================================================

uint16_t C_Communication::ConvertFloatBiasToUint16(float value) {
    return (uint16_t)(1664 + (int)(-value * (1850.0f - 1664.0f) / 100.0f + 0.5f));
}
float C_Communication::ConvertUint16BiasToFloat(uint16_t value) {
    return (float)((value - 1664.0f) * 100.0f / (1850.0f - 1664.0f));
}
uint16_t C_Communication::ConvertFloatAmplitudeToUint16(float value) {
    return (uint16_t)(value * 126.0f / 50.0f + 0.5f);
}
float C_Communication::ConvertUint16AmplitudeToFloat(uint16_t value) {
    return (float)(value * 50.0f / 126.0f);
}
uint16_t C_Communication::ConvertFloatOffsetToUint16(float value) {
    return (uint16_t)((int16_t)value - 56) & 0xFFF;
}
float C_Communication::ConvertUint16OffsetToFloat(uint16_t value) {
    float result = (float)value;
    if (value & (1 << 11)) {
        result = (float)((int16_t)(value | 0xF000));
    }
    return result + 56.0f;
}
uint32_t C_Communication::FreqToLabVIEW(float param) {
    return (uint32_t)(param * 1000.0f); // Convert Hz to mHz (LabVIEW format)
}

/**
 * @brief Parses the second parameter of 2-parameter commands (separated by commas).
 */
uint32_t C_Communication::ParseTokenForHex2(const char* token) {
    uint32_t hex2 = 0;
    const char* pc = strchr(token, ',');
    if (pc != nullptr) {
        char c;
        for (pc += 3; (c = *pc) != 0; ++pc) {
            hex2 <<= 4;
            hex2 += (c <= '9') ? c - '0'
                  : (c <= 'F') ? c - 'A' + 10
                  :               c - 'a' + 10;
        }
    }
    return hex2;
}