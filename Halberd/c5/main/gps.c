#include "gps.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hardware.h"
#include "link.h"
#include "link_protocol.h"

static const char *TAG = "gps";

#define GPS_RX_BUF_SIZE       2048
#define GPS_LINE_MAX          128
#define GPS_TASK_STACK        4096
#define GPS_TASK_PRIORITY     8
#define GPS_PUSH_INTERVAL_MS  1000

// Working state. Updated by sentence parsers, snapshot into a link_gps_fix
// when the 1 Hz push fires. All access is single-threaded (gps_task only).
typedef struct {
    bool     date_valid;
    bool     time_valid;
    bool     pos_valid;
    int32_t  lat_e7;
    int32_t  lon_e7;
    int16_t  altitude_m;
    uint8_t  satellites;
    uint16_t hdop_x100;
    uint16_t speed_kmh_x10;
    uint16_t course_deg_x10;
    uint16_t year;
    uint8_t  month, day, hour, minute, second, centisecond;
    uint8_t  fix_quality;
} gps_state_t;

static gps_state_t s_state;

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Validate a buffered NMEA sentence. Expects "$...*XX" with no trailing
// \r\n (the line reader strips those). XOR of bytes between '$' and '*'
// must equal the two hex digits after '*'.
static bool nmea_validate(const char *line, size_t len) {
    if (len < 5 || line[0] != '$') return false;
    const char *star = memchr(line, '*', len);
    if (!star) return false;
    if ((size_t)(star - line + 3) > len) return false;
    int hi = hex_nibble(star[1]);
    int lo = hex_nibble(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t want = (uint8_t)((hi << 4) | lo);
    uint8_t got = 0;
    for (const char *p = line + 1; p < star; p++) {
        got ^= (uint8_t)*p;
    }
    return got == want;
}

// Split an NMEA line on ',' up to '*'. Mutates `line` (replaces commas and
// the '*' with NULs). Returns field count; fills `fields` with pointers
// into `line`.
static int nmea_split(char *line, char **fields, int max_fields) {
    int count = 0;
    char *start = line;
    for (char *p = line; *p && count < max_fields; p++) {
        if (*p == '*') {
            *p = '\0';
            fields[count++] = start;
            return count;
        }
        if (*p == ',') {
            *p = '\0';
            fields[count++] = start;
            start = p + 1;
        }
    }
    if (count < max_fields) {
        fields[count++] = start;
    }
    return count;
}

// Convert NMEA DDMM.MMMM (lat: deg_width=2) or DDDMM.MMMM (lon: deg_width=3)
// plus hemisphere indicator into degrees × 1e7.
static int32_t parse_lat_lon(const char *s, char hemi, int deg_width) {
    if (!s || !*s) return 0;
    size_t slen = strlen(s);
    if ((int)slen < deg_width + 1) return 0;

    char deg_buf[4];
    if (deg_width + 1 > (int)sizeof(deg_buf)) return 0;
    memcpy(deg_buf, s, deg_width);
    deg_buf[deg_width] = '\0';
    int degrees = atoi(deg_buf);

    double minutes = atof(s + deg_width);  // MM.MMMM
    double dec = (double)degrees + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') dec = -dec;
    return (int32_t)(dec * 1e7);
}

// Parse $xxRMC. Field order:
//   0=$xxRMC, 1=time, 2=status, 3=lat, 4=N/S, 5=lon, 6=E/W,
//   7=speed_kts, 8=course_deg, 9=date_DDMMYY, ...
static void parse_rmc(char **f, int n) {
    if (n < 10) return;
    const bool active = (f[2][0] == 'A');

    if (active && f[1][0] && strlen(f[1]) >= 6) {
        s_state.hour   = (uint8_t)((f[1][0] - '0') * 10 + (f[1][1] - '0'));
        s_state.minute = (uint8_t)((f[1][2] - '0') * 10 + (f[1][3] - '0'));
        s_state.second = (uint8_t)((f[1][4] - '0') * 10 + (f[1][5] - '0'));
        s_state.centisecond = 0;
        const char *dot = strchr(f[1], '.');
        if (dot && dot[1] >= '0' && dot[1] <= '9' &&
                   dot[2] >= '0' && dot[2] <= '9') {
            s_state.centisecond = (uint8_t)((dot[1] - '0') * 10 + (dot[2] - '0'));
        }
        s_state.time_valid = true;
    }
    if (active && f[9][0] && strlen(f[9]) >= 6) {
        s_state.day   = (uint8_t)((f[9][0] - '0') * 10 + (f[9][1] - '0'));
        s_state.month = (uint8_t)((f[9][2] - '0') * 10 + (f[9][3] - '0'));
        s_state.year  = (uint16_t)(2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0'));
        s_state.date_valid = true;
    }
    if (active && f[3][0] && f[5][0]) {
        s_state.lat_e7 = parse_lat_lon(f[3], f[4][0], 2);
        s_state.lon_e7 = parse_lat_lon(f[5], f[6][0], 3);
        s_state.pos_valid = true;
    } else if (!active) {
        s_state.pos_valid = false;
    }
    if (f[7][0]) {
        s_state.speed_kmh_x10 = (uint16_t)(atof(f[7]) * 1.852 * 10.0);
    }
    if (f[8][0]) {
        s_state.course_deg_x10 = (uint16_t)(atof(f[8]) * 10.0);
    }
}

// Parse $xxGGA. Field order:
//   0=$xxGGA, 1=time, 2=lat, 3=N/S, 4=lon, 5=E/W,
//   6=fix_quality, 7=satellites, 8=hdop, 9=altitude_m, ...
static void parse_gga(char **f, int n) {
    if (n < 10) return;
    if (f[6][0]) s_state.fix_quality = (uint8_t)atoi(f[6]);
    if (f[7][0]) s_state.satellites  = (uint8_t)atoi(f[7]);
    if (f[8][0]) s_state.hdop_x100   = (uint16_t)(atof(f[8]) * 100.0);
    if (f[9][0]) s_state.altitude_m  = (int16_t)atof(f[9]);
    if (s_state.fix_quality > 0 && f[2][0] && f[4][0]) {
        s_state.lat_e7 = parse_lat_lon(f[2], f[3][0], 2);
        s_state.lon_e7 = parse_lat_lon(f[4], f[5][0], 3);
        s_state.pos_valid = true;
    }
}

static void handle_line(char *line, size_t len) {
    if (!nmea_validate(line, len)) {
        ESP_LOGD(TAG, "checksum fail: %.*s", (int)len, line);
        return;
    }
    // Talker ID is 2 chars after '$' (GP/GN/GA/BD/…); sentence id is the
    // next 3 chars. We support RMC and GGA from any talker.
    if (len < 6) return;
    const char id[3] = { line[3], line[4], line[5] };

    char *fields[20];
    int n = nmea_split(line, fields, 20);

    if (id[0] == 'R' && id[1] == 'M' && id[2] == 'C') {
        parse_rmc(fields, n);
    } else if (id[0] == 'G' && id[1] == 'G' && id[2] == 'A') {
        parse_gga(fields, n);
    }
}

static void send_fix(void) {
    struct link_gps_fix p;
    p.uptime_ms       = (uint32_t)(esp_timer_get_time() / 1000);
    p.fix_valid       = (s_state.fix_quality > 0 && s_state.pos_valid) ? 1 : 0;
    p.satellites      = s_state.satellites;
    p.hdop_x100       = s_state.hdop_x100;
    p.lat_e7          = s_state.lat_e7;
    p.lon_e7          = s_state.lon_e7;
    p.altitude_m      = s_state.altitude_m;
    p.speed_kmh_x10   = s_state.speed_kmh_x10;
    p.course_deg_x10  = s_state.course_deg_x10;
    p.year            = s_state.year;
    p.month           = s_state.month;
    p.day             = s_state.day;
    p.hour            = s_state.hour;
    p.minute          = s_state.minute;
    p.second          = s_state.second;
    p.centisecond     = s_state.centisecond;
    p.date_valid      = s_state.date_valid ? 1 : 0;
    p.time_valid      = s_state.time_valid ? 1 : 0;
    p.reserved        = 0;
    link_send_gps_fix(&p);
}

static void gps_task(void *arg) {
    (void)arg;
    char line[GPS_LINE_MAX];
    size_t fill = 0;
    uint8_t rx[256];
    TickType_t last_push = xTaskGetTickCount();
    const TickType_t push_interval = pdMS_TO_TICKS(GPS_PUSH_INTERVAL_MS);

    while (1) {
        const int got = uart_read_bytes(HALBERD_C5_GPS_UART_NUM, rx, sizeof(rx),
                                        pdMS_TO_TICKS(100));
        for (int i = 0; i < got; i++) {
            const char c = (char)rx[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (fill > 0) {
                    line[fill] = '\0';
                    handle_line(line, fill);
                    fill = 0;
                }
            } else if (fill < sizeof(line) - 1) {
                line[fill++] = c;
            } else {
                // Sentence overflowed buffer — drop and resync.
                fill = 0;
            }
        }

        if ((xTaskGetTickCount() - last_push) >= push_interval) {
            send_fix();
            last_push = xTaskGetTickCount();
        }
    }
}

void gps_init(void) {
    memset(&s_state, 0, sizeof(s_state));

    const uart_config_t cfg = {
        .baud_rate  = HALBERD_C5_GPS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(HALBERD_C5_GPS_UART_NUM,
                                        GPS_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HALBERD_C5_GPS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(HALBERD_C5_GPS_UART_NUM,
                                 HALBERD_C5_GPS_TX_GPIO,
                                 HALBERD_C5_GPS_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(gps_task, "gps", GPS_TASK_STACK, NULL, GPS_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init UART%d rx=%d tx=%d baud=%d push_hz=%d",
             HALBERD_C5_GPS_UART_NUM,
             HALBERD_C5_GPS_RX_GPIO, HALBERD_C5_GPS_TX_GPIO,
             HALBERD_C5_GPS_BAUD,
             1000 / GPS_PUSH_INTERVAL_MS);
}
