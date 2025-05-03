#include "radio_ctrl.h"
#include <fm_rda5807.h>
#include <rds_parser.h>
#include <stdio.h>
#include <assert.h> // For static_assert
#include <stdint.h> // For uint8_t

/* ----------------------------------------------------------------------- */
/*            **PRIVATE, unchanged pieces of the original demo**           */
/* ----------------------------------------------------------------------- */

#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define RSSI_THRESHOLD 20 // Minimum RSSI to consider a station valid
#define MAX_STATIONS 20   // Maximum number of stations to store

static const float STATION_PRESETS[] = {
    88.1f, 90.3f, 91.7f, 95.7f, 101.1f, 107.3f,
};
static_assert(COUNT_OF(STATION_PRESETS) <= 9, "");

#define DEFAULT_FREQUENCY STATION_PRESETS[0]
#define FM_CONFIG fm_config_usa()

static rda5807_t   radio;
static rds_parser_t rds_parser;

static float station_list[MAX_STATIONS];
static int station_count = 0;

static void print_help(void);         /* Forward declaration */
static void update_rds(void);         /* Forward declaration */
static void scan_stations(void);

/* ----------------------------------------------------------------------- */
/*                     **PUBLIC-API IMPLEMENTATION**                        */
/* ----------------------------------------------------------------------- */

bool radio_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin,
    float initial_freq, uint start_volume)
{
    print_help();

    /* RDA5807 tolerates up to 400 kHz I²C clock. Configure only once. */
    printf("Configuring I2C...\n");
    i2c_init(i2c, 400 * 1000);

    fm_init(&radio, i2c, sda_pin, scl_pin, true);
    sleep_ms(500);

    /* Call fm_power_up without checking its return value */
    fm_power_up(&radio, FM_CONFIG);
    if (!fm_is_powered_up(&radio)) {
        printf("Error: Radio failed to power up.\n");
        return false;
    }
    printf("Radio powered up successfully.\n");

    fm_set_frequency_blocking(&radio, initial_freq);
    fm_set_volume(&radio, MIN(start_volume, 30));
    fm_set_mute(&radio, false);

    rds_parser_reset(&rds_parser);
    printf("Radio Running Successfully!\n");

    return true; // Assume success if no errors occur
}

void radio_deinit(void)
{
    if (fm_is_powered_up(&radio))
        fm_power_down(&radio);
}

/* feed_char() – exactly the old switch-case, just made public -------------*/
void radio_feed_char(int ch)
{
    if (ch == PICO_ERROR_TIMEOUT)
        return;

    if (fm_is_powered_up(&radio)) {
        switch (ch) {
            case '-':
                if (fm_get_volume(&radio) > 0) {
                    fm_set_volume(&radio, fm_get_volume(&radio) - 1);
                    printf("Volume: %u\n", fm_get_volume(&radio));
                }
                break;
            case '=':
                if (fm_get_volume(&radio) < 30) {
                    fm_set_volume(&radio, fm_get_volume(&radio) + 1);
                    printf("Volume: %u\n", fm_get_volume(&radio));
                }
                break;
            case 's':
                printf("Scanning for stations...\n");
                scan_stations();
                break;
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                if (ch - '1' < station_count) {
                    float frequency = station_list[ch - '1'];
                    fm_set_frequency_blocking(&radio, frequency);
                    printf("Tuned to %.2f MHz\n", frequency);
                }
                break;
            case 'm':
                fm_set_mono(&radio, !fm_get_mono(&radio));
                printf("Mono: %u\n", fm_get_mono(&radio));
                break;
            case 'b':
                fm_set_bass_boost(&radio, !fm_get_bass_boost(&radio));
                printf("Bass Boost: %u\n", fm_get_bass_boost(&radio));
                break;
            case 'x':
                fm_power_down(&radio);
                printf("Radio powered down.\n");
                break;
            case '?':
                print_help();
                break;
            default:
                printf("Unknown command: %c\n", ch);
                break;
        }
    } else {
        if (ch == 'x') {
            fm_power_up(&radio, FM_CONFIG);
            printf("Radio powered up.\n");
        }
    }
}

/* radio_poll() – called from main loop to do background work -------------*/
void radio_poll(void)
{
    if (fm_is_powered_up(&radio))
        update_rds();
    sleep_ms(40);          /* same cadence as the original example */
}

/* Tiny helpers -----------------------------------------------------------*/
float   radio_get_frequency(void) { return fm_get_frequency(&radio); }
uint8_t radio_get_rssi(void)      { return fm_get_rssi(&radio);      }

/* ----------------------------------------------------------------------- */
/*                  **unchanged private functions below**                  */
/* ----------------------------------------------------------------------- */
static void update_rds(void)
{
    // Example RDS update logic
    union {
        uint16_t group_data[4];
        rds_group_t group;
    } rds;

    if (fm_read_rds_group(&radio, rds.group_data)) {
        //printf("RDS Frame: 0x%04X 0x%04X 0x%04X 0x%04X\n",
        //    rds.group_data[0], rds.group_data[1],
        //    rds.group_data[2], rds.group_data[3]);

        rds_parser_update(&rds_parser, &rds.group);
    }
}

static void print_help(void)
{
    printf("Radio Control Menu:\n");
    printf("====================\n");
    printf("- =   Volume down / up\n");
    printf("s     Scan for stations\n");
    printf("1-9   Tune to scanned station\n");
    printf("m     Toggle mono\n");
    printf("b     Toggle bass boost\n");
    printf("x     Power down / up\n");
    printf("?     Print this help menu\n");
}

static void scan_stations(void)
{
    fm_frequency_range_t range = fm_get_frequency_range(&radio);
    float frequency = range.bottom;
    station_count = 0;

    while (frequency <= range.top && station_count < MAX_STATIONS) {
        fm_set_frequency_blocking(&radio, frequency);
        sleep_ms(1000); // Allow time for the tuner to stabilize

        uint8_t rssi = fm_get_rssi(&radio);
        printf("Frequency: %.2f MHz, RSSI: %u\n", frequency, rssi);

        if (rssi >= RSSI_THRESHOLD) {
            station_list[station_count++] = frequency;
            printf("  -> Added to station list\n");
        }

        frequency += range.spacing;
    }

    printf("Scan complete. Found %d stations.\n", station_count);
    for (int i = 0; i < station_count; i++) {
        printf("%d: %.2f MHz\n", i + 1, station_list[i]);
    }
}
