/******************************************************************************
 * @file:    communication.h
 * @brief:   Serial communication dan command parser untuk HunStat2
 *
 * Membungkus semua fungsi ProcessCommand*, ProcessToken,
 * SplitAndProcessCommands, ShowParameter*, dan AddCommandToHistory
 * dari AD5941_25.ino ke dalam satu class.
 *
 * Penggunaan:
 *   C_Communication c_Comm;
 *   c_Comm.Begin(1000000, &c_Data);
 *   while (true) { c_Comm.ReadAndProcess(&c_Data); }
 *****************************************************************************/

#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <Arduino.h>
#include "../data_storage/data_storage.h"

// ---------------------------------------------------------------------------
// Forward declarations — class EC methods dipanggil dari sini
// ---------------------------------------------------------------------------
class C_EIS;
class C_OCP;
class C_CA;
class C_SWV;
class C_DPV;

// ---------------------------------------------------------------------------
// Command history buffer
// ---------------------------------------------------------------------------
#define HISTORY_BUFFER_SIZE  2000
#define SERIAL_BUFFER_SIZE   1000

// ---------------------------------------------------------------------------
// C_Communication
// ---------------------------------------------------------------------------
class C_Communication {
public:

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief  Inisialisasi Serial dan simpan referensi ke data storage.
     * @param  baudrate   Baud rate Serial (biasanya 1000000)
     * @param  pData      Pointer ke C_DataStorage yang sudah di-Begin()
     */
    void Begin(uint32_t baudrate, C_DataStorage* pData);

    // -----------------------------------------------------------------------
    // Main loop interface
    // -----------------------------------------------------------------------

    /**
     * @brief  Baca satu batch dari Serial lalu proses semua command di dalamnya.
     *         Dipanggil setiap iterasi loop().
     */
    void ReadAndProcess();

    // -----------------------------------------------------------------------
    // Output helpers (dipanggil dari class EC methods)
    // -----------------------------------------------------------------------

    void ShowParameters();

    void ShowParameter (const char* name, int   value,  bool verb);
    void ShowParameter8(const char* name, uint8_t value, bool verb);
    void ShowParameterF(const char* format, float value, bool verb);
    void ShowParameter2(const char* format, int value1, float value2, bool verb);
    void ShowAction    (const char* name, bool verb);

    // -----------------------------------------------------------------------
    // History
    // -----------------------------------------------------------------------

    void AddCommandToHistory(const char* token);
    void PrintHistory();

    // -----------------------------------------------------------------------
    // Conversion helpers (static — tidak butuh state)
    // -----------------------------------------------------------------------

    static uint16_t ConvertFloatBiasToUint16    (float value);
    static float    ConvertUint16BiasToFloat    (uint16_t value);
    static uint16_t ConvertFloatAmplitudeToUint16(float value);
    static float    ConvertUint16AmplitudeToFloat(uint16_t value);
    static uint16_t ConvertFloatOffsetToUint16  (float value);
    static float    ConvertUint16OffsetToFloat  (uint16_t value);
    static uint32_t FreqToLabVIEW               (float param);

private:

    // -----------------------------------------------------------------------
    // Internal state
    // -----------------------------------------------------------------------
    C_DataStorage* m_pData;
    char           m_History[HISTORY_BUFFER_SIZE];
    char*          m_pHistory;

    // -----------------------------------------------------------------------
    // Command parsing (private — dipanggil dari ReadAndProcess)
    // -----------------------------------------------------------------------

    void SplitAndProcessCommands(char* buf);
    void ProcessToken(char* token);

    // Dispatcher bertingkat: command tanpa param, 1 param, 2 param
    bool ProcessCommand        (char cmd);
    bool ProcessCommand1Float  (char cmd, float param);
    bool ProcessCommand2Float  (char cmd, float p1, float p2);
    bool ProcessCommand1Int    (char cmd, uint16_t param);
    bool ProcessCommand2Int    (char cmd, uint32_t p1, uint32_t p2);

    // Helper parser hex ganda
    uint32_t ParseTokenForHex2(const char* token);

    // -----------------------------------------------------------------------
    // EC method dispatch (dipanggil dari ProcessCommand)
    // Masing-masing membuat object method, memanggil Run(), lalu destroy.
    // -----------------------------------------------------------------------
    void DispatchEIS();
    void DispatchSeeedStat();
    void DispatchOCP();
    void DispatchCA();
    void DispatchSWV();
    void DispatchDPV();
    void DispatchCV();
};

#endif /* COMMUNICATION_H */