#ifndef BTN9990_DRIVER_H
#define BTN9990_DRIVER_H

#include <stdint.h>

bool btn9990Begin();
bool btn9990Enable();
void btn9990Disable();
bool btn9990SetBrake();
bool btn9990SetBrakeDuty(uint8_t brakePercent);
void btn9990SetSupplyAvailable(bool available, uint8_t brakePercent);
bool btn9990SetOutput(uint8_t throttlePercent, uint8_t dragPercent);
bool btn9990IsReady();
bool btn9990IsOutputEnabled();
// Both switches off for a short measurement; resume restores the last duties.
void btn9990PauseOutput();
void btn9990ResumeOutput();

#endif