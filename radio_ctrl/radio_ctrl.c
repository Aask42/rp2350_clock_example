#include "radio_ctrl.h"
#include <fm_rda5807.h>
#include <rds_parser.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define RSSI_THRESHOLD 60 // Minimum RSSI to consider a station valid
#define MAX_STATIONS 10   // Maximum number of stations to store

static const float STATION_PRESETS[] = {
    88.1f, 90.3f, 91.7f, 95.7f, 101.1f, 107.3f,
};
static_assert(COUNT_OF(STATION_PRESETS) <= 9, "");

#define DEFAULT_FREQUENCY STATION_PRESETS[0]
#define FM_CONFIG fm_config_usa()

static rda5807_t radio;
static rds_parser_t rds_parser;

static float station_list[MAX_STATIONS];
static int station_count = 0;

static void print_help(void);
static void update_rds(void);
static void scan_stations(void);
static void print_station_info(void);
static void print_rds_info(void);
static void set_frequency(float frequency);
static void seek(fm_seek_direction_t direction);

/* ----------------------------------------------------------------------- */
/*                     **PUBLIC-API IMPLEMENTATION**                        */
/* ----------------------------------------------------------------------- */

bool radio_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin,
                float initial_freq, uint start_volume)
{
    print_help();

    printf("Configuring I2C...\n");
    i2c_init(i2c, 400 * 1000);

    fm_init(&radio, i2c, sda_pin, scl_pin, true);
    sleep_ms(500);

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

    return true;
}

void radio_deinit(void)
{
    if (fm_is_powered_up(&radio))
        fm_power_down(&radio);
}

void radio_feed_char(int ch)
{
    if (ch == PICO_ERROR_TIMEOUT)
        return;

    if (fm_is_powered_up(&radio)) {
        switch (ch) {
            case '-':
                if (fm_get_volume(&radio) > 0) {
                    fm_set_volume(&radio, fm_get_volume(&radio) - 1);
                    printf("Set volume: %u\n", fm_get_volume(&radio));
                }
                break;
            case '=':
                if (fm_get_volume(&radio) < 30) {
                    fm_set_volume(&radio, fm_get_volume(&radio) + 1);
                    printf("Set volume: %u\n", fm_get_volume(&radio));
                }
                break;
            case 's':
                printf("Scanning for stations...\n");
                scan_stations();
                break;
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                if (ch - '1' < station_count) {
                    float frequency = station_list[ch - '1'];
                    set_frequency(frequency);
                }
                break;
            case '{': {
                fm_frequency_range_t range = fm_get_frequency_range(&radio);
                float frequency = fm_get_frequency(&radio) - range.spacing;
                if (frequency < range.bottom) {
                    frequency = range.top; // wrap to top
                }
                set_frequency(frequency);
                break;
            }
            case '}': {
                fm_frequency_range_t range = fm_get_frequency_range(&radio);
                float frequency = fm_get_frequency(&radio) + range.spacing;
                if (range.top < frequency) {
                    frequency = range.bottom; // wrap to bottom
                }
                set_frequency(frequency);
                printf("Signal Strength (RSSI): %u\n", fm_get_rssi(&radio));
                break;
            }
            case '[':
                seek(FM_SEEK_DOWN);
                break;
            case ']':
                seek(FM_SEEK_UP);
                break;
            case '<':
                if (fm_get_seek_threshold(&radio) > 0) {
                    fm_set_seek_threshold(&radio, fm_get_seek_threshold(&radio) - 1);
                    printf("Set seek threshold: %u\n", fm_get_seek_threshold(&radio));
                }
                break;
            case '>':
                if (fm_get_seek_threshold(&radio) < FM_MAX_SEEK_THRESHOLD) {
                    fm_set_seek_threshold(&radio, fm_get_seek_threshold(&radio) + 1);
                    printf("Set seek threshold: %u\n", fm_get_seek_threshold(&radio));
                }
                break;
            case '0':
                fm_set_mute(&radio, !fm_get_mute(&radio));
                printf("Set mute: %u\n", fm_get_mute(&radio));
                break;
            case 'f':
                fm_set_softmute(&radio, !fm_get_softmute(&radio));
                printf("Set softmute: %u\n", fm_get_softmute(&radio));
                break;
            case 'm':
                fm_set_mono(&radio, !fm_get_mono(&radio));
                printf("Set mono: %u\n", fm_get_mono(&radio));
                break;
            case 'b':
                fm_set_bass_boost(&radio, !fm_get_bass_boost(&radio));
                printf("Set bass boost: %u\n", fm_get_bass_boost(&radio));
                break;
            case 'i':
                print_station_info();
                break;
            case 'r':
                print_rds_info();
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

void radio_poll(void)
{
    if (fm_is_powered_up(&radio))
        update_rds();
    sleep_ms(40);
}

float radio_get_frequency(void) { return fm_get_frequency(&radio); }
uint8_t radio_get_rssi(void) { return fm_get_rssi(&radio); }

/* ----------------------------------------------------------------------- */
/*                  **PRIVATE FUNCTIONS BELOW**                            */
/* ----------------------------------------------------------------------- */

static void update_rds(void)
{
    union {
        uint16_t group_data[4];
        rds_group_t group;
    } rds;

    if (fm_read_rds_group(&radio, rds.group_data)) {
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
    printf("{ }   Frequency down / up\n");
    printf("[ ]   Seek down / up\n");
    printf("< >   Adjust seek threshold\n");
    printf("0     Toggle mute\n");
    printf("f     Toggle softmute\n");
    printf("m     Toggle mono\n");
    printf("b     Toggle bass boost\n");
    printf("i     Print station info\n");
    printf("r     Print RDS info\n");
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
        sleep_ms(500); // Allow time for the tuner to stabilize

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

static void print_station_info(void)
{
    printf("Current Frequency: %.2f MHz\n", fm_get_frequency(&radio));
    printf("Signal Strength (RSSI): %u\n", fm_get_rssi(&radio));
}

// Add these helper functions
static void mjd_to_date(uint32_t mjd, uint16_t *year, uint8_t *month, uint8_t *day) {
    uint32_t j = mjd + 2400001;
    uint32_t y = (uint32_t)((j - 1867216.25)/36524.25);
    uint32_t c = j + y - (y/4) + 1525;
    y = (uint32_t)((c - 122.1)/365.25);
    uint32_t d = (uint32_t)(365.25 * y);
    uint32_t m = (uint32_t)((c - d)/30.6001);
    
    *day = c - d - (uint32_t)(30.6001 * m);
    *month = m - 1;
    if (m > 13) *month = m - 13;
    *year = y - 4715;
    if (*month > 2) *year -= 1;
}


static void print_rds_info(void)
{
    printf("RDS Info:\n");
    char program_id_str[5];
    rds_get_program_id_as_str(&rds_parser, program_id_str);
    printf("RDS - PI: %s, PTY: %u, DI_PTY: %u, DI_ST: %u, MS: %u, TP: %u, TA: %u\n",
        program_id_str,
        rds_get_program_type(&rds_parser),
        rds_has_dynamic_program_type(&rds_parser),
        rds_has_stereo(&rds_parser),
        rds_has_music(&rds_parser),
        rds_has_traffic_program(&rds_parser),
        rds_has_traffic_announcement(&rds_parser));
    printf("      PS: %s\n", rds_get_program_service_name_str(&rds_parser));
    
    // Add clock time and date display
    uint8_t hour, minute;
    if (rds_get_time(&rds_parser, &hour, &minute)) {
        uint16_t year;
        uint8_t month, day;
        mjd_to_date(rds_parser.time_info.mjd, &year, &month, &day);
        printf("   Date: %02u/%02u/%04u\n", day, month, year);
        printf("   Time: %02u:%02u\n", hour, minute);
    } else {
        printf("   Date/Time: Not available\n");
    }
}
static void set_frequency(float frequency)
{
    fm_set_frequency_blocking(&radio, frequency);
    printf("Set frequency: %.2f MHz\n", fm_get_frequency(&radio));
    rds_parser_reset(&rds_parser);
}

static void seek(fm_seek_direction_t direction)
{
    fm_seek_async(&radio, direction);

    puts("Seeking...");
    fm_async_progress_t progress;
    do {
        sleep_ms(100);
        progress = fm_async_task_tick(&radio);
        printf("... %.2f MHz\n", fm_get_frequency(&radio));
    } while (!progress.done);

    if (progress.result == 0) {
        puts("... finished");
    } else {
        printf("... failed: %d\n", progress.result);
    }
    rds_parser_reset(&rds_parser);
}