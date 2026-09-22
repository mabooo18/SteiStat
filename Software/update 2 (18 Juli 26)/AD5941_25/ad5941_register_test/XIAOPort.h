#ifndef XIAOPORT_H
#define XIAOPORT_H

#include <stdint.h>

extern int csPin;
extern int resetPin;
extern int intPin;

uint32_t AD5940_GetMCUIntFlag(void);
uint32_t AD5940_ClrMCUIntFlag(void);
void Ext_Int0_Handler();
void AD5940_Delay10us(uint32_t time);
void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length);
void AD5940_CsClr(void);
void AD5940_CsSet(void);
void AD5940_RstSet(void);
void AD5940_RstClr(void);
uint32_t AD5940_MCUResourceInit(void *pCfg);

#endif