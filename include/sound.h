#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Initialise PWM audio on GPIO 26/27 and start the sample-rate alarm. */
void    sound_init(void);

/* Called from the umac sound callback when the Mac flips its sound buffer
 * (buf != NULL) or changes the mute state (buf == NULL).
 */
void    sound_buf_changed(const uint8_t *buf, uint8_t vol, int enabled);

#endif
