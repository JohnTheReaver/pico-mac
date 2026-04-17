/* sd_spi_pimoroni.c
 *
 * Software (bit-bang) SPI wrapper for the Pimoroni Pico VGA Demo Base.
 *
 * The Pimoroni board's SD card slot is wired to GPIO5 (CLK), GPIO18 (MOSI),
 * GPIO19 (MISO), GPIO22 (CS).  These do NOT map to any valid hardware SPI
 * peripheral assignment on the RP2040, so hardware SPI cannot be used.
 *
 * This file provides __wrap_my_spi_init() and __wrap_spi_transfer() which
 * replace the library's hardware-SPI versions via the GCC --wrap linker flag.
 * All other FatFs/SD library code is unchanged.
 *
 * MIT License
 * Copyright (c) 2024 pico-mac Pimoroni port
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
//
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/mutex.h"
#include "pico/sem.h"
//
#include "spi.h"        /* spi_t definition from FatFs_SPI/sd_driver */
#include "my_debug.h"

/* -----------------------------------------------------------------------
 * Bit-bang SPI helpers
 * ----------------------------------------------------------------------- */

/* Clock one SPI byte out (MSB first, CPOL=0 CPHA=0) and return received byte */
static inline uint8_t spi_bb_byte(spi_t *spi_p, uint8_t tx_byte)
{
    uint8_t rx_byte = 0;
    for (int bit = 7; bit >= 0; bit--) {
        /* Set MOSI */
        gpio_put(spi_p->mosi_gpio, (tx_byte >> bit) & 1u);
        /* Rising clock edge — slave samples MOSI */
        gpio_put(spi_p->sck_gpio, 1);
        /* Sample MISO */
        rx_byte |= (gpio_get(spi_p->miso_gpio) ? 1u : 0u) << bit;
        /* Falling clock edge */
        gpio_put(spi_p->sck_gpio, 0);
    }
    return rx_byte;
}

/* -----------------------------------------------------------------------
 * Wrapped replacements for the hardware-SPI functions in spi.c
 * ----------------------------------------------------------------------- */

/* Called instead of my_spi_init() in the FatFs library */
bool __wrap_my_spi_init(spi_t *spi_p)
{
    /* Initialise mutex/semaphore expected by the rest of the library */
    if (!mutex_is_initialized(&spi_p->mutex))
        mutex_init(&spi_p->mutex);
    sem_init(&spi_p->sem, 0, 1);

    /* Configure GPIO pins for bit-bang SPI */
    gpio_init(spi_p->sck_gpio);
    gpio_init(spi_p->mosi_gpio);
    gpio_init(spi_p->miso_gpio);

    gpio_set_dir(spi_p->sck_gpio,  GPIO_OUT);
    gpio_set_dir(spi_p->mosi_gpio, GPIO_OUT);
    gpio_set_dir(spi_p->miso_gpio, GPIO_IN);

    /* Idle states: SCK low, MOSI high (SPI idle line) */
    gpio_put(spi_p->sck_gpio,  0);
    gpio_put(spi_p->mosi_gpio, 1);

    if (spi_p->set_drive_strength) {
        gpio_set_drive_strength(spi_p->mosi_gpio, spi_p->mosi_gpio_drive_strength);
        gpio_set_drive_strength(spi_p->sck_gpio,  spi_p->sck_gpio_drive_strength);
    }

    spi_p->initialized = true;
    return true;
}

/* Called instead of spi_transfer() in the FatFs library */
bool __not_in_flash_func(__wrap_spi_transfer)(spi_t *spi_p,
                                              const uint8_t *tx,
                                              uint8_t *rx,
                                              size_t length)
{
    static const uint8_t dummy_tx = 0xFF;
    static       uint8_t dummy_rx;

    for (size_t i = 0; i < length; i++) {
        uint8_t tx_byte = tx ? tx[i] : 0xFF;
        uint8_t rx_byte = spi_bb_byte(spi_p, tx_byte);
        if (rx)
            rx[i] = rx_byte;
        else
            dummy_rx = rx_byte; /* discard */
    }
    return true;
}

/* spi_lock / spi_unlock are used by the SD card driver and remain as thin
 * mutex wrappers; the originals already work without hardware SPI, so we
 * do NOT wrap them. */
