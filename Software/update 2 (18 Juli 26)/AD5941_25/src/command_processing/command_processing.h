#ifndef COMMAND_PROCESSING_H
#define COMMAND_PROCESSING_H

#include <Arduino.h>

bool ProcessCommand2Int(const char command, uint32_t param1, uint32_t param2);
bool ProcessCommand2Float(const char command, float param1, float param2);
bool ProcessCommand1Int(const char command, uint16_t param);
bool ProcessCommand1Float(const char command, float param);
bool ProcessCommand(const char command);
void ProcessToken(char* token);
void SplitAndProcessCommands(char* buf);
void ShowParameters();
void ShowParameter(const char* name, int value, bool verb);
void ShowParameter8(const char* name, uint8_t value, bool verb);
void ShowParameterF(const char* format, float value, bool verb);
void ShowParameter2(const char* format, int value1, float value2, bool verb);
void ShowAction(const char* name, bool verb);

#endif
