#include "spi.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/dl_dma.h>

static volatile bool g_spi_dma_tx_done = true;
static volatile bool g_spi_dma_rx_done = true;
static volatile bool g_spi_tx_empty    = true;

void SPI_Master_Transmit(const uint8_t *buf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		DL_SPI_transmitDataBlocking8(SPI_0_INST, buf[i]);
		(void)DL_SPI_receiveDataBlocking8(SPI_0_INST);
	}
	while (DL_SPI_isBusy(SPI_0_INST)) {
		__WFE();
	}
}

void SPI_Master_Receive(uint8_t *buf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		DL_SPI_transmitDataBlocking8(SPI_0_INST, 0x00);
		buf[i] = (uint8_t)DL_SPI_receiveDataBlocking8(SPI_0_INST);
	}
}

void SPI_Master_TransmitReceive(
	const uint8_t *txBuf, uint8_t *rxBuf, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++) {
		DL_SPI_transmitDataBlocking8(SPI_0_INST, txBuf[i]);
		rxBuf[i] = (uint8_t)DL_SPI_receiveDataBlocking8(SPI_0_INST);
	}
}

bool SPI_DMA_TransmitReceive(
	const uint8_t *txBuf, uint8_t *rxBuf, uint16_t len)
{
	if (g_spi_dma_tx_done == false
		|| g_spi_dma_rx_done == false
		|| g_spi_tx_empty == false) {
		return false;
	}
	g_spi_dma_tx_done = false;
	g_spi_dma_rx_done = false;
	g_spi_tx_empty    = false;

	DL_DMA_setSrcAddr(DMA, DMA_CH2_CHAN_ID, (uint32_t)txBuf);
	DL_DMA_setDestAddr(DMA, DMA_CH2_CHAN_ID,
		(uint32_t)(&SPI_0_INST->TXDATA));
	DL_DMA_setTransferSize(DMA, DMA_CH2_CHAN_ID, len);

	DL_DMA_setSrcAddr(DMA, DMA_CH3_CHAN_ID,
		(uint32_t)(&SPI_0_INST->RXDATA));
	DL_DMA_setDestAddr(DMA, DMA_CH3_CHAN_ID, (uint32_t)rxBuf);
	DL_DMA_setTransferSize(DMA, DMA_CH3_CHAN_ID, len);

	DL_DMA_enableChannel(DMA, DMA_CH3_CHAN_ID);
	DL_DMA_enableChannel(DMA, DMA_CH2_CHAN_ID);
	return true;
}

bool SPI_DMA_isBusy(void)
{
	return (g_spi_dma_tx_done == false
		|| g_spi_dma_rx_done == false
		|| g_spi_tx_empty == false);
}

void SPI_DMA_waitDone(void)
{
	while (SPI_DMA_isBusy()) {
		__WFE();
	}
}

void SPI_0_INST_IRQHandler(void)
{
	switch (DL_SPI_getPendingInterrupt(SPI_0_INST)) {
	case DL_SPI_IIDX_DMA_DONE_TX:
		g_spi_dma_tx_done = true;
		break;
	case DL_SPI_IIDX_DMA_DONE_RX:
		g_spi_dma_rx_done = true;
		break;
	case DL_SPI_IIDX_TX_EMPTY:
		g_spi_tx_empty = true;
		break;
	default:
		break;
	}
}
