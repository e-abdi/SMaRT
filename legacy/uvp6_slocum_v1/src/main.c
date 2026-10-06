#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>   // strtol, strtod
#include <time.h>     // gmtime for time conversion

#include "glider.h"
#include "sensor.h"

// ===== Debug via RTT (never touches UARTs) =====
#define DEBUG_RTT 1
#if DEBUG_RTT
#include <zephyr/sys/printk.h>
#endif

#define UART0_NODE DT_NODELABEL(uart0) // Glider
#define UART1_NODE DT_NODELABEL(uart1) // Sensor
#if !DT_NODE_EXISTS(UART0_NODE) || !DT_NODE_EXISTS(UART1_NODE)
#error "uart0/uart1 not found in device tree"
#endif

// ===== State Machine =====
typedef enum {
    STATE_POWERUP,     // 2-second startup monitoring HI/SD and HW_CONF
    STATE_IDLE,        // Waiting for dive start (HI or SD messages)
    STATE_PASSTHROUGH, // Bidirectional UART forwarding
    STATE_DIVE,        // Processing dive data
} app_state_t;

static volatile app_state_t current_state = STATE_POWERUP;

static const struct device *const uart0_dev = DEVICE_DT_GET(UART0_NODE); // glider
static const struct device *const uart1_dev = DEVICE_DT_GET(UART1_NODE); // sensor

// ------ Minimal token matcher ------
typedef struct {
    const char *pat;
    size_t      len;
    size_t      pos;
    bool        hit;
} stream_match_t;

static inline void matcher_init(stream_match_t *m, const char *pat){
    m->pat = pat; m->len = strlen(pat); m->pos = 0; m->hit = false;
}
static inline bool matcher_feed(stream_match_t *m, uint8_t ch){
    if (m->hit || m->len == 0) return m->hit;
    if (ch == (uint8_t)m->pat[m->pos]) {
        if (++m->pos == m->len) m->hit = true;
    } else {
        m->pos = (ch == (uint8_t)m->pat[0]) ? 1 : 0;
    }
    return m->hit;
}

// ------ Line accumulator ------
typedef struct { char buf[512]; size_t len; int64_t last_ms; } line_acc_t;
static inline void line_acc_reset(line_acc_t *a){ a->len = 0; a->buf[0] = '\0'; a->last_ms = k_uptime_get(); }
static inline bool line_acc_add(line_acc_t *a, uint8_t ch){
    if (a->len < sizeof(a->buf)-1) { a->buf[a->len++] = (char)ch; a->buf[a->len] = '\0'; }
    a->last_ms = k_uptime_get();
    return (ch=='\n') || (ch=='\r');
}
static inline bool line_acc_stalled(const line_acc_t *a){
    // Increase idle threshold to avoid cutting lines early when upstream flush time is ~170ms
    return (a->len > 0 && (a->len >= sizeof(a->buf)-4 || (k_uptime_get() - a->last_ms) > 300));
}

// --------- Helpers: SW on UART0 (with XOR checksum) ----------
static uint8_t xor_checksum(const char *payload){ uint8_t cs=0; for (const char *p=payload; *p; ++p) cs^=(uint8_t)(*p); return cs; }
static inline char hex_hi(uint8_t v){ v>>=4; return (v<10)?('0'+v):('A'+(v-10)); }
static inline char hex_lo(uint8_t v){ v&=0x0F; return (v<10)?('0'+v):('A'+(v-10)); }

static inline void uart0_send_byte(uint8_t b){ uart_poll_out(uart0_dev, b); }
static inline void uart0_send_bytes(const uint8_t *d, size_t n){ for (size_t i=0;i<n;i++) uart_poll_out(uart0_dev, d[i]); }
static inline void uart1_send_bytes(const uint8_t *d, size_t n){ for (size_t i=0;i<n;i++) uart_poll_out(uart1_dev, d[i]); }

// Integer formatter for signed and unsigned values
static void send_sw_on_uart0_int(uint8_t index, int32_t value){
    char payload[32];
    int n;
    if (value < 0) {
        n = snprintk(payload, sizeof(payload), "SW,%u:%d", (unsigned)index, (int)value);
    } else {
        n = snprintk(payload, sizeof(payload), "SW,%u:%u", (unsigned)index, (unsigned)value);
    }
    if (n <= 0 || n >= (int)sizeof(payload)) return;
    uint8_t cs = xor_checksum(payload);
    uart0_send_byte('$');
    for (int i=0;i<n;i++) uart0_send_byte((uint8_t)payload[i]);
    uart0_send_byte('*'); uart0_send_byte((uint8_t)hex_hi(cs)); uart0_send_byte((uint8_t)hex_lo(cs));
    uart0_send_byte('\r'); uart0_send_byte('\n');
#if DEBUG_RTT
    printk("[RTT] ->U0 SW: $%s*%02X\\r\\n\n", payload, cs);
#endif
}

// --------- Sensor cmds on UART1 ----------
static inline void sensor_send_cstr_noeol(const char *s){
    uart1_send_bytes((const uint8_t*)s, strlen(s));
}
static inline void sensor_send_lf(void){
    static const char e[] = "\n";
    uart1_send_bytes((const uint8_t*)e, 1);
}

// Send start command with time: $start:ACQ_CSCS_002H,YYYYMMDD,HHMMSS;
static void sensor_send_start_with_time(double epoch_f){
    // Convert epoch to YYYYMMDD,HHMMSS format
    time_t epoch_t = (time_t)epoch_f;
    struct tm *time_info = gmtime(&epoch_t);
    
    char start_cmd[64];
    int n = snprintk(start_cmd, sizeof(start_cmd), "$start:ACQ_CSCS_002H,%04d%02d%02d,%02d%02d%02d;",
                     time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
                     time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
    if (n <= 0 || n >= (int)sizeof(start_cmd)) return;
    sensor_send_cstr_noeol(start_cmd);
    sensor_send_lf();
#if DEBUG_RTT
    printk("[RTT] ->U1 START (with time): %s\\n\n", start_cmd);
#endif
}

// Send start command without time: $start:ACQ_CSCS_002H;
static void sensor_send_start_no_time(void){
    sensor_send_cstr_noeol(SENSOR_START_NO_TIME);
    sensor_send_lf();
#if DEBUG_RTT
    printk("[RTT] ->U1 START (no time): %s\\n\n", SENSOR_START_NO_TIME);
#endif
}

// Send stop command: $stop;
static void sensor_send_stop(void){
    sensor_send_cstr_noeol(SENSOR_STOP_CMD);
    sensor_send_lf();
#if DEBUG_RTT
    printk("[RTT] ->U1 STOP: %s\\n\n", SENSOR_STOP_CMD);
#endif
}


// ===== Global state (declared early for use in functions) =====
static stream_match_t m_glider_hi, m_glider_sd, m_sensor_hw;
static line_acc_t acc0, acc1;

// Powerup phase tracking
static bool powerup_glider_seen = false;  // HI or SD received
static bool powerup_sensor_seen = false;  // HW_CONF received
static int64_t powerup_start_time = 0;

// Dive phase tracking
static bool glider_seen = false;
static bool sensor_seen = false;
static bool glider_sd_seen = false;

static int  last_mode = -1;
static int  last_mode_before_stop = -1;  // Track mode before stop for restart decision
static volatile bool stop_in_progress = false; // avoid re-entrant stops
static volatile bool sensor_restart_pending = false; // restart after 2-sec delay
static volatile int64_t sensor_restart_time = 0;

// ------ Non-blocking poll helper ------
static inline void poll_drain_uart(const struct device *dev, void (*on_byte)(uint8_t)){
    unsigned char ch;
    static int poll_count_u0 = 0, poll_count_u1 = 0;
    static int bytes_received_u0 = 0, bytes_received_u1 = 0;
    int byte_count = 0;
    
    while (uart_poll_in(dev, &ch) == 0) {
        on_byte((uint8_t)ch);
        byte_count++;
    }
    
#if DEBUG_RTT
    if (dev == uart0_dev) {
        if (poll_count_u0++ % 5000 == 0) {
            printk("[RTT] UART0 poll stats: call_count=%d, total_bytes=%d\n", poll_count_u0, bytes_received_u0);
        }
        if (byte_count > 0) {
            bytes_received_u0 += byte_count;
            printk("[RTT] UART0 got %d bytes (total so far: %d)\n", byte_count, bytes_received_u0);
        }
    } else if (dev == uart1_dev) {
        if (poll_count_u1++ % 5000 == 0) {
            printk("[RTT] UART1 poll stats: call_count=%d, total_bytes=%d\n", poll_count_u1, bytes_received_u1);
        }
        if (byte_count > 0) {
            bytes_received_u1 += byte_count;
        }
    }
#endif
}

// Passthrough: drain all available bytes on src and forward to dst
// Returns number of bytes forwarded, and transitions to IDLE if $QUIT is detected
static inline uint32_t forward_all(const struct device *src, const struct device *dst, const char *tag){
    uint32_t count = 0;
    unsigned char ch;
    static line_acc_t quit_buffer;  // Buffer to detect $QUIT command
    
    while (uart_poll_in(src, &ch) == 0) {
        // Forward the byte
        uart_poll_out(dst, ch);
        count++;
        
        // Check for $QUIT command by accumulating bytes
        bool quit_eol = line_acc_add(&quit_buffer, ch);
        if (quit_eol || line_acc_stalled(&quit_buffer)) {
            if (strstr(quit_buffer.buf, "$QUIT")) {
                current_state = STATE_IDLE;  // Exit passthrough
#if DEBUG_RTT
                printk("[RTT] $QUIT received, transitioning to IDLE\n");
#endif
            }
            line_acc_reset(&quit_buffer);
        }
    }
    return count;
}

static double last_epoch_f = 0.0;     // Most recent epoch from SD message
static double last_depth_f = 0.0;    // Most recent depth from SD message
static bool   have_epoch   = false;

static int   sample_mode = 0;         // 0=none, 1=dive, 2=climb, 3=hover
static double sample_trigger_depth = 0.0;
static int   sample_number = 0;       // 0-8 (9 samples)
static int   sample_below = 0;        // sample_below field (for future use)
static bool  sample_triggered = false; // flag to avoid duplicate triggers

// LPM_DATA tracking from sensor
static uint32_t lpm_data_count = 0;   // Number of LPM_DATA lines received
static double lpm_max_depth = 0.0;    // Maximum depth from LPM_DATA

// --- Parse helpers ---
// Parse "$SD,..." → extract epoch (field 4), depth (field 5), mode (field 6)
// NEW FIELD INDICES per requirements
static void parse_sd_fields(const char *line, double *epoch_out, double *depth_out, int *mode_out,
                            int *sample_mode_out, double *sample_depth_out, int *sample_num_out, int *sample_below_out)
{
    const char *p = strstr(line, "$SD,");
    if (!p) return;
    char tmp[256];
    strncpy(tmp, p+4, sizeof(tmp)-1); // skip "$SD,"
    tmp[sizeof(tmp)-1] = '\0';
    char *star = strchr(tmp, '*');
    if (star) *star = '\0';

    char *tok, *ctx = NULL;
    int field_count = 0;
    for (tok = strtok_r(tmp, ",", &ctx); tok; tok = strtok_r(NULL, ",", &ctx)) {
        int idx = -1;
        const char *colon = strchr(tok, ':');
        if (!colon) continue;
        idx = (int)strtol(tok, NULL, 10);
        const char *val = colon + 1;

        // Field mapping: NEW indices per requirements
        if (idx == 4 && epoch_out) {
            double e = strtod(val, NULL);
            if (e > 0.0) *epoch_out = e;
#if DEBUG_RTT
            printk("[RTT] SD field 4 (epoch): %d.%03d\n", (int)e, (int)((e - (int)e) * 1000));
#endif
        } else if (idx == 5 && depth_out) {
            double d = strtod(val, NULL);
            *depth_out = d;
#if DEBUG_RTT
            printk("[RTT] SD field 5 (depth): %d.%01d\n", (int)d, (int)((d - (int)d) * 10));
#endif
        } else if (idx == 6 && mode_out) {
            int m = (int)strtol(val, NULL, 10);
            *mode_out = m;
#if DEBUG_RTT
            printk("[RTT] SD field 6 (mode): %d\n", m);
#endif
        }
    }
}


// Note: Phase 1 token detection removed. System now uses state machine for all operations.

// ------ Byte handlers for powerup phase ------
static void on_u0_powerup(uint8_t b){
    if (!powerup_glider_seen) {
        if (matcher_feed(&m_glider_hi, b) || matcher_feed(&m_glider_sd, b)) {
            powerup_glider_seen = true;
#if DEBUG_RTT
            printk("[RTT] POWERUP: HI or SD detected from glider\n");
#endif
        }
    }
    bool eol = line_acc_add(&acc0, b);
    if (eol || line_acc_stalled(&acc0)) {
        // During powerup, parse SD messages to capture epoch and other fields
        if (strstr(acc0.buf, "$SD,")) {
            double ep=0.0, dep=0.0; int md=-1;
            parse_sd_fields(acc0.buf, &ep, &dep, &md, NULL, NULL, NULL, NULL);
            if (ep > 0.0) { 
                last_epoch_f = ep; 
                have_epoch = true; 
#if DEBUG_RTT
                printk("[RTT] POWERUP: SD epoch captured: %d.%03d\n", (int)ep, (int)((ep - (int)ep) * 1000));
#endif
            }
            if (dep >= 0.0) { 
                last_depth_f = dep;
#if DEBUG_RTT
                printk("[RTT] POWERUP: SD depth captured: %d.%01d\n", (int)dep, (int)((dep - (int)dep) * 10));
#endif
            }
            if (md >= 0) { 
                last_mode = md;
#if DEBUG_RTT
                printk("[RTT] POWERUP: SD mode captured: %d\n", md);
#endif
            }
        }
        line_acc_reset(&acc0);
    }
}

static void on_u1_powerup(uint8_t b){
    if (!powerup_sensor_seen && matcher_feed(&m_sensor_hw, b)) {
        powerup_sensor_seen = true;
#if DEBUG_RTT
        printk("[RTT] POWERUP: HW_CONF detected from sensor\n");
#endif
    }
    bool eol = line_acc_add(&acc1, b);
    if (eol || line_acc_stalled(&acc1)) {
        if (strstr(acc1.buf, SENSOR_HW_CONF_STR)) {
#if DEBUG_RTT
            printk("[RTT] POWERUP: HW_CONF line received: %s\n", acc1.buf);
#endif
        }
        line_acc_reset(&acc1);
    }
}

// ------ Byte handlers for dive/idle phase ------
static void on_u0_run(uint8_t b){
#if DEBUG_RTT
    static int u0_call_counter = 0;
    if (u0_call_counter++ % 1000 == 0) {
        printk("[RTT] on_u0_run called (counter=%d)\n", u0_call_counter);
    }
#endif
    if (!glider_seen && matcher_feed(&m_glider_hi, b)) glider_seen = true;

    bool eol = line_acc_add(&acc0, b);
    if (eol || line_acc_stalled(&acc0)) {
        // Quick visibility: detect presence of depth field even if parse fails
#if DEBUG_RTT
        if (strstr(acc0.buf, "$SD,5:")) {
            printk("[RTT] SD,5 present in buffer: %s\n", acc0.buf);
        }
#endif
        // Allow MIRROR to trigger passthrough in IDLE state
        if (current_state == STATE_IDLE && strstr(acc0.buf, "$MIRROR")) {
            current_state = STATE_PASSTHROUGH;
#if DEBUG_RTT
            printk("[RTT] $MIRROR requested (entering passthrough)\n");
#endif
        }

        // BY (end of dive) handling: exit DIVE state and return to IDLE
        if (strstr(acc0.buf, "$BY")) {
#if DEBUG_RTT
            printk("[RTT] $BY received, ending dive. Stopping sensor...\n");
#endif
            if (current_state == STATE_DIVE) {
                // Send LPM stats before stopping
                send_sw_on_uart0_int(2, (int32_t)lpm_data_count);
#if DEBUG_RTT
                printk("[RTT] Sent SW,2:%u (LPM count)\n", lpm_data_count);
#endif
                send_sw_on_uart0_int(3, (int32_t)(lpm_max_depth * 100));  // Send as x100 for precision
#if DEBUG_RTT
                printk("[RTT] Sent SW,3:%u (max depth x100: %d.%02d)\n", 
                       (uint32_t)(lpm_max_depth * 100), (int)lpm_max_depth, (int)((lpm_max_depth - (int)lpm_max_depth) * 100));
#endif
                
                // Send stop commands
                sensor_send_stop();
                k_msleep(SENSOR_STOP_DELAY_MS);
                sensor_send_stop();
                stop_in_progress = true;
#if DEBUG_RTT
                printk("[RTT] Sensor stop commands sent (x2)\n");
#endif
            }
            current_state = STATE_IDLE;
            glider_seen = false;
            glider_sd_seen = false;
            sensor_seen = false;
            sample_triggered = false;
            last_mode = -1;
            sensor_restart_pending = false;
            stop_in_progress = false;
            lpm_data_count = 0;
            lpm_max_depth = 0.0;
            line_acc_reset(&acc0);
            return;
        }

        if (strstr(acc0.buf, "$HI") || strstr(acc0.buf, "$SD,")) {
            if (strstr(acc0.buf, "$SD,")) {
                glider_sd_seen = true;
            }
            if (current_state == STATE_IDLE) {
                current_state = STATE_DIVE;
#if DEBUG_RTT
                printk("[RTT] DIVE START: HI or SD message received\n");
#endif
                // Reset LPM stats for new dive
                lpm_data_count = 0;
                lpm_max_depth = 0.0;
                
                // Send start command to sensor
                if (have_epoch && last_epoch_f > 0.0) {
                    sensor_send_start_with_time(last_epoch_f);
                    send_sw_on_uart0_int(1, 1);  // Valid time
#if DEBUG_RTT
                    printk("[RTT] Sensor started with time, SW,1:1 sent\n");
#endif
                } else {
                    sensor_send_start_no_time();
                    send_sw_on_uart0_int(1, -1);  // No valid time
#if DEBUG_RTT
                    printk("[RTT] Sensor started without time, SW,1:-1 sent\n");
#endif
                }
                last_mode = -1;
                last_mode_before_stop = -1;
                sample_triggered = false;
            } else if (current_state == STATE_DIVE && strstr(acc0.buf, "$SD,")) {
                // Handle SD message during dive
#if DEBUG_RTT
                printk("[RTT] SD message in DIVE state: %s\n", acc0.buf);
#endif
                double ep=0.0, dep=0.0; int md=-1;
                parse_sd_fields(acc0.buf, &ep, &dep, &md, NULL, NULL, NULL, NULL);
                
                if (ep > 0.0) { last_epoch_f = ep; have_epoch = true; }
                if (dep > 0.0) { last_depth_f = dep; }
                
                if (md >= 0) {
                    if (last_mode < 0) {
                        last_mode = md;
                    } else if (md != last_mode) {
                        // Mode change detected
#if DEBUG_RTT
                        printk("[RTT] Mode change detected: %d -> %d\n", last_mode, md);
#endif
                        // IMMEDIATELY acknowledge the mode change so glider can continue
                        send_sw_on_uart0_int(1, 1);
#if DEBUG_RTT
                        printk("[RTT] Sent SW,1:1 immediately to acknowledge new mode\n");
#endif
                        
                        // Send LPM stats before stopping
                        send_sw_on_uart0_int(2, (int32_t)lpm_data_count);
#if DEBUG_RTT
                        printk("[RTT] Sent SW,2:%u (LPM count at mode change)\n", lpm_data_count);
#endif
                        send_sw_on_uart0_int(3, (int32_t)(lpm_max_depth * 100));
#if DEBUG_RTT
                        printk("[RTT] Sent SW,3:%u (max depth x100 at mode change: %d.%02d)\n", 
                               (uint32_t)(lpm_max_depth * 100), (int)lpm_max_depth, (int)((lpm_max_depth - (int)lpm_max_depth) * 100));
#endif
                        
                        // Stop the sensor
                        sensor_send_stop();
                        k_msleep(SENSOR_STOP_DELAY_MS);
                        sensor_send_stop();
                        stop_in_progress = true;
                        last_mode_before_stop = last_mode;
#if DEBUG_RTT
                        printk("[RTT] Sensor stop commands sent (mode change, x2)\n");
#endif
                        // Schedule restart after 2 seconds
                        sensor_restart_pending = true;
                        sensor_restart_time = k_uptime_get() + SENSOR_RESTART_DELAY_MS;
#if DEBUG_RTT
                        printk("[RTT] Scheduled sensor restart at t=%lld (now=%lld, wait=%d ms)\n", 
                               sensor_restart_time, k_uptime_get(), SENSOR_RESTART_DELAY_MS);
#endif
                        sample_triggered = false; // Allow new sample in new phase
                        // Reset LPM stats for next mode
                        lpm_data_count = 0;
                        lpm_max_depth = 0.0;
                        last_mode = md;
                    } else {
                        last_mode = md;
                    }
                }
            }
        }
        line_acc_reset(&acc0);
    }
}

static void on_u1_run(uint8_t b){
    if (!sensor_seen && matcher_feed(&m_sensor_hw, b)) sensor_seen = true;
    bool eol = line_acc_add(&acc1, b);
    if (eol || line_acc_stalled(&acc1)) {
        // Accept MIRROR from sensor side too, in IDLE state
        if (current_state == STATE_IDLE && strstr(acc1.buf, "$MIRROR")) {
            current_state = STATE_PASSTHROUGH;
#if DEBUG_RTT
            printk("[RTT] $MIRROR requested on U1 (entering passthrough)\n");
#endif
        }
        
        // Parse LPM_DATA messages from sensor: LPM_DATA,<depth>,<data>...
        if (strstr(acc1.buf, "LPM_DATA,")) {
            lpm_data_count++;
            // Extract depth (first number after "LPM_DATA,")
            const char *p = strstr(acc1.buf, "LPM_DATA,");
            if (p) {
                p += 9;  // Skip "LPM_DATA,"
                // Parse the depth value, stopping at the next comma
                char *endptr;
                double depth = strtod(p, &endptr);
                if (endptr != p && depth >= 0.0) {  // Valid parse and non-negative depth
                    if (depth > lpm_max_depth) {
                        lpm_max_depth = depth;
#if DEBUG_RTT
                        printk("[RTT] LPM_DATA: count=%u, depth=%d.%02d, max_depth=%d.%02d\n",
                               lpm_data_count, (int)depth, (int)((depth - (int)depth) * 100),
                               (int)lpm_max_depth, (int)((lpm_max_depth - (int)lpm_max_depth) * 100));
#endif
                    } else {
#if DEBUG_RTT
                        printk("[RTT] LPM_DATA: count=%u, depth=%d.%02d (max unchanged)\n",
                               lpm_data_count, (int)depth, (int)((depth - (int)depth) * 100));
#endif
                    }
                }
            }
        }
        
        // Check for start acknowledgment
        if (strstr(acc1.buf, "$startack;")) {
#if DEBUG_RTT
            printk("[RTT] Sensor startack received\n");
#endif
            // Forward startack to glider in case it expects it
            uart0_send_bytes((const uint8_t*)"$startack;\r\n", strlen("$startack;\r\n"));
        }
        
        // Check for stop acknowledgment
        if (strstr(acc1.buf, "$stopack;")) {
#if DEBUG_RTT
            printk("[RTT] Sensor stopack received\n");
#endif
            stop_in_progress = false;
            // Forward stopack to glider in case it expects it
            uart0_send_bytes((const uint8_t*)"$stopack;\r\n", strlen("$stopack;\r\n"));
        }
        
        line_acc_reset(&acc1);
    }
}


void main(void){
#if DEBUG_RTT
    printk("[RTT] ===== MAIN START =====\n");
    printk("[RTT] Board initialized, entering POWERUP state\n");
#endif
    bool uart0_ready = device_is_ready(uart0_dev);
    bool uart1_ready = device_is_ready(uart1_dev);
    
#if DEBUG_RTT
    printk("[RTT] UART0 (glider) ready: %s\n", uart0_ready ? "YES" : "NO");
    printk("[RTT] UART1 (sensor) ready: %s\n", uart1_ready ? "YES" : "NO");
#endif
    
    if (!uart0_ready || !uart1_ready) {
#if DEBUG_RTT
        printk("[RTT] ERROR: UARTs not ready!\n");
#endif
        while (1) { k_msleep(1000); }
    }

    // Init state
    matcher_init(&m_glider_hi, GLIDER_EXPECTED_STR);
    matcher_init(&m_glider_sd, GLIDER_SD_EXPECTED);
    matcher_init(&m_sensor_hw, SENSOR_HW_CONF_STR);
    line_acc_reset(&acc0);
    line_acc_reset(&acc1);
    
    powerup_glider_seen = false;
    powerup_sensor_seen = false;
    powerup_start_time = k_uptime_get();
    
    glider_seen = sensor_seen = glider_sd_seen = false;
    last_mode = -1;
    last_mode_before_stop = -1;
    have_epoch = false;
    last_epoch_f = 0.0;
    last_depth_f = 0.0;
    stop_in_progress = false;
    sensor_restart_pending = false;
    sample_mode = 0;
    sample_trigger_depth = 0.0;
    sample_number = 0;
    sample_below = 0;
    sample_triggered = false;

#if DEBUG_RTT
    printk("[RTT] System initialized. Starting POWERUP phase...\n");
#endif

    /* Main loop: Continuous run with state machine (POWERUP, IDLE, PASSTHROUGH, DIVE) */
    for (;;) {
        int64_t now = k_uptime_get();
        
        if (current_state == STATE_POWERUP) {
            int64_t elapsed = now - powerup_start_time;
            if (elapsed >= POWERUP_TIMEOUT_MS) {
                // Powerup timeout complete - calculate status byte
                uint8_t status_byte = 0;
                if (powerup_sensor_seen) {
                    status_byte |= (1 << STATUS_SENSOR_BIT);
                }
                if (powerup_glider_seen) {
                    status_byte |= (1 << STATUS_GLIDER_BIT);
                }
#if DEBUG_RTT
                printk("[RTT] POWERUP phase complete after %lld ms\n", elapsed);
                printk("[RTT] Status: glider=%s sensor=%s status_byte=%u\n",
                       powerup_glider_seen ? "seen" : "not seen",
                       powerup_sensor_seen ? "seen" : "not seen",
                       status_byte);
#endif
                // Send status byte to glider
                send_sw_on_uart0_int(0, (int32_t)status_byte);
#if DEBUG_RTT
                printk("[RTT] Sent SW,0:%u to glider\n", status_byte);
#endif
                // Transition to IDLE state
                current_state = STATE_IDLE;
                glider_seen = powerup_glider_seen;
                sensor_seen = powerup_sensor_seen;
#if DEBUG_RTT
                printk("[RTT] Transitioning to IDLE state\n");
#endif
            } else {
                // Still in powerup phase - monitor for HI/SD/HW_CONF
                poll_drain_uart(uart0_dev, on_u0_powerup);
                poll_drain_uart(uart1_dev, on_u1_powerup);
                k_sleep(K_MSEC(2));
            }
        } else if (current_state == STATE_PASSTHROUGH) {
#if DEBUG_RTT
            printk("[RTT] Entering PASSTHROUGH state\n");
#endif
            // Drain any stale data from both UARTs before starting passthrough
            unsigned char dummy;
            int drain_count_u0 = 0, drain_count_u1 = 0;
            while (uart_poll_in(uart0_dev, &dummy) == 0) { drain_count_u0++; }
            while (uart_poll_in(uart1_dev, &dummy) == 0) { drain_count_u1++; }
#if DEBUG_RTT
            printk("[RTT] UARTs drained (U0=%d U1=%d bytes)\n", drain_count_u0, drain_count_u1);
#endif
            
            // Send a line on UART0 to confirm entry
            static const char passthrough_msg[] = "PASSTHROUGH_ACTIVE\r\n";
            uart0_send_bytes((const uint8_t*)passthrough_msg, strlen(passthrough_msg));
            
            int64_t last_byte_time = k_uptime_get();
            int64_t last_diag_time = k_uptime_get();
            int64_t last_u1_rx_time = 0;
            uint32_t passthrough_bytes = 0;
            uint32_t u0_to_u1_total = 0;
            uint32_t u1_to_u0_total = 0;
            uint32_t u1_rx_count = 0;
            while (current_state == STATE_PASSTHROUGH) {
                int64_t now = k_uptime_get();
                
                // Try U1->U0 first, then U0->U1
                uint32_t b = forward_all(uart1_dev, uart0_dev, "U1->U0");
                uint32_t a = forward_all(uart0_dev, uart1_dev, "U0->U1");
                
#if DEBUG_RTT
                if (a) { 
                    passthrough_bytes += a; 
                    u0_to_u1_total += a; 
                    last_byte_time = now;
                    printk("[RTT] U0->U1: %u bytes\n", a);
                }
                if (b) { 
                    passthrough_bytes += b; 
                    u1_to_u0_total += b; 
                    last_byte_time = now;
                    last_u1_rx_time = now;
                    u1_rx_count++;
                    printk("[RTT] U1->U0: %u bytes (event #%u)\n", b, u1_rx_count);
                }
                if ((now - last_diag_time) > 5000) {
                    int64_t since_u1_rx = last_u1_rx_time ? (now - last_u1_rx_time) : -1;
                    printk("[RTT] Stats: U0->U1=%u, U1->U0=%u, last_U1_RX=%lld ms ago\n", 
                           u0_to_u1_total, u1_to_u0_total, since_u1_rx);
                    last_diag_time = now;
                }
#endif
                // No sleep - run as fast as possible for low latency
            }
            
            // Exited PASSTHROUGH state (via $QUIT or other means)
#if DEBUG_RTT
            printk("[RTT] Exiting PASSTHROUGH state, resetting to IDLE\n");
#endif
            
            // Send message to UART0 to notify user
            static const char idle_msg[] = "Back to idle\r\n";
            uart0_send_bytes((const uint8_t*)idle_msg, strlen(idle_msg));
            
            // Reset state for next cycle
            glider_seen = false;
            glider_sd_seen = false;
            sensor_seen = false;
            last_mode = -1;
            have_epoch = false;
            sample_triggered = false;
            line_acc_reset(&acc0);
            line_acc_reset(&acc1);
        } else if (current_state == STATE_DIVE) {
#if DEBUG_RTT
            static int dive_loop_counter = 0;
            if (dive_loop_counter++ % 500 == 0) {
                printk("[RTT] DIVE loop iteration (counter=%d, restart_pending=%d, acc0.len=%zu, acc1.len=%zu)\n", 
                       dive_loop_counter, sensor_restart_pending, acc0.len, acc1.len);
            }
#endif
            // Check for pending sensor restart
            if (sensor_restart_pending) {
                int64_t time_until_restart = sensor_restart_time - now;
#if DEBUG_RTT
                if (time_until_restart <= 0) {
                    printk("[RTT] Restart timeout reached (waited %lld ms total)\n", sensor_restart_time - (sensor_restart_time - SENSOR_RESTART_DELAY_MS));
                }
#endif
                if (now >= sensor_restart_time) {
#if DEBUG_RTT
                    printk("[RTT] Restarting sensor after mode change...\n");
#endif
                    if (have_epoch && last_epoch_f > 0.0) {
                        sensor_send_start_with_time(last_epoch_f);
                        send_sw_on_uart0_int(1, 1);
#if DEBUG_RTT
                        printk("[RTT] Sensor restarted with time\n");
#endif
                    } else {
                        sensor_send_start_no_time();
                        send_sw_on_uart0_int(1, -1);
#if DEBUG_RTT
                        printk("[RTT] Sensor restarted without time\n");
#endif
                    }
                    sensor_restart_pending = false;
                    stop_in_progress = false;
                    sample_triggered = false;
                    // Reset line accumulators to clear any stale data before restart
                    line_acc_reset(&acc0);
                    line_acc_reset(&acc1);
                    // Proactively drain UART RX FIFOs to clear any stale bytes
                    {
                        unsigned char dummy;
                        int drain_u0 = 0, drain_u1 = 0;
                        while (uart_poll_in(uart0_dev, &dummy) == 0) { drain_u0++; }
                        while (uart_poll_in(uart1_dev, &dummy) == 0) { drain_u1++; }
#if DEBUG_RTT
                        printk("[RTT] UARTs drained after restart (U0=%d U1=%d bytes)\n", drain_u0, drain_u1);
#endif
                    }
                    // Small grace period to let glider/sensor settle post-restart
                    k_msleep(100);
                    // CRITICAL: Reset mode tracking so first SD message is properly recognized
                    last_mode = -1;
#if DEBUG_RTT
                    printk("[RTT] Restart complete: pending=%d, flags cleared, line buffers reset, last_mode reset\n", sensor_restart_pending);
#endif
                    // Resend status byte to glider to nudge protocol continuation
                    {
                        uint8_t status_byte = 0;
                        if (sensor_seen) { status_byte |= (1 << STATUS_SENSOR_BIT); }
                        if (glider_seen || glider_sd_seen) { status_byte |= (1 << STATUS_GLIDER_BIT); }
                        send_sw_on_uart0_int(0, (int32_t)status_byte);
#if DEBUG_RTT
                        printk("[RTT] Sent SW,0:%u after restart (glider_seen=%d sensor_seen=%d)\n",
                               status_byte, glider_seen || glider_sd_seen, sensor_seen);
#endif
                    }
#if DEBUG_RTT
                    printk("[RTT] @@@ RESTART COMPLETE - RESUMING DATA POLLING @@@\n");
#endif
                }
            }
            
            // In DIVE state, process messages and handle state
            poll_drain_uart(uart0_dev, on_u0_run);
            poll_drain_uart(uart1_dev, on_u1_run);
            k_sleep(K_MSEC(2));
        } else {
            // STATE_IDLE - wait for HI/SD or MIRROR
            poll_drain_uart(uart0_dev, on_u0_run);
            poll_drain_uart(uart1_dev, on_u1_run);
            k_sleep(K_MSEC(2));
        }
    }
}


