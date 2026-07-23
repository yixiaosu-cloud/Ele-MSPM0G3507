#ifndef ADC_H
#define ADC_H

#include <stdint.h>

uint16_t ADC_Read_mV(void);
uint16_t ADC_ReadRaw(void);
void ADC_Demo(void);

#endif
