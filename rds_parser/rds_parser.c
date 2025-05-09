/*
 * Copyright (c) 2021 Valentin Milea <valentin.milea@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

 #include <rds_parser.h>
 #include <string.h>
 #include <stdio.h>

 
 //
 // misc
 //
 
 static char hex_to_char(uint8_t value) {
     assert(value < 16);
 
     return (value < 10 ? ('0' + value) : ('A' - 10 + value));
 }
 
 //
 // rds_group
 //
 

 
 static uint16_t rds_get_group_pi(const rds_group_t *group) {
     return group->a;
 }
 
 static rds_group_type_t rds_get_group_type(const rds_group_t *group) {
     return group->b >> 12;
 }
 
 static uint8_t rds_get_group_version(const rds_group_t *group) {
     return (group->b >> 11) & 0x01;
 }
 
 static bool rds_get_group_tp(const rds_group_t *group) {
     return ((group->b >> 10) & 0x01) != 0;
 }
 
 static uint8_t rds_get_group_pty(const rds_group_t *group) {
     return (group->b >> 5) & 0x1F;
 }
 
 //
 // rds_parser_t
 //
 
 static void rds_parse_group_basic_ps(rds_parser_t *parser, const rds_group_t *group) {
     // group 0A / 0B
     size_t address = group->b & 0x3;
     size_t char_index = 2 * address;
     char ch0 = group->d >> 8;
     char ch1 = group->d & 0xFF;
     parser->ps_scratch_str[char_index] = ch0;
     parser->ps_scratch_str[char_index + 1] = ch1;
 
     bool finished = (address == 3);
     if (finished) {
         memcpy(parser->ps_str, parser->ps_scratch_str, 8);
     }
 }
 
 static void rds_parse_group_basic_di(rds_parser_t *parser, const rds_group_t *group) {
     // group 0A / 0B
     size_t di_bit_index = ~group->b & 0x3;
     uint8_t di_bit = (group->b >> 2) & 0x1;
     parser->di_scratch &= ~(1 << di_bit_index);
     parser->di_scratch |= di_bit << di_bit_index;
 
     bool finished = (di_bit_index == 0);
     if (finished) {
         parser->di = parser->di_scratch;
     }
 }
 
 #if RDS_PARSER_ALTERNATIVE_FREQUENCIES_ENABLE
 static void rds_add_alt_freq(rds_parser_t *parser, uint8_t alt_freq) {
     if (alt_freq == 0 || 205 <= alt_freq) {
         return; // out of range, ignored
     }
     if (parser->alt_freq_count == sizeof(parser->alt_freq)) {
         return; // list full, ignored
     }
     for (size_t i = 0; i < parser->alt_freq_count; i++) {
         if (parser->alt_freq[i] == alt_freq) {
             return; // duplicate, ignored
         }
     }
     parser->alt_freq[parser->alt_freq_count++] = alt_freq;
 }
 
 static void rds_parse_group_basic_alt_freq(rds_parser_t *parser, const rds_group_t *group) {
     uint8_t version = rds_get_group_version(group);
     if (version != 0) {
         return;
     }
     // group 0A
     uint8_t f0 = group->c >> 8;
     uint8_t f1 = group->c & 0xFF;
     rds_add_alt_freq(parser, f0);
     rds_add_alt_freq(parser, f1);
 }
 #endif // RDS_PARSER_ALTERNATIVE_FREQUENCIES_ENABLE
 
 static void rds_parse_group_basic(rds_parser_t *parser, const rds_group_t *group) {
     rds_parse_group_basic_ps(parser, group);
     rds_parse_group_basic_di(parser, group);
 #if RDS_PARSER_ALTERNATIVE_FREQUENCIES_ENABLE
     rds_parse_group_basic_alt_freq(parser, group);
 #endif
 }
 
 #if RDS_PARSER_RADIO_TEXT_ENABLE
 static void rds_parse_group_rt(rds_parser_t *parser, const rds_group_t *group) {
     uint8_t version = rds_get_group_version(group);
     size_t address = group->b & 0xF;
     parser->rt_scratch_a_b = (group->b >> 4) & 0x1;
 
     char chars[4];
     size_t char_count;
     size_t char_index;
     if (version == 0) { // group 2A
         chars[0] = group->c >> 8;
         chars[1] = group->c & 0xFF;
         chars[2] = group->d >> 8;
         chars[3] = group->d & 0xFF;
         char_count = 4;
         char_index = address * 4;
     } else { // group 2B
         chars[0] = group->d >> 8;
         chars[1] = group->d & 0xFF;
         char_count = 2;
         char_index = address * 2;
     }
 
     bool finished = false;
     for (size_t i = 0; i < char_count; i++) {
         char ch = chars[i];
         if (ch == '\r') {
             parser->rt_scratch_str[char_index++] = '\0';
             finished = true;
             break;
         }
         parser->rt_scratch_str[char_index++] = ch;
         if (char_index == 64) {
             finished = true;
             break;
         }
     }
     if (finished) {
         memcpy(parser->rt_str, parser->rt_scratch_str, 64);
         parser->rt_a_b = parser->rt_scratch_a_b;
     }
 }
 #endif // RDS_PARSER_RADIO_TEXT_ENABLE
 
 static void rds_parse_group_clock_time(rds_parser_t *parser, const rds_group_t *group) {
    // Extract fields from RDS data
    uint32_t mjd = ((group->c & 0xFFF0) >> 4);
    uint8_t hour = ((group->c & 0x000F) << 2) | ((group->d & 0xC000) >> 14);
    uint8_t minute = ((group->d & 0x3F00) >> 8);
    int8_t offset = (group->d & 0x003F);

    // Validate time fields
    if (hour < 24 && minute < 60) {
        parser->time_info.mjd = mjd;
        parser->time_info.hour = hour;
        parser->time_info.minute = minute;
        parser->time_info.offset = offset;
        parser->time_info.time_valid = true;
        
        // Convert MJD to date
        uint32_t j = mjd + 2400001;
        uint32_t y = (uint32_t)((j - 1867216.25)/36524.25);
        uint32_t c = j + y - (y/4) + 1525;
        y = (uint32_t)((c - 122.1)/365.25);
        uint32_t d = (uint32_t)(365.25 * y);
        uint32_t m = (uint32_t)((c - d)/30.6001);
        
        parser->time_info.day = c - d - (uint32_t)(30.6001 * m);
        parser->time_info.month = m - 1;
        if (m > 13) parser->time_info.month = m - 13;
        parser->time_info.year = y - 4715;
        if (parser->time_info.month > 2) parser->time_info.year -= 1;

        // Apply timezone offset
        if (offset != 0) {
            int16_t offset_minutes = (offset & 0x1F) * 30;
            if (offset & 0x20) {
                offset_minutes = -offset_minutes;
            }
            // Adjust hour and handle day wrapping if needed
            // ... add timezone adjustment code here
        }
    }
}

static void rds_parse_rt_plus_tags(rds_parser_t *parser, const rds_group_t *group) {
    bool running = (group->b >> 4) & 0x1;
    if (!running) return;

    parser->has_rt_plus = true;
    
    // Extract RT+ content type and start/length information
    parser->rt_plus_tags[0] = (group->c >> 10) & 0x3F;
    parser->rt_plus_start[0] = (group->c >> 4) & 0x3F;
    parser->rt_plus_length[0] = ((group->c & 0x0F) << 2) | ((group->d >> 14) & 0x03);
    
    parser->rt_plus_tags[1] = (group->d >> 8) & 0x3F;
    parser->rt_plus_start[1] = (group->d >> 2) & 0x3F;
    parser->rt_plus_length[1] = group->d & 0x03;

    // Process tags if RadioText is available
    if (parser->rt_str[0] != '\0') {
        for (int i = 0; i < 2; i++) {
            if (parser->rt_plus_tags[i] == 1) { // ITEM.TITLE
                size_t len = parser->rt_plus_length[i];
                size_t start = parser->rt_plus_start[i];
                if (start + len < 64) {
                    memcpy(parser->current_title, 
                           &parser->rt_str[start], 
                           len);
                    parser->current_title[len] = '\0';
                }
            }
            else if (parser->rt_plus_tags[i] == 4) { // ITEM.ARTIST
                size_t len = parser->rt_plus_length[i];
                size_t start = parser->rt_plus_start[i];
                if (start + len < 64) {
                    memcpy(parser->current_artist, 
                           &parser->rt_str[start], 
                           len);
                    parser->current_artist[len] = '\0';
                }
            }
        }
    }
}
 //
 // public interface
 //
 
 void rds_parser_reset(rds_parser_t *parser) {
     memset(parser, 0, sizeof(rds_parser_t));
 }
 
 void rds_parser_update(rds_parser_t *parser, const rds_group_t *group) {
    parser->pi = rds_get_group_pi(group);
    parser->pty = rds_get_group_pty(group);
    parser->tp = rds_get_group_tp(group);

    switch (rds_get_group_type(group)) {
    case RDS_GROUP_BASIC:
        rds_parse_group_basic(parser, group);
        break;
    case RDS_GROUP_CLOCK:
        if (rds_get_group_version(group) == 0) { // Only process 4A
            //print raw data on time for debugging
            printf("Raw RDS data: A: %04X B: %04X C: %04X D: %04X\n", group->a, group->b, group->c, group->d);  
            rds_parse_group_clock_time(parser, group);
        }
        break;
    case RDS_GROUP_RT_PLUS:
        if (rds_get_group_version(group) == 0) { // Only process 17A
            rds_parse_rt_plus_tags(parser, group);
        }
        break;
#if RDS_PARSER_RADIO_TEXT_ENABLE
    case RDS_GROUP_RT:
        rds_parse_group_rt(parser, group);
        break;
#endif
    default:
        break;
    }
 }
 
 void rds_get_program_id_as_str(const rds_parser_t *parser, char *str) {
     str[0] = hex_to_char(parser->pi >> 12);
     str[1] = hex_to_char((parser->pi >> 8) & 0xF);
     str[2] = hex_to_char((parser->pi >> 4) & 0xF);
     str[3] = hex_to_char(parser->pi & 0xF);
     str[4] = '\0';
 }

 bool rds_get_time(const rds_parser_t *parser, uint8_t *hour, uint8_t *minute) {
    if (!parser->time_info.time_valid) {
        return false;
    }
    *hour = parser->time_info.hour;
    *minute = parser->time_info.minute;
    return true;
}

void rds_get_current_title(const rds_parser_t *parser, char *title, size_t max_len) {
    strncpy(title, parser->current_title, max_len - 1);
    title[max_len - 1] = '\0';
}

void rds_get_current_artist(const rds_parser_t *parser, char *artist, size_t max_len) {
    strncpy(artist, parser->current_artist, max_len - 1);
    artist[max_len - 1] = '\0';
}