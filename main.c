#include "radio_ctrl.h"
#include "LCD_Test.h"
#include <pico/stdlib.h>
#include <stdio.h>

int main(void) {
    stdio_init_all();

    /* Pins match the old demo; change if you wired differently */
    const uint SDIO_PIN = 20, SCLK_PIN = 21;

    if (!radio_init(i2c_default, SDIO_PIN, SCLK_PIN,
                    88.1f /*start station*/, 1 /*volume*/)) {
        printf("Radio initialization failed!\n");
        while (true) tight_loop_contents();
    }

    lcd_clock_init();

    uint64_t last_lcd_update = time_us_64();
    const uint64_t lcd_update_interval = 1000 * 1000; // 1 second

    while (true) {
        /* Pump user input into the radio module */
        radio_feed_char(getchar_timeout_us(0));

        /* Let the radio module run its housekeeping */
        radio_poll();

        /* Update the LCD clock every second */
        uint64_t now = time_us_64();
        if (now - last_lcd_update >= lcd_update_interval) {
            lcd_clock_update();
            last_lcd_update = now;
        }
    }

    lcd_clock_deinit();
    return 0;
}