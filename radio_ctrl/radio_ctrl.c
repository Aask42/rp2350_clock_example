#include "radio_ctrl.h"
#include <fm_rda5807.h>
#include <librdsparser.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>

#define COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


// define ANSI colours
#define RED "\033[1;31m"
#define WHITE "\033[37m"
#define YELLOW "\033[93m"
#define BOLD "\033[1m"
#define ENDC "\033[m"


#define RSSI_THRESHOLD (60) // Minimum RSSI to consider a station valid
#define MAX_STATIONS (10)   // Maximum number of stations to store

static const float STATION_PRESETS[] = {
    88.1f, 90.3f, 91.7f, 95.7f, 101.1f, 107.3f,
};

static_assert(COUNT_OF(STATION_PRESETS) <= 9, "");

#define DEFAULT_FREQUENCY STATION_PRESETS[0]
#define FM_CONFIG fm_config_usa()

static rda5807_t radio;
static rdsparser_t *rds_parser;

// Store CT data from callback
static struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    bool valid;
} ct_data = {0};

static float station_list[MAX_STATIONS];
static int station_count = 0;

// RDS callback function prototypes
static void callback_ps(rdsparser_t *rds, void *user_data);
static void callback_rt(rdsparser_t *rds, rdsparser_rt_flag_t flag, void *user_data);
static void callback_ct(rdsparser_t *rds, const rdsparser_ct_t *ct, void *user_data);

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

    // Initialize RDS parser
#ifdef RDSPARSER_DISABLE_HEAP
    rdsparser_t buffer;
    rdsparser_init(&buffer);
    rds_parser = &buffer;
#else
    rds_parser = rdsparser_new();
    if (rds_parser == NULL) {
        printf("Error: Failed to create RDS parser.\n");
        return false;
    }
#endif

    // Configure RDS parser
    rdsparser_set_text_correction(rds_parser, RDSPARSER_TEXT_PS, RDSPARSER_BLOCK_TYPE_INFO, RDSPARSER_BLOCK_ERROR_LARGE);
    rdsparser_set_text_correction(rds_parser, RDSPARSER_TEXT_PS, RDSPARSER_BLOCK_TYPE_DATA, RDSPARSER_BLOCK_ERROR_LARGE);
    rdsparser_set_text_correction(rds_parser, RDSPARSER_TEXT_RT, RDSPARSER_BLOCK_TYPE_INFO, RDSPARSER_BLOCK_ERROR_LARGE);
    rdsparser_set_text_correction(rds_parser, RDSPARSER_TEXT_RT, RDSPARSER_BLOCK_TYPE_DATA, RDSPARSER_BLOCK_ERROR_LARGE);
    
    rdsparser_set_text_progressive(rds_parser, RDSPARSER_TEXT_PS, true);
    rdsparser_set_text_progressive(rds_parser, RDSPARSER_TEXT_RT, true);
    
    // Register callbacks
    rdsparser_register_ps(rds_parser, callback_ps);
    rdsparser_register_rt(rds_parser, callback_rt);
    rdsparser_register_ct(rds_parser, callback_ct);
    
    printf("Radio Running Successfully!\n");

    return true;
}

void radio_deinit(void)
{
    if (fm_is_powered_up(&radio))
        fm_power_down(&radio);
        
#ifndef RDSPARSER_DISABLE_HEAP
    if (rds_parser != NULL) {
        rdsparser_free(rds_parser);
        rds_parser = NULL;
    }
#endif
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
    rdsparser_data_t data;
    rdsparser_error_t errors = {0}; // No error information from RDA5807
    
    if (fm_read_rds_group(&radio, data)) {
        rdsparser_parse(rds_parser, data, errors);
    }
}

static void print_help(void)
{
    printf(YELLOW "Radio Control Menu:" ENDC "\n");
    printf("====================\n");
    printf("- =   Volume down / up\n");
    printf("s     Scan for stations\n");
    printf("1-9   Tune to scanned station\n");
    printf("{ }   Frequency down / up\n");
    printf("[ ]   Seek down / up\n");
    printf("< >   Adjust seek threshold\n");
    printf("0     Toggle mute\n");
    printf("f     Toggle soft mute\n");
    printf("m     Toggle mono\n");
    printf("b     Toggle bass boost\n");
    printf("i     Print station info\n");
    printf("r     Print RDS info\n");
    printf("x     Power down / up\n");
    printf(BOLD "?     Print this help menu" ENDC "\n");
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

// RDS callback functions
static void callback_ps(rdsparser_t *rds, void *user_data)
{
    // Program Service Name updated
    // This is called automatically when PS changes
}

static void callback_rt(rdsparser_t *rds, rdsparser_rt_flag_t flag, void *user_data)
{
    // Radio Text updated
    // This is called automatically when RT changes
}

void format_rds_time(const rdsparser_ct_t *ct, char *buffer, size_t buffer_size) {
    // Create a tm structure from the RDS CT data
    struct tm timeinfo = {0};
    timeinfo.tm_year = rdsparser_ct_get_year(ct) - 1900;  // tm_year is years since 1900
    timeinfo.tm_mon = rdsparser_ct_get_month(ct) - 1;     // tm_mon is 0-11
    timeinfo.tm_mday = rdsparser_ct_get_day(ct);
    timeinfo.tm_hour = rdsparser_ct_get_hour(ct);
    timeinfo.tm_min = rdsparser_ct_get_minute(ct);
    timeinfo.tm_sec = 0;                 // RDS CT doesn't provide seconds
    
    // Format the time
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}


static void callback_ct(rdsparser_t *rds, const rdsparser_ct_t *ct, void *user_data)
{
    int16_t offset = rdsparser_ct_get_offset(ct);
    char time_str[64];
    format_rds_time(ct, time_str, sizeof(time_str));
    
    printf("Formatted time: %s\n", time_str);
    printf("UTC offset: %d half-hours\n", offset);
}


static void print_rds_info(void)
{
    printf("RDS Info:\n");
    
    // Program Identification
    rdsparser_pi_t pi = rdsparser_get_pi(rds_parser);
    if (pi != RDSPARSER_PI_UNKNOWN) {
        printf("RDS - PI: %04X", pi);
    } else {
        printf("RDS - PI: Unknown");
    }
    
    // Program Type
    rdsparser_pty_t pty = rdsparser_get_pty(rds_parser);
    if (pty != RDSPARSER_PTY_UNKNOWN) {
        printf(", PTY: %s (%d)", rdsparser_pty_lookup_long(pty, false), pty);
    } else {
        printf(", PTY: Unknown");
    }
    
    // Traffic Program and Announcement
    rdsparser_tp_t tp = rdsparser_get_tp(rds_parser);
    if (tp != RDSPARSER_TP_UNKNOWN) {
        printf(", TP: %d", tp);
    }
    
    rdsparser_ta_t ta = rdsparser_get_ta(rds_parser);
    if (ta != RDSPARSER_TA_UNKNOWN) {
        printf(", TA: %d", ta);
    }
    
    // Music/Speech
    rdsparser_ms_t ms = rdsparser_get_ms(rds_parser);
    if (ms != RDSPARSER_MS_UNKNOWN) {
        printf(", MS: %d", ms);
    }
    
    printf("\n");
    
    // Program Service Name
    const rdsparser_string_t *ps = rdsparser_get_ps(rds_parser);
    if (ps && rdsparser_string_get_available(ps)) {
        printf("      PS: %s\n", rdsparser_string_get_content(ps));
    } else {
        printf("      PS: Not available\n");
    }
    
    // Radio Text
    const rdsparser_string_t *rt_a = rdsparser_get_rt(rds_parser, RDSPARSER_RT_FLAG_A);
    if (rt_a && rdsparser_string_get_available(rt_a)) {
        printf("      RT-A: %s\n", rdsparser_string_get_content(rt_a));
    }
    
    const rdsparser_string_t *rt_b = rdsparser_get_rt(rds_parser, RDSPARSER_RT_FLAG_B);
    if (rt_b && rdsparser_string_get_available(rt_b)) {
        printf("      RT-B: %s\n", rdsparser_string_get_content(rt_b));
    }
    
    // Clock Time - use the data stored from the callback
    if (ct_data.valid) {
        printf("      Date: %02u/%02u/%04u\n", ct_data.day, ct_data.month, ct_data.year);
        printf("      Time: %02u:%02u\n", ct_data.hour, ct_data.minute);
    } else {
        printf("      Date/Time: Not available\n");
    }
}

static void set_frequency(float frequency)
{
    fm_set_frequency_blocking(&radio, frequency);
    printf("Set frequency: %.2f MHz\n", fm_get_frequency(&radio));
    rdsparser_clear(rds_parser);
    
    // Reset CT data
    ct_data.valid = false;
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
    rdsparser_clear(rds_parser);
    
    // Reset CT data
    ct_data.valid = false;
}