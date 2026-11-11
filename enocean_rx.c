/*
 * EnOcean RX Test - phase 1
 *
 * Goal: find out whether the Flipper's CC1101 can resolve the 125 kbit/s ASK
 * signal of EnOcean ERP1 (868.3 MHz, ~8 us per bit) at all.
 *
 * The stock Sub-GHz app uses presets with ~3.8 kBaud data rate, which smears
 * 8 us pulses into 400-800 us blobs (see docs/capture-analysis.md). This app
 * loads a custom CC1101 register set (125 kBaud, 650 kHz RX bandwidth, OOK),
 * grabs raw edges straight from the HAL (microsecond resolution, no worker
 * filter) and shows simple pulse statistics per burst. Every burst is also
 * appended to the SD card in Flipper RAW_Data notation so it can be analysed
 * on a PC.
 *
 * STATUS: untested on hardware. Register values follow the stock Flipper OOK
 * presets with MDMCFG4/MDMCFG3 changed for 125 kBaud.
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "EnOceanRx"

#define ENOCEAN_FREQUENCY_HZ 868300000UL
#define BURST_GAP_US 400U /* a pause this long ends a burst */
#define MIN_EDGES_FOR_BURST 8U /* ignore tiny noise blips */
#define MAX_EDGES 768U /* edges kept per burst */
#define EDGE_STREAM_BYTES 8192U /* ISR -> main thread buffer */

#define LOG_BASE_DIR "/ext/apps_data"
#define LOG_DIR "/ext/apps_data/enocean_rx"
#define LOG_PATH "/ext/apps_data/enocean_rx/bursts.sub.txt"

/*
 * CC1101 register preset: {register, value} pairs, {0, 0} terminator, then the
 * 8 byte PA table.
 *
 *  MDMCFG4 = 0x1C : CHANBW_E=0, CHANBW_M=1 -> 26 MHz / (8*5*1) = 650 kHz, DRATE_E = 12
 *  MDMCFG3 = 0x3B : DRATE_M = 59
 *  => data rate = (256+59) * 2^12 / 2^28 * 26 MHz ~= 124.97 kBaud
 *  MDMCFG2 = 0x30 : ASK/OOK, no Manchester, no sync word
 */
static const uint8_t enocean_cc1101_preset_base[] = {
    0x02, 0x0D, /* IOCFG0: GDO0 = async serial data output */
    0x03, 0x07, /* FIFOTHR */
    0x08, 0x32, /* PKTCTRL0: async serial mode, infinite packet length */
    0x0B, 0x06, /* FSCTRL1 */
    0x10, 0x1C, /* MDMCFG4 */
    0x11, 0x3B, /* MDMCFG3 */
    0x12, 0x30, /* MDMCFG2 */
    0x13, 0x00, /* MDMCFG1 */
    0x14, 0x00, /* MDMCFG0 */
    0x18, 0x18, /* MCSM0 */
    0x19, 0x18, /* FOCCFG */
    0x1B, 0x07, /* AGCCTRL2 */
    0x1C, 0x00, /* AGCCTRL1 */
    0x1D, 0x91, /* AGCCTRL0 */
    0x20, 0xFB, /* WORCTRL */
    0x21, 0xB6, /* FREND1 */
    0x22, 0x11, /* FREND0 (OOK: PA table index 0/1) */
    0x00, 0x00, /* end of register list */
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table */
};

/* Byte offsets of the values inside the preset array above. */
#define PRESET_OFF_MDMCFG4 9
#define PRESET_OFF_MDMCFG3 11
#define PRESET_OFF_MDMCFG2 13

typedef struct {
    const char* name;
    uint8_t mdmcfg4; /* CHANBW_E/M + DRATE_E */
    uint8_t mdmcfg3; /* DRATE_M */
    uint8_t mdmcfg2; /* bit7 = DEM_DCFILT_OFF, bits 6:4 = 011 (ASK/OOK) */
} EnOceanPresetVariant;

/*
 * Variants to compare on the device (Left/Right cycles them):
 *  - RX bandwidth 650 kHz (0x1x) vs 812 kHz (0x0x)
 *  - digital DC blocking filter on (0x30) vs off (0xB0); with long runs of equal
 *    levels the DC filter can distort the slicer output
 *  - data rate ~125 kBaud (DRATE_E=12) vs ~250 kBaud (DRATE_E=13)
 */
static const EnOceanPresetVariant enocean_variants[] = {
    {"650k", 0x1C, 0x3B, 0x30},
    {"812k", 0x0C, 0x3B, 0x30},
    {"650k DCoff", 0x1C, 0x3B, 0xB0},
    {"812k DCoff", 0x0C, 0x3B, 0xB0},
    {"812k 250k DCoff", 0x0D, 0x3B, 0xB0},
};
#define ENOCEAN_VARIANT_COUNT (sizeof(enocean_variants) / sizeof(enocean_variants[0]))

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    FuriStreamBuffer* edge_stream;

    /* burst assembly (main thread only) */
    uint32_t edges[MAX_EDGES]; /* bit 31 = level, bits 0..30 = duration in us */
    uint32_t edge_count;

    /* statistics of the last finished burst */
    uint32_t burst_total;
    uint32_t last_edges;
    uint32_t last_total_us;
    uint32_t last_min_us;
    uint32_t last_max_us;
    uint32_t last_le12; /* pulses <= 12 us   (about 1 bit)  */
    uint32_t last_le28; /* pulses 13..28 us  (about 2-3 bits) */
    uint32_t last_gt28; /* pulses > 28 us */

    float rssi;
    bool logging;
    bool log_ok;
    volatile uint32_t overruns;

    uint8_t variant; /* index into enocean_variants */
    uint8_t preset_buf[sizeof(enocean_cc1101_preset_base)];
} EnOceanApp;

/* ---------- ISR side ---------- */

static void enocean_capture_cb(bool level, uint32_t duration, void* context) {
    EnOceanApp* app = context;
    uint32_t packed = ((uint32_t)level << 31) | (duration & 0x7FFFFFFFU);
    if(furi_stream_buffer_send(app->edge_stream, &packed, sizeof(packed), 0) != sizeof(packed)) {
        app->overruns++;
    }
}

/* ---------- radio ---------- */

static void enocean_radio_start(EnOceanApp* app) {
    const EnOceanPresetVariant* v = &enocean_variants[app->variant];
    memcpy(app->preset_buf, enocean_cc1101_preset_base, sizeof(app->preset_buf));
    app->preset_buf[PRESET_OFF_MDMCFG4] = v->mdmcfg4;
    app->preset_buf[PRESET_OFF_MDMCFG3] = v->mdmcfg3;
    app->preset_buf[PRESET_OFF_MDMCFG2] = v->mdmcfg2;

    furi_hal_subghz_reset();
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(app->preset_buf);
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_subghz_set_frequency_and_path(ENOCEAN_FREQUENCY_HZ);
    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();
    furi_hal_subghz_start_async_rx(enocean_capture_cb, app);
}

static void enocean_radio_stop(void) {
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

/* ---------- logging ---------- */

static void enocean_log_burst(EnOceanApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, LOG_BASE_DIR);
    storage_simply_mkdir(storage, LOG_DIR);

    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        FuriString* line = furi_string_alloc();
        furi_string_printf(
            line,
            "# burst %lu preset=%u(%s) edges=%lu total_us=%lu\nRAW_Data:",
            (unsigned long)app->burst_total,
            (unsigned)app->variant,
            enocean_variants[app->variant].name,
            (unsigned long)app->edge_count,
            (unsigned long)app->last_total_us);
        for(uint32_t i = 0; i < app->edge_count; i++) {
            bool level = (app->edges[i] >> 31) != 0;
            int32_t dur = (int32_t)(app->edges[i] & 0x7FFFFFFFU);
            furi_string_cat_printf(line, " %ld", (long)(level ? dur : -dur));
        }
        furi_string_cat(line, "\n");
        ok = storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line)) ==
             furi_string_size(line);
        furi_string_free(line);
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    app->log_ok = ok;
}

/* ---------- burst handling ---------- */

static void enocean_finish_burst(EnOceanApp* app) {
    if(app->edge_count >= MIN_EDGES_FOR_BURST) {
        uint32_t total = 0, min_us = UINT32_MAX, max_us = 0;
        uint32_t le12 = 0, le28 = 0, gt28 = 0;
        for(uint32_t i = 0; i < app->edge_count; i++) {
            uint32_t d = app->edges[i] & 0x7FFFFFFFU;
            total += d;
            if(d < min_us) min_us = d;
            if(d > max_us) max_us = d;
            if(d <= 12)
                le12++;
            else if(d <= 28)
                le28++;
            else
                gt28++;
        }

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->burst_total++;
        app->last_edges = app->edge_count;
        app->last_total_us = total;
        app->last_min_us = min_us;
        app->last_max_us = max_us;
        app->last_le12 = le12;
        app->last_le28 = le28;
        app->last_gt28 = gt28;
        furi_mutex_release(app->mutex);

        if(app->logging) enocean_log_burst(app);
    }
    app->edge_count = 0;
}

static void enocean_process_edge(EnOceanApp* app, uint32_t packed) {
    uint32_t dur = packed & 0x7FFFFFFFU;
    if(dur >= BURST_GAP_US) {
        enocean_finish_burst(app);
        return;
    }
    if(app->edge_count < MAX_EDGES) {
        app->edges[app->edge_count++] = packed;
    }
}

/* ---------- UI ---------- */

static void enocean_draw_cb(Canvas* canvas, void* context) {
    EnOceanApp* app = context;
    char buf[48];

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "P%u %s", (unsigned)app->variant, enocean_variants[app->variant].name);
    canvas_draw_str(canvas, 2, 10, buf);

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        buf,
        sizeof(buf),
        "Bursts:%lu  Edges:%lu",
        (unsigned long)app->burst_total,
        (unsigned long)app->last_edges);
    canvas_draw_str(canvas, 2, 21, buf);

    if(app->burst_total > 0) {
        snprintf(
            buf,
            sizeof(buf),
            "len:%luus min:%lu max:%lu",
            (unsigned long)app->last_total_us,
            (unsigned long)app->last_min_us,
            (unsigned long)app->last_max_us);
        canvas_draw_str(canvas, 2, 31, buf);
        snprintf(
            buf,
            sizeof(buf),
            "<=12:%lu 13-28:%lu >28:%lu",
            (unsigned long)app->last_le12,
            (unsigned long)app->last_le28,
            (unsigned long)app->last_gt28);
        canvas_draw_str(canvas, 2, 41, buf);
    } else {
        canvas_draw_str(canvas, 2, 31, "Press the EnOcean button...");
    }

    snprintf(
        buf,
        sizeof(buf),
        "RSSI:%d dBm  ovr:%lu",
        (int)app->rssi,
        (unsigned long)app->overruns);
    canvas_draw_str(canvas, 2, 51, buf);

    snprintf(
        buf,
        sizeof(buf),
        "OK:log %s%s  </>:preset",
        app->logging ? "ON" : "OFF",
        (app->logging && !app->log_ok && app->burst_total > 0) ? "!" : "");
    canvas_draw_str(canvas, 2, 62, buf);

    furi_mutex_release(app->mutex);
}

static void enocean_input_cb(InputEvent* event, void* context) {
    EnOceanApp* app = context;
    furi_message_queue_put(app->input_queue, event, 0);
}

/* ---------- entry ---------- */

int32_t enocean_rx_app(void* p) {
    UNUSED(p);

    EnOceanApp* app = malloc(sizeof(EnOceanApp));
    memset(app, 0, sizeof(EnOceanApp));
    app->logging = true;
    app->log_ok = true;
    app->rssi = -127.0f;
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->edge_stream = furi_stream_buffer_alloc(EDGE_STREAM_BYTES, sizeof(uint32_t));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, enocean_draw_cb, app);
    view_port_input_callback_set(app->view_port, enocean_input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    enocean_radio_start(app);
    FURI_LOG_I(TAG, "RX started at %lu Hz", (unsigned long)ENOCEAN_FREQUENCY_HZ);

    bool running = true;
    uint32_t last_rssi_tick = 0;
    while(running) {
        /* drain edges captured by the ISR */
        uint32_t packed;
        while(furi_stream_buffer_receive(app->edge_stream, &packed, sizeof(packed), 0) ==
              sizeof(packed)) {
            enocean_process_edge(app, packed);
        }

        InputEvent event;
        if(furi_message_queue_get(app->input_queue, &event, 10) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                if(event.key == InputKeyBack) {
                    running = false;
                } else if(event.key == InputKeyOk) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    app->logging = !app->logging;
                    furi_mutex_release(app->mutex);
                } else if(event.key == InputKeyLeft || event.key == InputKeyRight) {
                    /* switch CC1101 variant: restart the radio with new registers */
                    enocean_radio_stop();
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    if(event.key == InputKeyRight) {
                        app->variant = (app->variant + 1) % ENOCEAN_VARIANT_COUNT;
                    } else {
                        app->variant = (app->variant + ENOCEAN_VARIANT_COUNT - 1) %
                                       ENOCEAN_VARIANT_COUNT;
                    }
                    app->edge_count = 0;
                    app->overruns = 0;
                    furi_mutex_release(app->mutex);
                    while(furi_stream_buffer_receive(
                              app->edge_stream, &packed, sizeof(packed), 0) == sizeof(packed)) {
                    }
                    enocean_radio_start(app);
                }
            }
        }

        uint32_t now = furi_get_tick();
        if(now - last_rssi_tick > 200) {
            last_rssi_tick = now;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->rssi = furi_hal_subghz_get_rssi();
            furi_mutex_release(app->mutex);
        }
        view_port_update(app->view_port);
    }

    enocean_radio_stop();

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_stream_buffer_free(app->edge_stream);
    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->input_queue);
    free(app);
    return 0;
}
