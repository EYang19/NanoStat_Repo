/*
 * AD5941 MCU Porting Layer for Zephyr RTOS (nRF52840)
 * Bridges the Analog Devices ad5940lib to Zephyr drivers.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "ad5940.h"

/* * ==========================================
 * 1. Hardware node bindings shared with main.c
 * ==========================================
 */
#define SPI1_NODE DT_NODELABEL(ad5941_spi)

/* Mode 0 (CPOL=0, CPHA=0) is required by the AD5941 interface. */
static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI1_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0); 
static const struct gpio_dt_spec cs_pin = GPIO_DT_SPEC_GET(DT_PATH(ad5941_config, ad5941_cs), gpios);
static const struct gpio_dt_spec rst_pin = GPIO_DT_SPEC_GET(DT_PATH(ad5941_config, ad5941_reset), gpios);

/* * ==========================================
 * 2. Low-level interfaces required by the ADI library
 * ==========================================
 */

/**
 * @brief Check and initialise MCU-side hardware resources.
 */
uint32_t AD5940_MCUResourceInit(void *pCfg)
{
    if (!spi_is_ready_dt(&spi_dev)) {
        printk("[AD5941 Port] ERROR: SPI device not ready!\n");
        return 1; // Return error
    }
    if (!gpio_is_ready_dt(&cs_pin) || !gpio_is_ready_dt(&rst_pin)) {
        printk("[AD5941 Port] ERROR: GPIO devices not ready!\n");
        return 1; 
    }
    
    /* Leave chip select inactive and release reset by default. */
    gpio_pin_configure_dt(&cs_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&rst_pin, GPIO_OUTPUT_INACTIVE);
    
    return 0; // Success
}

/**
 * @brief Assert chip select.
 */
void AD5940_CsClr(void)
{
    gpio_pin_set_dt(&cs_pin, 1);
}

/**
 * @brief Deassert chip select.
 */
void AD5940_CsSet(void)
{
    gpio_pin_set_dt(&cs_pin, 0);
}

/**
 * @brief Release reset.
 */
void AD5940_RstSet(void)
{
    gpio_pin_set_dt(&rst_pin, 0);
}

/**
 * @brief Assert reset.
 */
void AD5940_RstClr(void)
{
    gpio_pin_set_dt(&rst_pin, 1);
}

/**
 * @brief Delay in units of 10 microseconds.
 * @param time Number of 10 microsecond intervals.
 */
void AD5940_Delay10us(uint32_t time)
{
    if (time == 0) return;
    
    /* Busy-wait because the ADI interface requires a short synchronous delay. */
    k_busy_wait(time * 10);
}

/**
 * @brief Low-level SPI transfer function.
 * @note The ADI library calls AD5940_CsClr before this function and
 * AD5940_CsSet afterwards, so chip select must not be changed here.
 */
void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuffer, unsigned long length)
{
    int ret;
    
    /* Zephyr transmit buffer. */
    struct spi_buf tx_buf = {
        .buf = pSendBuffer,
        .len = length
    };
    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1
    };

    /* Zephyr receive buffer. */
    struct spi_buf rx_buf = {
        .buf = pRecvBuffer,
        .len = length
    };
    struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1
    };

    /* Transmit only. */
    if (pRecvBuffer == NULL) {
        ret = spi_write_dt(&spi_dev, &tx_set);
    } 
    /* Receive only. */
    else if (pSendBuffer == NULL) {
        ret = spi_read_dt(&spi_dev, &rx_set);
    } 
    /* Full-duplex transfer. */
    else {
        ret = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
    }

    if (ret < 0) {
        printk("[AD5941 Port] ERROR: SPI Transfer failed: %d\n", ret);
    }
}

/* Optional interrupt-flag hooks required by the library interface. */
uint32_t AD5940_GetMCUIntFlag(void)
{
    return 0; 
}

uint32_t AD5940_ClrMCUIntFlag(void)
{
    return 0;
}

void AD5940_MCUResourceDeInit(void)
{
    gpio_pin_configure_dt(&cs_pin, GPIO_DISCONNECTED);
    gpio_pin_configure_dt(&rst_pin, GPIO_DISCONNECTED);
}
