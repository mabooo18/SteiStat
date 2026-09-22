/******************************************************************************
 * @file:    data_storage.h
 * @brief:   Centralized parameter and state storage for HunStat2
 *
 * Menggantikan semua variabel global di AD5941_25.ino dengan satu class
 * terpusat. Semua class lain menerima pointer ke C_DataStorage.
 *
 * @author:  Refactored from AD5941_25.ino by Richard Morrison
 * @version: V 1.0.0
 *****************************************************************************/

#ifndef DATA_STORAGE_H
#define DATA_STORAGE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// System state constants
// ---------------------------------------------------------------------------
#define HUNSTAT_WAITING     0
#define HUNSTAT_RUNNING     1
#define HUNSTAT_DONE        2

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
#define DEFAULT_RCAL            10000.0f
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
    uint8_t  SystemStatus;          // HUNSTAT_WAITING / RUNNING / DONE
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