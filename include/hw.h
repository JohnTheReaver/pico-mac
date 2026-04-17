/*
 * pico-umac pin definitions
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HW_H
#define HW_H

#define GPIO_LED_PIN    PICO_DEFAULT_LED_PIN

/* Video pin assignment.
 *
 * Pins can be configured independently via CMake variables:
 *   VIDEO_DATA_PIN, VIDEO_VS_PIN, VIDEO_CLK_PIN, VIDEO_HS_PIN
 *
 * If VIDEO_PIN is set (legacy/bare-Pico mode), all four pins are derived
 * consecutively from that base: DATA=BASE, VS=BASE+1, CLK=BASE+2, HS=BASE+3.
 *
 * Pimoroni Pico VGA Demo Base defaults (set in CMakeLists.txt):
 *   DATA=4 (Red DAC MSB), VS=17, CLK=15 (suppressed), HS=16
 * NOTE: CLK pin output is suppressed via GPIO_OVERRIDE_LOW in video.c so
 * the pixel clock does not appear as noise on the VGA Blue channel.
 *
 * CONSTRAINT: CLK and HS must be adjacent (CLK = HS - 1) because they share
 * a 2-bit PIO sideset.
 */
#define GPIO_VID_DATA   GPIO_VID_DATA_PIN
#define GPIO_VID_VS     GPIO_VID_VS_PIN
#define GPIO_VID_CLK    GPIO_VID_CLK_PIN
#define GPIO_VID_HS     GPIO_VID_HS_PIN

/* First of 5 consecutive GPIOs driven by the echo SM for white monochrome.
 * GPIO 10 = Green MSB (G4) on the Pimoroni VGA Demo Base; GPIOs 11-14 = B0-B3.
 * GPIO 15 (Blue MSB) is reserved as the suppressed pixel-clock sideset pin.
 */
#define GPIO_VID_GREEN_ECHO_BASE  10

#endif
