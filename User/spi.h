#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <stdbool.h>

void SPI_Master_Transmit(const uint8_t *buf, uint16_t len);
void SPI_Master_Receive(uint8_t *buf, uint16_t len);
void SPI_Master_TransmitReceive(
	const uint8_t *txBuf, uint8_t *rxBuf, uint16_t len);

bool SPI_DMA_TransmitReceive(
	const uint8_t *txBuf, uint8_t *rxBuf, uint16_t len);
bool SPI_DMA_isBusy(void);
void SPI_DMA_waitDone(void);

#endif
