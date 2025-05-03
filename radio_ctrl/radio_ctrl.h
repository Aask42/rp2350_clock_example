#ifndef RADIO_CTRL_H
#define RADIO_CTRL_H

#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <stdbool.h>

/* ---- one-time set-up / tear-down --------------------------------------- */
bool  radio_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin,
                 float initial_freq, uint start_volume);
void  radio_deinit(void);   /* optional */

/* ---- things you may want to call from the rest of the firmware --------- */
void  radio_poll(void);                 /* call every 40 ms (or slower)     */
void  radio_feed_char(int ch);          /* raw keystrokes, e.g. from UART   */

/* convenience helpers if you need them elsewhere in your code */
float radio_get_frequency(void);
uint8_t radio_get_rssi(void);

#endif /* RADIO_CTRL_H */
