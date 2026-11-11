#ifndef BTN9990_DRIVER_H
#define BTN9990_DRIVER_H

#include <stdint.h>

bool btn9990Begin();
bool btn9990Enable();
void btn9990Disable();
bool btn9990SetBrakeDuty(uint8_t brakePercent);
void btn9990SetSupplyAvailable(bool available, uint8_t brakePercent);
// Duties in 0.1 % (0 ... 1000); IN = throttle, INH = throttle + drag.
bool btn9990SetOutput(uint16_t throttlePermille, uint16_t dragPermille);
bool btn9990IsReady();
bool btn9990IsOutputEnabled();
// Applied on the next drive/brake command; 200 Hz ... 50 kHz.
void btn9990SetFrequencies(uint32_t driveHz, uint32_t brakeHz);
// Both switches off for a short measurement; resume restores the last duties.
void btn9990PauseOutput();
void btn9990ResumeOutput();

#endif