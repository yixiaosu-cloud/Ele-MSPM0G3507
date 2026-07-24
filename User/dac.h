#ifndef DAC_H
#define DAC_H

#include <stdint.h>

void DAC_SetVoltage(uint32_t mV);
void DAC_SetCode(uint16_t code);
void DAC_Disable(void);
void DAC_SetFrequency(float freq_hz);
void DAC_WaveConfig(float offset_mV, float amplitude_mV);
void DAC_Demo(void);

#endif
