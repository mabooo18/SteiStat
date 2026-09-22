/******************************************************************************
 * @file:    data_storage.h
 * @brief:   Centralized parameter and state storage for SteiStat
 *
 * Menggantikan semua variabel global di AD5941_25.ino dengan satu class
 * terpusat. Semua class lain menerima pointer ke C_DataStorage.
 *
 * Fields are private; every field has a Get<Field>()/Set<Field>(value)
 * accessor pair below. This replaces the previous all-public-fields
 * struct-shape the project's own OOP audit flagged as "a struct wearing a
 * class keyword, not an encapsulating abstraction."
 *
 * @author:  Refactored from AD5941_25.ino by Richard Morrison
 * @version: V 1.1.0
 *****************************************************************************/

#ifndef DATA_STORAGE_H
#define DATA_STORAGE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// System state constants
// ---------------------------------------------------------------------------
#define STEISTAT_WAITING     0
#define STEISTAT_RUNNING     1
#define STEISTAT_DONE        2

// ---------------------------------------------------------------------------
// Measurement mode constants
// ---------------------------------------------------------------------------
#define MODE_EIS_RZ         0   // EIS measuring unknown impedance
#define MODE_EIS_RCAL       1   // EIS measuring calibration resistor

// ---------------------------------------------------------------------------
// Default EIS parameters (dari AD5941_25.ino)
// ---------------------------------------------------------------------------
#define DEFAULT_PGA_GAIN        1
#define DEFAULT_TIA_RF          3
#define DEFAULT_NFREQS          50
#define DEFAULT_AMPLITUDE       126
#define DEFAULT_VBIAS           1664
#define DEFAULT_VZERO           26
#define DEFAULT_OFFSET          4040
#define DEFAULT_RCAL              200.0f   // must match the fitted RCAL jumper (RCAL1)
#define DEFAULT_FAMPLITUDE      50.0f
#define DEFAULT_FBIAS           0.0f
#define DEFAULT_FOFFSET         0.0f
#define DEFAULT_CGMAX           30000
#define DEFAULT_CGMIN           7500

// ---------------------------------------------------------------------------
// Default OCP parameters
// ---------------------------------------------------------------------------
#define DEFAULT_OCP_NPTS        10
#define DEFAULT_CONST_A         32772.0f
#define DEFAULT_CONST_B         -26.719f

// ---------------------------------------------------------------------------
// Default CA parameters
// ---------------------------------------------------------------------------
#define DEFAULT_CA_VOLTAGE_MV   0.0f
#define DEFAULT_CA_DURATION_S   1.0f
#define DEFAULT_CA_SAMPLERATE   100.0f

// ---------------------------------------------------------------------------
// Default SWV parameters
// ---------------------------------------------------------------------------
#define DEFAULT_SWV_START       -100.0f
#define DEFAULT_SWV_END          100.0f
#define DEFAULT_SWV_STEP          5.0f
#define DEFAULT_SWV_AMPLITUDE    25.0f
#define DEFAULT_SWV_FREQUENCY    50.0f
#define DEFAULT_SWV_SAMPLEDELAY  0.02f

// ---------------------------------------------------------------------------
// Default DPV parameters
// ---------------------------------------------------------------------------
#define DEFAULT_DPV_START        -100.0f
#define DEFAULT_DPV_END           100.0f
#define DEFAULT_DPV_STEP           5.0f
#define DEFAULT_DPV_AMPLITUDE     50.0f
#define DEFAULT_DPV_PULSEWIDTH     0.05f
#define DEFAULT_DPV_PULSEPERIOD    0.2f
#define DEFAULT_DPV_SAMPLEDELAY    0.02f

// ---------------------------------------------------------------------------
// EIS result buffer size
// 4: real+imag untuk Rz dan Rcal
// 7: max decade (0.1 Hz s.d. 100 kHz)
// 20: max frekuensi per decade
// ---------------------------------------------------------------------------
#define EIS_MEASUREMENT_BUFFER_SIZE  (4 * 7 * 20)


class C_DataStorage {
public:

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief  Inisialisasi semua parameter ke nilai default.
     *         Dipanggil sekali di awal loop() sebelum class lain dibuat.
     */
    void Begin();

    // -----------------------------------------------------------------------
    // System state
    // -----------------------------------------------------------------------
    uint8_t  GetSystemStatus() const           { return SystemStatus; }
    void     SetSystemStatus(uint8_t v)        { SystemStatus = v; }
    bool     GetSeeedStatMode() const          { return SeeedStatMode; }
    void     SetSeeedStatMode(bool v)          { SeeedStatMode = v; }
    uint32_t GetVerbose() const                { return Verbose; }
    void     SetVerbose(uint32_t v)            { Verbose = v; }

    // -----------------------------------------------------------------------
    // EIS parameters
    // -----------------------------------------------------------------------
    uint8_t  GetPGA_Gain() const                { return PGA_Gain; }
    void     SetPGA_Gain(uint8_t v)             { PGA_Gain = v; }
    uint8_t  GetTIA_Rf() const                  { return TIA_Rf; }
    void     SetTIA_Rf(uint8_t v)               { TIA_Rf = v; }
    uint8_t  GetEIS_Mode() const                { return EIS_Mode; }
    void     SetEIS_Mode(uint8_t v)             { EIS_Mode = v; }
    uint16_t GetNFreqs() const                  { return NFreqs; }
    void     SetNFreqs(uint16_t v)              { NFreqs = v; }
    uint16_t GetAmplitude() const               { return Amplitude; }
    void     SetAmplitude(uint16_t v)           { Amplitude = v; }
    uint16_t GetVBias() const                   { return VBias; }
    void     SetVBias(uint16_t v)               { VBias = v; }
    uint16_t GetVZero() const                   { return VZero; }
    void     SetVZero(uint16_t v)               { VZero = v; }
    uint16_t GetOffset() const                  { return Offset; }
    void     SetOffset(uint16_t v)              { Offset = v; }
    uint32_t GetFreqLo() const                  { return FreqLo; }
    void     SetFreqLo(uint32_t v)              { FreqLo = v; }
    uint32_t GetFreqHi() const                  { return FreqHi; }
    void     SetFreqHi(uint32_t v)              { FreqHi = v; }
    uint32_t GetCGMax() const                   { return CGMax; }
    void     SetCGMax(uint32_t v)               { CGMax = v; }
    uint32_t GetCGMin() const                   { return CGMin; }
    void     SetCGMin(uint32_t v)               { CGMin = v; }
    uint8_t  GetUseVariableGain() const         { return UseVariableGain; }
    void     SetUseVariableGain(uint8_t v)      { UseVariableGain = v; }
    float    GetfRcal() const                   { return fRcal; }
    void     SetfRcal(float v)                  { fRcal = v; }
    float    GetfAmplitude() const              { return fAmplitude; }
    void     SetfAmplitude(float v)             { fAmplitude = v; }
    float    GetfBias() const                   { return fBias; }
    void     SetfBias(float v)                  { fBias = v; }
    float    GetfOffset() const                 { return fOffset; }
    void     SetfOffset(float v)                { fOffset = v; }

    // -----------------------------------------------------------------------
    // EIS result buffer
    // -----------------------------------------------------------------------
    float    GetMeasurementAt(int i) const      { return Measurements[i]; }
    void     SetMeasurementAt(int i, float v)   { Measurements[i] = v; }
    float*   GetMeasurementsPtr()               { return Measurements; }
    void     ResetMeasurementPointer()          { pMeasurement = Measurements; }
    int      GetNumberOfMeasurements() const    { return NumberOfMeasurements; }
    void     SetNumberOfMeasurements(int v)     { NumberOfMeasurements = v; }

    // -----------------------------------------------------------------------
    // OCP parameters
    // -----------------------------------------------------------------------
    uint16_t GetOCP_Npts() const                { return OCP_Npts; }
    void     SetOCP_Npts(uint16_t v)            { OCP_Npts = v; }
    uint32_t GetOCP_Sum() const                 { return OCP_Sum; }
    void     SetOCP_Sum(uint32_t v)             { OCP_Sum = v; }
    uint32_t GetADCCON() const                  { return ADCCON; }
    void     SetADCCON(uint32_t v)              { ADCCON = v; }
    float    GetConstA() const                  { return ConstA; }
    void     SetConstA(float v)                 { ConstA = v; }
    float    GetConstB() const                  { return ConstB; }
    void     SetConstB(float v)                 { ConstB = v; }
    bool     GetUseConstAB() const              { return UseConstAB; }
    void     SetUseConstAB(bool v)              { UseConstAB = v; }
    float    GetWE_mV() const                   { return WE_mV; }
    void     SetWE_mV(float v)                  { WE_mV = v; }
    float    GetWEFrom_mV() const               { return WEFrom_mV; }
    void     SetWEFrom_mV(float v)              { WEFrom_mV = v; }
    float    GetWETo_mV() const                 { return WETo_mV; }
    void     SetWETo_mV(float v)                { WETo_mV = v; }
    float    GetWEStep_mV() const               { return WEStep_mV; }
    void     SetWEStep_mV(float v)              { WEStep_mV = v; }
    bool     GetOCP_Calibration() const         { return OCP_Calibration; }
    void     SetOCP_Calibration(bool v)         { OCP_Calibration = v; }
    bool     GetOCP_CalibrationCycling() const  { return OCP_CalibrationCycling; }
    void     SetOCP_CalibrationCycling(bool v)  { OCP_CalibrationCycling = v; }
    uint32_t GetHSDACDAT() const                { return HSDACDAT; }
    void     SetHSDACDAT(uint32_t v)            { HSDACDAT = v; }

    // -----------------------------------------------------------------------
    // CV parameters (digunakan cv.cpp)
    // -----------------------------------------------------------------------
    float    GetV_Start() const                 { return V_Start; }
    void     SetV_Start(float v)                { V_Start = v; }
    float    GetV_Stop() const                  { return V_Stop; }
    void     SetV_Stop(float v)                 { V_Stop = v; }
    float    GetEStep() const                   { return EStep; }
    void     SetEStep(float v)                  { EStep = v; }
    float    GetScanRate() const                { return ScanRate; }
    void     SetScanRate(float v)               { ScanRate = v; }
    uint16_t GetCycleNumber() const             { return CycleNumber; }
    void     SetCycleNumber(uint16_t v)         { CycleNumber = v; }

    // -----------------------------------------------------------------------
    // CA parameters
    // -----------------------------------------------------------------------
    float    GetCA_Voltage_mV() const           { return CA_Voltage_mV; }
    void     SetCA_Voltage_mV(float v)          { CA_Voltage_mV = v; }
    float    GetCA_Duration_s() const           { return CA_Duration_s; }
    void     SetCA_Duration_s(float v)          { CA_Duration_s = v; }
    float    GetCA_SampleRate_Hz() const        { return CA_SampleRate_Hz; }
    void     SetCA_SampleRate_Hz(float v)       { CA_SampleRate_Hz = v; }
    uint32_t GetCA_NumSamples() const           { return CA_NumSamples; }
    void     SetCA_NumSamples(uint32_t v)       { CA_NumSamples = v; }

    // -----------------------------------------------------------------------
    // SWV parameters
    // -----------------------------------------------------------------------
    float    GetSWV_Start_mV() const            { return SWV_Start_mV; }
    void     SetSWV_Start_mV(float v)           { SWV_Start_mV = v; }
    float    GetSWV_End_mV() const              { return SWV_End_mV; }
    void     SetSWV_End_mV(float v)             { SWV_End_mV = v; }
    float    GetSWV_Step_mV() const             { return SWV_Step_mV; }
    void     SetSWV_Step_mV(float v)            { SWV_Step_mV = v; }
    float    GetSWV_Amplitude_mV() const        { return SWV_Amplitude_mV; }
    void     SetSWV_Amplitude_mV(float v)       { SWV_Amplitude_mV = v; }
    float    GetSWV_Frequency_Hz() const        { return SWV_Frequency_Hz; }
    void     SetSWV_Frequency_Hz(float v)       { SWV_Frequency_Hz = v; }
    float    GetSWV_SampleDelay_s() const       { return SWV_SampleDelay_s; }
    void     SetSWV_SampleDelay_s(float v)      { SWV_SampleDelay_s = v; }

    // -----------------------------------------------------------------------
    // DPV parameters
    // -----------------------------------------------------------------------
    float    GetDPV_Start_mV() const            { return DPV_Start_mV; }
    void     SetDPV_Start_mV(float v)           { DPV_Start_mV = v; }
    float    GetDPV_End_mV() const              { return DPV_End_mV; }
    void     SetDPV_End_mV(float v)             { DPV_End_mV = v; }
    float    GetDPV_Step_mV() const             { return DPV_Step_mV; }
    void     SetDPV_Step_mV(float v)            { DPV_Step_mV = v; }
    float    GetDPV_Amplitude_mV() const        { return DPV_Amplitude_mV; }
    void     SetDPV_Amplitude_mV(float v)       { DPV_Amplitude_mV = v; }
    float    GetDPV_PulseWidth_s() const        { return DPV_PulseWidth_s; }
    void     SetDPV_PulseWidth_s(float v)       { DPV_PulseWidth_s = v; }
    float    GetDPV_PulsePeriod_s() const       { return DPV_PulsePeriod_s; }
    void     SetDPV_PulsePeriod_s(float v)      { DPV_PulsePeriod_s = v; }
    float    GetDPV_SampleDelay_s() const       { return DPV_SampleDelay_s; }
    void     SetDPV_SampleDelay_s(float v)      { DPV_SampleDelay_s = v; }

private:
    // -----------------------------------------------------------------------
    // System state
    // -----------------------------------------------------------------------
    uint8_t  SystemStatus;          // STEISTAT_WAITING / RUNNING / DONE
    bool     SeeedStatMode;         // true = mode SeeedStat, false = LabVIEW
    uint32_t Verbose;               // bitmask verbosity (lihat dokumentasi)

    // -----------------------------------------------------------------------
    // EIS parameters
    // -----------------------------------------------------------------------
    uint8_t  PGA_Gain;              // kode PGA: 0=1x, 1=1.5x, 2=2x, 3=4x, 4=9x
    uint8_t  TIA_Rf;                // kode feedback resistor TIA (0..7)
    uint8_t  EIS_Mode;              // MODE_EIS_RZ atau MODE_EIS_RCAL
    uint16_t NFreqs;                // jumlah titik frekuensi
    uint16_t Amplitude;             // amplitudo sinusoidal (kode DAC)
    uint16_t VBias;                 // bias DC (kode DAC 12-bit)
    uint16_t VZero;                 // Vzero LPDAC (kode 6-bit)
    uint16_t Offset;                // offset waveform (kode DAC)
    uint32_t FreqLo;                // frekuensi bawah (dalam mHz, x1000)
    uint32_t FreqHi;                // frekuensi atas  (dalam mHz, x1000)
    uint32_t CGMax;                 // combined gain maks (untuk auto-gain)
    uint32_t CGMin;                 // combined gain min  (untuk auto-gain)
    uint8_t  UseVariableGain;       // 0 = gain tetap, 1 = auto-gain per frekuensi
    float    fRcal;                 // nilai resistor kalibrasi (Ohm)
    float    fAmplitude;            // amplitudo dalam mV
    float    fBias;                 // bias dalam mV
    float    fOffset;               // offset dalam mV

    // -----------------------------------------------------------------------
    // EIS result buffer
    // -----------------------------------------------------------------------
    float    Measurements[EIS_MEASUREMENT_BUFFER_SIZE];
    float*   pMeasurement;          // pointer bergerak saat mengisi buffer
    int      NumberOfMeasurements;  // jumlah pasang (real,imag) yang sudah terisi

    // -----------------------------------------------------------------------
    // OCP parameters
    // -----------------------------------------------------------------------
    uint16_t OCP_Npts;              // jumlah sampel OCP per pengukuran
    uint32_t OCP_Sum;               // akumulator raw ADC untuk rata-rata
    uint32_t ADCCON;                // snapshot register ADCCON saat OCP
    float    ConstA;                // koef. A untuk kalibrasil OCP linier
    float    ConstB;                // koef. B untuk kalibrasi OCP linier
    bool     UseConstAB;            // true = gunakan kalibrasi linier
    float    WE_mV;                 // tegangan WE target (OCP calibration)
    float    WEFrom_mV;             // awal sweep OCP cycling
    float    WETo_mV;               // akhir sweep OCP cycling
    float    WEStep_mV;             // langkah sweep OCP cycling
    bool     OCP_Calibration;       // sedang dalam mode kalibrasi OCP
    bool     OCP_CalibrationCycling;// sedang dalam mode cycling OCP
    uint32_t HSDACDAT;              // nilai HSDACDAT terakhir (untuk OCP calc)

    // -----------------------------------------------------------------------
    // CV parameters (digunakan cv.cpp)
    // -----------------------------------------------------------------------
    float    V_Start;               // tegangan awal CV (mV)
    float    V_Stop;                // tegangan akhir CV (mV)
    float    EStep;                 // step potensial CV (mV)
    float    ScanRate;              // scan rate CV (mV/s)
    uint16_t CycleNumber;           // jumlah siklus CV

    // -----------------------------------------------------------------------
    // CA parameters
    // -----------------------------------------------------------------------
    float    CA_Voltage_mV;
    float    CA_Duration_s;
    float    CA_SampleRate_Hz;
    uint32_t CA_NumSamples;         // dihitung saat RunCA() dipanggil

    // -----------------------------------------------------------------------
    // SWV parameters
    // -----------------------------------------------------------------------
    float    SWV_Start_mV;
    float    SWV_End_mV;
    float    SWV_Step_mV;
    float    SWV_Amplitude_mV;
    float    SWV_Frequency_Hz;
    float    SWV_SampleDelay_s;

    // -----------------------------------------------------------------------
    // DPV parameters
    // -----------------------------------------------------------------------
    float    DPV_Start_mV;
    float    DPV_End_mV;
    float    DPV_Step_mV;
    float    DPV_Amplitude_mV;
    float    DPV_PulseWidth_s;
    float    DPV_PulsePeriod_s;
    float    DPV_SampleDelay_s;
};

#endif /* DATA_STORAGE_H */
