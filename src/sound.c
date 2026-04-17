/* sound.c
 *
 * Mac 128K audio output via RP2040 PWM.
 *
 * The Mac generates sound from two 370-byte ping-pong buffers in RAM,
 * switching buffers at ~60 Hz.  Each byte is an 8-bit sample at ~22,254 Hz
 * (one sample per video scanline).  Volume is a 3-bit value (0–7) from
 * VIA Port A.
 *
 * Output: GPIO 26 (PWM5A, left) and GPIO 27 (PWM5B, right) — both carry
 * the same mono signal.  On the Pimoroni Pico VGA Demo Base these GPIOs are
 * wired through RC low-pass filters directly to the 3.5 mm headphone jack.
 *
 * MIT License — pico-mac Pimoroni port
 */

#include <stdint.h>
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "sound.h"

#define SOUND_SAMPLE_RATE   22254u          /* Hz */
#define SOUND_BUF_SIZE      370u            /* bytes per buffer */
#define GPIO_SND_L          26u             /* PWM5A — left  channel */
#define GPIO_SND_R          27u             /* PWM5B — right channel */
#define PWM_WRAP            255u            /* 8-bit resolution */
/* Clock divider: 250 MHz / (44 × 256) = 22,230 Hz ≈ 22,254 Hz (0.1% off) */
#define PWM_CLK_DIV         44u

static const uint8_t    *sound_buf     = NULL;
static volatile uint8_t  sound_vol     = 7;
static volatile int      sound_enabled = 0;
static volatile unsigned int sound_pos = 0;
static uint              pwm_slice;

/* Called by the hardware alarm at ~22,254 Hz to advance one sample. */
static int64_t __not_in_flash_func(sound_alarm_cb)(alarm_id_t id, void *user_data)
{
        uint16_t level;

        if (sound_enabled && sound_buf) {
                uint8_t raw = sound_buf[sound_pos];
                /* Scale by volume: 0 = mute, 7 = full.  Add 1 so vol=7 → 8/8. */
                level = (uint16_t)raw * (sound_vol + 1u) / 8u;
                if (++sound_pos >= SOUND_BUF_SIZE)
                        sound_pos = 0;
        } else {
                level = PWM_WRAP / 2;   /* mid-rail = silence */
        }

        pwm_set_both_levels(pwm_slice, level, level);

        /* Negative return = reschedule relative to when this alarm fired
         * (avoids drift from scheduling latency). */
        return -(int64_t)(1000000u / SOUND_SAMPLE_RATE);
}

/*
 * Called from the umac sound callback (set via umac_set_sound_cb) when:
 *   buf != NULL : Mac has switched to a new 370-byte buffer; start playing it.
 *   buf == NULL : mute state changed only (sndres bit in VIA Port B).
 */
void sound_buf_changed(const uint8_t *buf, uint8_t vol, int enabled)
{
        if (buf) {
                sound_buf = buf;
                sound_vol = vol;
                sound_pos = 0;
        }
        sound_enabled = enabled;
}

void sound_init(void)
{
        gpio_set_function(GPIO_SND_L, GPIO_FUNC_PWM);
        gpio_set_function(GPIO_SND_R, GPIO_FUNC_PWM);

        /* Both GPIOs share PWM slice 5 (L = channel A, R = channel B). */
        pwm_slice = pwm_gpio_to_slice_num(GPIO_SND_L);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_int(&cfg, PWM_CLK_DIV);
        pwm_config_set_wrap(&cfg, PWM_WRAP);
        pwm_init(pwm_slice, &cfg, true);

        /* Start at mid-rail (silence). */
        pwm_set_both_levels(pwm_slice, PWM_WRAP / 2, PWM_WRAP / 2);

        /* Start the sample-rate timer alarm. */
        add_alarm_in_us(1000000u / SOUND_SAMPLE_RATE, sound_alarm_cb, NULL, true);
}
