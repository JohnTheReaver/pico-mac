/* sd_spi_pimoroni.c
 *
 * Software (bit-bang) SPI wrapper for the Pimoroni Pico VGA Demo Base.
 *
 * The Pimoroni board's SD card slot is wired to GPIO5 (CLK), GPIO18 (MOSI),
 * GPIO19 (MISO), GPIO22 (CS).  These do NOT map to any valid hardware SPI
 * peripheral assignment on the RP2040, so hardware SPI cannot be used.
 *
 * This file provides __wrap_my_spi_init(), __wrap_spi_transfer(), and
 * __wrap_spi_set_baudrate() which replace the library's hardware-SPI
 * versions via the GCC --wrap linker flag.
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
#include "hardware/spi.h"
#include "pico/time.h"
#include "pico/mutex.h"
#include "pico/sem.h"
//
#include "spi.h"        /* spi_t definition from FatFs_SPI/sd_driver */
#include "my_debug.h"

/* -----------------------------------------------------------------------
 * Baud-rate tracking
 *
 * sd_spi_go_low_frequency() calls spi_set_baudrate(hw_inst, 400 kHz)
 * before the SD card init sequence.  Our bit-bang ignores the hardware SPI
 * baud register, so without intervention it always clocks at ~12 MHz —
 * 30× faster than the 400 kHz limit the SD spec requires during init.
 *
 * We wrap spi_set_baudrate to remember the requested rate.  spi_bb_byte
 * adds half-clock delays whenever the rate is ≤ 400 kHz, giving ~250 kHz
 * during init (safely within spec) and full GPIO speed (~12 MHz) for data.
 * ----------------------------------------------------------------------- */

static volatile uint bb_target_baud = 5 * 1000 * 1000; /* default post-init */

/* Real function provided by the linker via --wrap */
extern uint __real_spi_set_baudrate(spi_inst_t *spi, uint baudrate);

uint __wrap_spi_set_baudrate(spi_inst_t *spi, uint baudrate)
{
    bb_target_baud = baudrate;
    /* Keep the hardware SPI peripheral in sync (used by spi_write_blocking
     * inside sd_spi_select / sd_spi_deselect). */
    return __real_spi_set_baudrate(spi, baudrate);
}

/* -----------------------------------------------------------------------
 * Bit-bang SPI helpers
 * ----------------------------------------------------------------------- */

/* Clock one SPI byte out (MSB first, CPOL=0 CPHA=0) and return received byte.
 * When bb_target_baud <= 400 kHz (SD init), busy_wait_us(2) is inserted at
 * each clock edge to keep the clock at ~250 kHz. */
static inline uint8_t spi_bb_byte(spi_t *spi_p, uint8_t tx_byte)
{
    uint8_t rx_byte = 0;
    bool slow = (bb_target_baud <= 400 * 1000);
    for (int bit = 7; bit >= 0; bit--) {
        /* Set MOSI */
        gpio_put(spi_p->mosi_gpio, (tx_byte >> bit) & 1u);
        /* Rising clock edge — slave samples MOSI */
        gpio_put(spi_p->sck_gpio, 1);
        if (slow) busy_wait_us(2);
        /* Sample MISO */
        rx_byte |= (gpio_get(spi_p->miso_gpio) ? 1u : 0u) << bit;
        /* Falling clock edge */
        gpio_put(spi_p->sck_gpio, 0);
        if (slow) busy_wait_us(2);
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

    /* SD cards' DO must be pulled up — without this, MISO floats at 0
     * and sd_wait_ready spins for the full 2000ms timeout on every command. */
    gpio_pull_up(spi_p->miso_gpio);

    /* Idle states: SCK low, MOSI high (SPI idle line) */
    gpio_put(spi_p->sck_gpio,  0);
    gpio_put(spi_p->mosi_gpio, 1);

    if (spi_p->set_drive_strength) {
        gpio_set_drive_strength(spi_p->mosi_gpio, spi_p->mosi_gpio_drive_strength);
        gpio_set_drive_strength(spi_p->sck_gpio,  spi_p->sck_gpio_drive_strength);
    }

    /* sd_spi_select() and sd_spi_deselect() call spi_write_blocking(hw_inst)
     * directly (not via spi_transfer), so the hardware SPI peripheral must be
     * enabled or those calls hang waiting for a byte to clock out.  The bytes
     * they send are just dummy clocks; no GPIO pin is set to SPI function, so
     * nothing reaches the SD card — but the peripheral completes the transfer
     * internally and spi_write_blocking() returns normally. */
    spi_init(spi_p->hw_inst, 400 * 1000);
    spi_set_format(spi_p->hw_inst, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    spi_p->initialized = true;
    return true;
}

/* Called instead of spi_transfer() in the FatFs library */
bool __not_in_flash_func(__wrap_spi_transfer)(spi_t *spi_p,
                                              const uint8_t *tx,
                                              uint8_t *rx,
                                              size_t length)
{
    static uint8_t dummy_rx;

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
