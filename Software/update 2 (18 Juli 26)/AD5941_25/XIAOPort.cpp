// Porting file that maps AD5940/AD5941 SPI and hardware pins to the Seeed Studio XIAO RP2040 board.

#include "ad5940.h"
#include <Arduino.h>

// Microcontroller pin assignments
#define SPI_CS_AD5940_Pin D7  // Pin D7: Chip Select (active LOW) for SPI
#define AD5940_ResetPin   A1  // Pin A1: Reset line (active LOW) for AD5941
#define AD5940_IntPin     A2  // Pin A2: Interrupt Input from AD5941's Gp0 pin

// This board wires the AD5941's MISO to D10 and MOSI to D9 - the reverse of
// the XIAO RP2040's hardware SPI0 role assignment for those same physical
// pins (SCK=D8/GPIO2, MISO=D9/GPIO4, MOSI=D10/GPIO3). The silicon's SPI
// peripheral pin-mux can't swap that role, so this uses a small bit-banged
// software SPI below instead of the hardware SPI peripheral.
#define SPI_NEW_SCK       D8
#define SPI_NEW_MOSI      D9
#define SPI_NEW_MISO      D10

// Minimal bit-banged SPI (Mode 0: CPOL=0, CPHA=0, MSB first) for pin
// combinations the hardware SPI peripheral cannot mux.
class BitBangSPI
{
public:
    void begin(uint8_t sck, uint8_t mosi, uint8_t miso)
    {
        _sck = sck;
        _mosi = mosi;
        _miso = miso;
        pinMode(_sck, OUTPUT);
        pinMode(_mosi, OUTPUT);
        pinMode(_miso, INPUT);
        digitalWrite(_sck, LOW);
    }

    uint8_t transfer(uint8_t data)
    {
        uint8_t result = 0;
        for (int8_t bit = 7; bit >= 0; --bit)
        {
            digitalWrite(_mosi, (data >> bit) & 0x01);
            digitalWrite(_sck, HIGH);
            result = (result << 1) | digitalRead(_miso);
            digitalWrite(_sck, LOW);
        }
        return result;
    }

private:
    uint8_t _sck, _mosi, _miso;
};

static BitBangSPI customSPI;

// Expose internal pins globally for use in other modules
int csPin = SPI_CS_AD5940_Pin;
int resetPin = AD5940_ResetPin;
int intPin = AD5940_IntPin;

// Interrupt flag used to signal the microcontroller that the AD5941 has triggered an interrupt (e.g. data ready)
volatile static uint32_t ucInterrupted = 0;       

/**
 * @brief Retrieves the microcontroller's interrupt flag state.
 * @return 1 if an interrupt occurred, 0 otherwise.
 */
uint32_t AD5940_GetMCUIntFlag(void)
{
    return ucInterrupted;
}

/**
 * @brief Clears the microcontroller's interrupt flag.
 * @return Always returns 1.
 */
uint32_t AD5940_ClrMCUIntFlag(void)
{
    ucInterrupted = 0;
    return 1;
}

/**
 * @brief Interrupt Service Routine (ISR) triggered by the AD5941 interrupt pin falling edge.
 *        Lighter ISR that sets a flag to be processed within the main polling loop.
 */
void Ext_Int0_Handler()
{
    ucInterrupted = 1;
}

/**
 * @brief Platform-specific delay wrapper (10-microsecond multiplier).
 * @param time Count of 10-microsecond units to delay.
 */
void AD5940_Delay10us(uint32_t time)
{
    delayMicroseconds(time * 10); 
}

/**
 * @brief High-speed full-duplex SPI transfer function.
 *        Reads and writes N bytes simultaneously over SPI at 12 MHz.
 * @param pSendBuffer Pointer to the buffer containing data bytes to send.
 * @param pRecvBuff Pointer to the buffer where received data bytes will be written.
 * @param length The total number of bytes to transfer.
 */
void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length)
{
    for (unsigned long i = 0; i < length; i++)
    {
        // Transfer byte-by-byte through the bit-banged SPI shifter
        *pRecvBuff++ = customSPI.transfer(*pSendBuffer++);
    }
}

/**
 * @brief Clears (pulls LOW) the SPI Chip Select pin to activate AD5941 communication.
 */
void AD5940_CsClr(void)
{
    digitalWrite(SPI_CS_AD5940_Pin, LOW);
}

/**
 * @brief Sets (pulls HIGH) the SPI Chip Select pin to deactivate AD5941 communication.
 */
void AD5940_CsSet(void)
{
    digitalWrite(SPI_CS_AD5940_Pin, HIGH);
}

/**
 * @brief Deasserts the AD5941 Reset line (pulls HIGH) to allow normal operation.
 */
void AD5940_RstSet(void)
{
    digitalWrite(AD5940_ResetPin, HIGH);
}

/**
 * @brief Asserts the AD5941 Reset line (pulls LOW) to force a hard hardware reset.
 */
void AD5940_RstClr(void)
{
    digitalWrite(AD5940_ResetPin, LOW);
}

/**
 * @brief Initializes SPI bus and registers the external falling-edge pin interrupt.
 * @param pCfg Unused config parameter block.
 * @return Always returns 0 on successful setup.
 */
uint32_t AD5940_MCUResourceInit(void *pCfg)
{
    // Bring up the bit-banged SPI bus on this board's actual AD5941 wiring.
    customSPI.begin(SPI_NEW_SCK, SPI_NEW_MOSI, SPI_NEW_MISO);

    // Set pin drive modes
    pinMode(SPI_CS_AD5940_Pin, OUTPUT);
    pinMode(AD5940_ResetPin, OUTPUT);
    
    // Configure interrupt line with pullup and attach falling-edge ISR
    pinMode(AD5940_IntPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(AD5940_IntPin), Ext_Int0_Handler, FALLING);

    // Establish default idle pin levels
    AD5940_CsSet();  // CS starts HIGH (inactive)
    AD5940_RstSet(); // RESET starts HIGH (running)
    return 0;
}
