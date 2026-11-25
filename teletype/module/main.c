#include <ctype.h>
#include <stdio.h>
#include <string.h>

// asf
#include "compiler.h"
#include "delay.h"
#include "gpio.h"
#include "intc.h"
#include "pm.h"
#include "preprocessor.h"
#include "print_funcs.h"
#include "spi.h"
#include "twi.h"
#include "sysclk.h"
#include "usb_protocol_hid.h"

// system
#include "adc.h"
#include "cdc.h"
#include "events.h"
#include "font.h"
#include "hid.h"
#include "i2c.h"
#include "init_common.h"
#include "init_teletype.h"
#include "interrupts.h"
#include "kbd.h"
#include "midi.h"
#include "midi_common.h"
#include "monome.h"
#include "region.h"
#include "screen.h"
#include "timers.h"
#include "util.h"

// this
#include "chaos.h"
#include "conf_board.h"
#include "edit_mode.h"
#include "flash.h"
#include "globals.h"
#include "grid.h"
#include "help_mode.h"
#include "keyboard_helper.h"
#include "live_mode.h"
#include "pattern_mode.h"
#include "preset_r_mode.h"
#include "preset_w_mode.h"
#include "seaflake/sf.h"
#include "seaflake_mode.h"
#include "sf_ui.h"
#include "seaflake/sf_ii.h"
#include "dream.h"
#include "teletype.h"
#include "teletype_io.h"
#include "usb_disk_mode.h"

#ifdef TELETYPE_PROFILE
#include "profile.h"

profile_t prof_Script[TOTAL_SCRIPT_COUNT], prof_Delay[DELAY_SIZE], prof_CV,
    prof_ADC, prof_ScreenRefresh;

void tele_profile_script(size_t s) {
    profile_update(&prof_Script[s]);
}

void tele_profile_delay(uint8_t d) {
    profile_update(&prof_Delay[d]);
}

#endif

////////////////////////////////////////////////////////////////////////////////
// constants

#define RATE_CLOCK 10
#define RATE_CV 6
// the screensaver: after 5 idle minutes the dream (dream.c), until a key.
// (It used to go dark after 50 more minutes; the user wants the dream only.
// The dream drifts, so nothing stays lit in one place.)
#define SS_TIMEOUT (5 /* minutes */ * 60 * 100)


////////////////////////////////////////////////////////////////////////////////
// globals (defined in globals.h)

scene_state_t scene_state;
char scene_text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
uint8_t preset_select;
region line[8] = { { .w = 128, .h = 8, .x = 0, .y = 0 },
                   { .w = 128, .h = 8, .x = 0, .y = 8 },
                   { .w = 128, .h = 8, .x = 0, .y = 16 },
                   { .w = 128, .h = 8, .x = 0, .y = 24 },
                   { .w = 128, .h = 8, .x = 0, .y = 32 },
                   { .w = 128, .h = 8, .x = 0, .y = 40 },
                   { .w = 128, .h = 8, .x = 0, .y = 48 },
                   { .w = 128, .h = 8, .x = 0, .y = 56 } };
char copy_buffer[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
uint8_t copy_buffer_len = 0;


////////////////////////////////////////////////////////////////////////////////
// locals

static device_config_t device_config;
// the screen on show lives in sf_ui.c (Orca Seaflake or its help)
static uint32_t ss_counter = 0;
// the dream picks one of its four at random when it starts (dream.c)
uint32_t dream_entropy(void) { return get_ticks(); }
static u8 grid_connected = 0;
static u8 grid_control_mode = 0;
static u8 midi_clock_counter = 0;

static uint16_t adc[4];

typedef struct {
    uint16_t now;
    uint16_t off;
    uint16_t target;
    uint16_t slew;
    uint16_t step;
    int32_t delta;
    uint32_t a;
} aout_t;

static u8 ignore_front_press = 0;
static aout_t aout[4];
static bool metro_timer_enabled;
static uint8_t mod_key = 0, hold_key, hold_key_count = 0;
static uint64_t last_adc_tick = 0;
static midi_behavior_t midi_behavior;

// timers
static softTimer_t clockTimer = { .next = NULL, .prev = NULL };
static softTimer_t refreshTimer = { .next = NULL, .prev = NULL };
static softTimer_t keyTimer = { .next = NULL, .prev = NULL };
static softTimer_t cvTimer = { .next = NULL, .prev = NULL };
static softTimer_t adcTimer = { .next = NULL, .prev = NULL };
static softTimer_t hidTimer = { .next = NULL, .prev = NULL };
static softTimer_t metroTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomePollTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomeRefreshTimer = { .next = NULL, .prev = NULL };
static softTimer_t gridFaderTimer = { .next = NULL, .prev = NULL };
static softTimer_t midiScriptTimer = { .next = NULL, .prev = NULL };
static softTimer_t seaflakeTimer = { .next = NULL, .prev = NULL };
static softTimer_t trPulseTimer[TR_COUNT];


////////////////////////////////////////////////////////////////////////////////
// prototypes

// timer callback prototypes
static void cvTimer_callback(void* o);
static void clockTimer_callback(void* o);
static void refreshTimer_callback(void* o);
static void keyTimer_callback(void* o);
static void adcTimer_callback(void* o);
static void hidTimer_callback(void* o);
static void metroTimer_callback(void* o);
static void monome_poll_timer_callback(void* obj);
static void monome_refresh_timer_callback(void* obj);
static void grid_fader_timer_callback(void* obj);
static void midiScriptTimer_callback(void* obj);
static void trPulseTimer_callback(void* obj);
static void seaflakeTimer_callback(void* obj);

// event handler prototypes
static void handler_None(int32_t data);
static void handler_Front(int32_t data);
static void handler_PollADC(int32_t data);
static void handler_KeyTimer(int32_t data);
static void handler_HidConnect(int32_t data);
static void handler_HidDisconnect(int32_t data);
static void handler_HidTimer(int32_t data);
static void handler_MscConnect(int32_t data);
static void handler_Trigger(int32_t data);
static void handler_ScreenRefresh(int32_t data);
static void handler_EventTimer(int32_t data);
static void handler_AppCustom(int32_t data);

// event queue
static void empty_event_handlers(void);
void assign_main_event_handlers(void);
static void assign_msc_event_handlers(void);
void check_events(void);

// key handling
void process_keypress(uint8_t key, uint8_t mod_key, bool is_held_key,
                      bool is_release);

// start/stop monome polling/refresh timers
void timers_set_monome(void);
void timers_unset_monome(void);

// other
static void render_init(void);
static void exit_screensaver(void);
static void update_device_config(u8 refresh);

////////////////////////////////////////////////////////////////////////////
// I2C bus setup (replaces libavr32's init_i2c_leader for this firmware)
//
// Stock init_i2c_leader() tells the TWI driver the peripheral clock is FOSC0
// (12 MHz) while the Teletype runs PBA from PLL0 at 12 MHz x 5 = 60 MHz
// (libavr32/conf/conf_clock.h), so its "100 kHz" divider gives an SCL of about
// 60e6 / ((56 + 4) * 2) = 500 kHz: past I2C fast mode, and fragile with weak
// pull-ups or long ribbons. Here: the real clock, so SCL is 100 kHz; the
// chip's own pull-ups on SDA and SCL (weak, but enough for a short bus with
// none); a check of each line at rest; and the standard recovery (9 clocks
// and a STOP) when a follower holds SDA low.

#define BUS_SDA AVR32_TWI_SDA_0_0_PIN
#define BUS_SCL AVR32_TWI_SCL_0_0_PIN

// open drain by hand: low = drive 0, high = let go (input)
static void bus_low(uint32_t pin) { gpio_clr_gpio_pin(pin); }
static void bus_let_go(uint32_t pin) { gpio_enable_gpio_pin(pin); }

// does the line rise by itself with the chip's pull-up off? discharge it,
// let go, wait 20 us (a 10k pull-up on a short ribbon rises in about 1 us)
static bool bus_rises_alone(uint32_t pin) {
    gpio_disable_pin_pull_up(pin);
    bus_low(pin);
    delay_us(5);
    bus_let_go(pin);
    delay_us(20);
    return gpio_get_pin_value(pin);
}

static void bus_twi_start(void) {
    static const gpio_map_t map = {
        { AVR32_TWI_SDA_0_0_PIN, AVR32_TWI_SDA_0_0_FUNCTION },
        { AVR32_TWI_SCL_0_0_PIN, AVR32_TWI_SCL_0_0_FUNCTION }
    };
    gpio_enable_module(map, sizeof(map) / sizeof(map[0]));
    twi_options_t opt;
    opt.pba_hz = sysclk_get_pba_hz();  // 60 MHz, not FOSC0
    opt.speed = 100000;
    opt.chip = 0x50;
    twi_master_abort(TWI);  // (clears a busy flag left from before)
    twi_master_init(TWI, &opt);
}

static void sf_bus_init(void) {
    memset(&sf_bus, 0, sizeof(sf_bus));
    if (bus_rises_alone(BUS_SDA)) sf_bus.ext_pullup |= 1;
    if (bus_rises_alone(BUS_SCL)) sf_bus.ext_pullup |= 2;

    // the chip's pull-ups on (they stay on in TWI mode)
    gpio_enable_pin_pull_up(BUS_SDA);
    gpio_enable_pin_pull_up(BUS_SCL);
    sf_bus.internal_pullup = 1;
    bus_let_go(BUS_SDA);
    bus_let_go(BUS_SCL);
    delay_us(50);
    if (!gpio_get_pin_value(BUS_SDA)) sf_bus.stuck |= 1;
    if (!gpio_get_pin_value(BUS_SCL)) sf_bus.stuck |= 2;

    // SDA held low by a follower stuck mid-byte: clock it out, then a STOP
    if ((sf_bus.stuck & 1) && !(sf_bus.stuck & 2)) {
        for (uint8_t i = 0; i < 9 && !gpio_get_pin_value(BUS_SDA); i++) {
            bus_low(BUS_SCL);
            delay_us(5);
            bus_let_go(BUS_SCL);
            delay_us(5);
        }
        bus_low(BUS_SDA);
        delay_us(5);
        bus_let_go(BUS_SDA);  // SDA rising while SCL is high = STOP
        delay_us(5);
        if (gpio_get_pin_value(BUS_SDA)) sf_bus.recovered = 1;
    }

    bus_twi_start();
    sf_bus.khz = 100;
}

// a transfer that timed out (TWI_BUSY): abort it, free the bus if a follower
// still holds SDA (9 clocks + STOP), start the TWI again. Counted in sf_bus.
static void sf_bus_restart(void) {
    twi_master_abort(TWI);
    bus_let_go(BUS_SDA);
    bus_let_go(BUS_SCL);
    delay_us(10);
    for (uint8_t i = 0; i < 9 && !gpio_get_pin_value(BUS_SDA); i++) {
        bus_low(BUS_SCL);
        delay_us(5);
        bus_let_go(BUS_SCL);
        delay_us(5);
    }
    bus_low(BUS_SDA);
    delay_us(5);
    bus_let_go(BUS_SDA);
    delay_us(5);
    bus_twi_start();
    if (sf_bus.restarts < 255) sf_bus.restarts++;
}

void initialize_module(void);


////////////////////////////////////////////////////////////////////////////////
// timer callbacks

void cvTimer_callback(void* o) {
#ifdef TELETYPE_PROFILE
    profile_update(&prof_CV);
#endif
    bool updated = false;
    bool slewing = false;

    for (size_t i = 0; i < 4; i++) {
        if (aout[i].step) {
            aout[i].step--;

            if (aout[i].step == 0) { aout[i].now = aout[i].target; }
            else {
                aout[i].a += aout[i].delta;
                aout[i].now = aout[i].a >> 16;
                slewing = true;
            }

            updated = true;
        }
    }

    set_slew_icon(slewing);

    if (updated) {
        uint16_t output[4];

        for (uint8_t hardware_index = 0; hardware_index < 4; hardware_index++) {
            uint8_t software_index =
                device_config.flip ? 3 - hardware_index : hardware_index;

            // With default CV.CAL settings, skip calibration math
            if (scene_state.cal.cv_scale[hardware_index].m == 1 &&
                scene_state.cal.cv_scale[hardware_index].b == 0) {
                output[hardware_index] = aout[software_index].now >> 2;
            }
            else {
                // apply calibration via fixed-point linear scaling
                int32_t p = aout[software_index].now;
                p = p * scene_state.cal.cv_scale[hardware_index].m +
                    scene_state.cal.cv_scale[hardware_index].b;

                output[hardware_index] = (p >= 0) ? FROM_Q15(p) : 0;
                if (output[hardware_index] > 16383) {
                    output[hardware_index] = 16383;
                }
                output[hardware_index] = output[hardware_index] >> 2;
            }
        }

        spi_selectChip(DAC_SPI, DAC_SPI_NPCS);
        spi_write(DAC_SPI, 0x31);
        spi_write(DAC_SPI, output[2] >> 4);
        spi_write(DAC_SPI, output[2] << 4);
        spi_write(DAC_SPI, 0x31);
        spi_write(DAC_SPI, output[0] >> 4);
        spi_write(DAC_SPI, output[0] << 4);
        spi_unselectChip(DAC_SPI, DAC_SPI_NPCS);

        spi_selectChip(DAC_SPI, DAC_SPI_NPCS);
        spi_write(DAC_SPI, 0x38);
        spi_write(DAC_SPI, output[3] >> 4);
        spi_write(DAC_SPI, output[3] << 4);
        spi_write(DAC_SPI, 0x38);
        spi_write(DAC_SPI, output[1] >> 4);
        spi_write(DAC_SPI, output[1] << 4);
        spi_unselectChip(DAC_SPI, DAC_SPI_NPCS);
    }
#ifdef TELETYPE_PROFILE
    profile_update(&prof_CV);
#endif
}

void clockTimer_callback(void* o) {
    event_t e = { .type = kEventTimer, .data = 0 };
    event_post(&e);
}

void refreshTimer_callback(void* o) {
    event_t e = { .type = kEventScreenRefresh, .data = 0 };
    event_post(&e);
}

void keyTimer_callback(void* o) {
    event_t e = { .type = kEventKeyTimer, .data = 0 };
    event_post(&e);
}

void adcTimer_callback(void* o) {
    event_t e = { .type = kEventPollADC, .data = 0 };
    event_post(&e);
}

void hidTimer_callback(void* o) {
    event_t e = { .type = kEventHidTimer, .data = 0 };
    event_post(&e);
}

void metroTimer_callback(void* o) {
    event_t e = { .type = kEventAppCustom, .data = 0 };
    event_post(&e);
}

// Orca Seaflake: checked every ms, posts an event only when a frame or a
// timed note is due
void seaflakeTimer_callback(void* o) {
    if (seaflake_due(get_ticks())) {
        event_t e = { .type = kEventAppCustom, .data = 1 };
        event_post(&e);
    }
}

// monome polling callback
static void monome_poll_timer_callback(void* obj) {
    // asynchronous, non-blocking read
    // UHC callback spawns appropriate events
    serial_read();
}

// monome refresh callback
static void monome_refresh_timer_callback(void* obj) {
    if (grid_connected && scene_state.grid.grid_dirty) {
        static event_t e;
        e.type = kEventMonomeRefresh;
        event_post(&e);
    }
}

// monome: start polling
void timers_set_monome(void) {
    timer_add(&monomePollTimer, 20, &monome_poll_timer_callback, NULL);
    timer_add(&monomeRefreshTimer, 30, &monome_refresh_timer_callback, NULL);
}

// monome stop polling
void timers_unset_monome(void) {
    timer_remove(&monomePollTimer);
    timer_remove(&monomeRefreshTimer);
}

void grid_fader_timer_callback(void* o) {
    grid_process_fader_slew(&scene_state);
}

void midiScriptTimer_callback(void* obj) {
    u8 executed[EDITABLE_SCRIPT_COUNT];
    for (uint8_t i = 0; i < EDITABLE_SCRIPT_COUNT; i++) executed[i] = 0;

    if (scene_state.midi.on_count && scene_state.midi.on_script >= 0 &&
        scene_state.midi.on_script < EDITABLE_SCRIPT_COUNT) {
        run_script(&scene_state, scene_state.midi.on_script);
        executed[scene_state.midi.on_script] = 1;
    }

    if (scene_state.midi.off_count && scene_state.midi.off_script >= 0 &&
        scene_state.midi.off_script < EDITABLE_SCRIPT_COUNT) {
        if (!executed[scene_state.midi.off_script])
            run_script(&scene_state, scene_state.midi.off_script);
        executed[scene_state.midi.off_script] = 1;
    }

    if (scene_state.midi.cc_count && scene_state.midi.cc_script >= 0 &&
        scene_state.midi.cc_script < EDITABLE_SCRIPT_COUNT) {
        if (!executed[scene_state.midi.cc_script])
            run_script(&scene_state, scene_state.midi.cc_script);
    }

    scene_state.midi.on_count = 0;
    scene_state.midi.off_count = 0;
    scene_state.midi.cc_count = 0;
}

////////////////////////////////////////////////////////////////////////////////
// event handlers

void handler_None(int32_t data) {}

void handler_Front(int32_t data) {
    if (ss_counter >= SS_TIMEOUT) {
        exit_screensaver();
        return;
    }
    ss_counter = 0;

    // press (data 0). The first-start "SCENES WILL BE OVERWRITTEN" press is
    // not a command. A monome grid no longer toggles Teletype's grid
    // control mode: that is a stock Teletype screen.
    if (data == 0) {
        if (ignore_front_press) {
            ignore_front_press = 0;
            return;
        }
        sf_ui_front();
    }
}


void handler_PollADC(int32_t data) {
#ifdef TELETYPE_PROFILE
    profile_update(&prof_ADC);
#endif
    static int16_t last_knob = 0;

    adc_convert(&adc);

    ss_set_in(&scene_state, adc[0] << 2);

    if (ss_counter >= SS_TIMEOUT && (adc[1] >> 8 != last_knob >> 8)) {
        exit_screensaver();
        return;
    }
    last_knob = adc[1];

    // PARAM: the Teletype PARAM value (scripts), and the slot in Orca's
    // LOAD PATCH picker while it is open (sf_ui.c)
    ss_set_param(&scene_state, adc[1] << 2);
    sf_ui_knob(adc[1]);
#ifdef TELETYPE_PROFILE
    profile_update(&prof_ADC);
#endif
}

void handler_KeyTimer(int32_t data) {
    if (hold_key) {
        if (hold_key_count > 4)
            process_keypress(hold_key, mod_key, true, false);
        else
            hold_key_count++;
    }
}

void handler_HidConnect(int32_t data) {
    timer_add(&hidTimer, 47, &hidTimer_callback, NULL);
}

void handler_HidDisconnect(int32_t data) {
    timer_remove(&hidTimer);
}

void handler_HidTimer(int32_t data) {
    if (hid_get_frame_dirty()) {
        const int8_t* frame = (const int8_t*)hid_get_frame_data();

        mod_key = frame[0];
        for (size_t i = 2; i < 8; i++) {
            if (frame[i] == 0) {
                if (i == 2) {
                    hold_key_count = 0;
                    process_keypress(hold_key, mod_key, false, true);
                    hold_key = 0;
                }
            }
            else if (frame_compare(frame[i]) == false) {
                hold_key = frame[i];
                hold_key_count = 0;
                process_keypress(hold_key, mod_key, false, false);
            }
        }

        set_old_frame(frame);

        //moved inside the if block to avoid potential race conditions.
        hid_clear_frame_dirty(); 
    }

}

void handler_MscConnect(int32_t data) {
    // disable event handlers while doing USB write
    assign_msc_event_handlers();

    // clear screen
    for (size_t i = 0; i < 8; i++) {
        region_fill(&line[i], 0);
        region_draw(&line[i]);
    }
}

void handler_Trigger(int32_t data) {
    u8 input = device_config.flip ? 7 - data : data;
    if (!ss_get_mute(&scene_state, input)) {
        bool tr_state = gpio_get_pin_value(A00 + data);
        if (tr_state) {
            if (scene_state.variables.script_pol[input] & 1) {
                run_script(&scene_state, input);
            }
        }
        else {
            if (scene_state.variables.script_pol[input] & 2) {
                run_script(&scene_state, input);
            }
        }
    }
}

void handler_ScreenRefresh(int32_t data) {
#ifdef TELETYPE_PROFILE
    profile_update(&prof_ScreenRefresh);
#endif
    uint8_t screen_dirty = 0;

    screen_dirty = sf_ui_refresh();

    u8 grid = 0;
    for (size_t i = 0; i < 8; i++)
        if (screen_dirty & (1 << i)) {
            grid = 1;
            if (ss_counter < SS_TIMEOUT) region_draw(&line[i]);
        }
    if (grid_control_mode && grid) scene_state.grid.grid_dirty = 1;

    // the OLED controller's setup is sent again before every dream frame and
    // every ~2 s otherwise (screen_refresh_config, libavr32 local branch): a
    // module's screen went to garbage during the dream and stayed so through
    // the reset pin and init until a power-off; this puts it right at once
    static u8 heal_count;
    if (ss_counter >= SS_TIMEOUT || ++heal_count >= 32) {
        heal_count = 0;
        screen_refresh_config();
    }

    // the screensaver: the dream, drawn band by band
    if (ss_counter >= SS_TIMEOUT) {
        static u8 band[128 * 8];
        const uint32_t ms = (ss_counter - SS_TIMEOUT) * RATE_CLOCK;
        for (u8 i = 0; i < 8; i++) {
            dream_band(ms, i, band);
            screen_draw_region(0, i * 8, 128, 8, band);
        }
    }

#ifdef TELETYPE_PROFILE
    profile_update(&prof_ScreenRefresh);
#endif
}

void handler_EventTimer(int32_t data) {
    tele_tick(&scene_state, RATE_CLOCK);

    if (ss_counter < 0xFFFFFFF0u) ss_counter++;
}

void handler_AppCustom(int32_t data) {
    // If we need multiple custom event handlers then we can use an enum in the
    // data argument. 0 = metro, 1 = Orca Seaflake clock
    if (data == 1) {
        seaflake_tick(get_ticks());
        return;
    }
    if (ss_get_script_len(&scene_state, METRO_SCRIPT)) {
        set_metro_icon(true);
        run_script(&scene_state, METRO_SCRIPT);
        if (grid_connected && grid_control_mode)
            grid_metro_triggered(&scene_state);
    }
    else
        set_metro_icon(false);
}

static void handler_FtdiConnect(s32 data) {
    ftdi_setup();
}

static void handler_SerialConnect(s32 data) {
    monome_setup_mext();
}

static void handler_FtdiDisconnect(s32 data) {
    grid_connected = 0;
    timers_unset_monome();
}

static void handler_MonomeConnect(s32 data) {
    hold_key = 0;
    timers_set_monome();
    grid_connected = 1;

    grid_set_control_mode(grid_control_mode, sf_ui_mode(), &scene_state);

    scene_state.grid.grid_dirty = 1;
    grid_clear_held_keys();
}

static void handler_MonomePoll(s32 data) {
    monome_read_serial();
}

static void handler_MonomeRefresh(s32 data) {
    grid_refresh(&scene_state);
    monomeFrameDirty = 0b1111;
    (*monome_refresh)();
}

static void handler_MonomeGridKey(s32 data) {
    if (grid_control_mode && ss_counter >= SS_TIMEOUT) {
        exit_screensaver();
        return;
    }
    if (grid_control_mode) ss_counter = 0;

    u8 x, y, z;
    monome_grid_key_parse_event_data(data, &x, &y, &z);
    grid_process_key(&scene_state, x, y, z, 0);
}

static void handler_midi_connect(s32 data) {}

static void handler_midi_disconnect(s32 data) {}

static void handler_standard_midi_packet(s32 data) {
    midi_packet_parse(&midi_behavior, (u32)data);
}

static void midi_note_on(u8 ch, u8 num, u8 vel) {
    scene_state.midi.last_event_type = 1;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_note = num;
    scene_state.midi.last_velocity = vel;

    if (scene_state.midi.on_script != -1 &&
        scene_state.midi.on_count < MAX_MIDI_EVENTS) {
        scene_state.midi.on_channel[scene_state.midi.on_count] = ch;
        scene_state.midi.note_on[scene_state.midi.on_count] = num;
        scene_state.midi.note_vel[scene_state.midi.on_count] = vel;
        scene_state.midi.on_count++;
    }
}

static void midi_note_off(u8 ch, u8 num, u8 vel) {
    scene_state.midi.last_event_type = 2;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_note = num;
    scene_state.midi.last_velocity = vel;

    if (scene_state.midi.off_script != -1 &&
        scene_state.midi.off_count < MAX_MIDI_EVENTS) {
        scene_state.midi.off_channel[scene_state.midi.off_count] = ch;
        scene_state.midi.note_off[scene_state.midi.off_count] = num;
        scene_state.midi.off_count++;
    }
}

static void midi_control_change(u8 ch, u8 num, u8 val) {
    scene_state.midi.last_event_type = 3;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_controller = num;
    scene_state.midi.last_cc = val;

    if (scene_state.midi.cc_script != -1) {
        u8 found = 0;
        for (u8 i = 0; i < scene_state.midi.cc_count; i++) {
            if (scene_state.midi.cn[i] == num &&
                scene_state.midi.cc_channel[i] == ch) {
                scene_state.midi.cc[i] = val;
                found = 1;
                break;
            }
        }

        if (!found && scene_state.midi.cc_count < MAX_MIDI_EVENTS) {
            scene_state.midi.cc_channel[scene_state.midi.cc_count] = ch;
            scene_state.midi.cn[scene_state.midi.cc_count] = num;
            scene_state.midi.cc[scene_state.midi.cc_count] = val;
            scene_state.midi.cc_count++;
        }
    }
}

static void midi_clock_tick(void) {
    if (++midi_clock_counter >= scene_state.midi.clock_div) {
        midi_clock_counter = 0;
        scene_state.midi.last_event_type = 4;
        if (scene_state.midi.clk_script >= 0 &&
            scene_state.midi.clk_script < EDITABLE_SCRIPT_COUNT)
            run_script(&scene_state, scene_state.midi.clk_script);
    }
}

static void midi_seq_start(void) {
    scene_state.midi.last_event_type = 5;
    if (scene_state.midi.start_script >= 0 &&
        scene_state.midi.start_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.start_script);
}

static void midi_seq_stop(void) {
    scene_state.midi.last_event_type = 6;
    if (scene_state.midi.stop_script >= 0 &&
        scene_state.midi.stop_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.stop_script);
}

static void midi_seq_continue(void) {
    scene_state.midi.last_event_type = 7;
    if (scene_state.midi.continue_script >= 0 &&
        scene_state.midi.continue_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.continue_script);
}

////////////////////////////////////////////////////////////////////////////////
// event queue

void empty_event_handlers() {
    for (size_t i = 0; i < kNumEventTypes; i++) {
        app_event_handlers[i] = &handler_None;
    }
}

void assign_main_event_handlers() {
    empty_event_handlers();

    app_event_handlers[kEventFront] = &handler_Front;
    app_event_handlers[kEventPollADC] = &handler_PollADC;
    app_event_handlers[kEventKeyTimer] = &handler_KeyTimer;
    app_event_handlers[kEventHidConnect] = &handler_HidConnect;
    app_event_handlers[kEventHidDisconnect] = &handler_HidDisconnect;
    app_event_handlers[kEventHidTimer] = &handler_HidTimer;
    app_event_handlers[kEventMscConnect] = &handler_MscConnect;
    app_event_handlers[kEventTrigger] = &handler_Trigger;
    app_event_handlers[kEventScreenRefresh] = &handler_ScreenRefresh;
    app_event_handlers[kEventTimer] = &handler_EventTimer;
    app_event_handlers[kEventAppCustom] = &handler_AppCustom;
    app_event_handlers[kEventFtdiConnect] = &handler_FtdiConnect;
    app_event_handlers[kEventFtdiDisconnect] = &handler_FtdiDisconnect;
    app_event_handlers[kEventMonomeConnect] = &handler_MonomeConnect;
    app_event_handlers[kEventMonomeDisconnect] = &handler_None;
    app_event_handlers[kEventMonomePoll] = &handler_MonomePoll;
    app_event_handlers[kEventMonomeRefresh] = &handler_MonomeRefresh;
    app_event_handlers[kEventMonomeGridKey] = &handler_MonomeGridKey;
    app_event_handlers[kEventMidiConnect] = &handler_midi_connect;
    app_event_handlers[kEventMidiDisconnect] = &handler_midi_disconnect;
    app_event_handlers[kEventMidiPacket] = &handler_standard_midi_packet;
    app_event_handlers[kEventSerialConnect] = &handler_SerialConnect;
    app_event_handlers[kEventSerialDisconnect] = &handler_FtdiDisconnect;
}

static void assign_msc_event_handlers(void) {
    empty_event_handlers();

    app_event_handlers[kEventFront] = &handler_usb_Front;
    app_event_handlers[kEventPollADC] = &handler_usb_PollADC;
    app_event_handlers[kEventScreenRefresh] = &handler_usb_ScreenRefresh;
}

// app event loop
void check_events(void) {
    event_t e;
    if (event_next(&e)) {
        (app_event_handlers)[e.type](e.data);
        return;
    }
#ifdef SF_LOW_POWER
    // Nothing to do: idle the CPU until the next interrupt (the 1 ms timer
    // tick at the latest; USB, I2C, SPI and the timers keep running). An
    // event posted right before the sleep waits at most that 1 ms.
    SLEEP(AVR32_PM_SMODE_IDLE);
#endif
}


////////////////////////////////////////////////////////////////////////////////
// mode handling

// defined in globals.h
// set_mode() and set_last_mode() are in sf_ui.c: Orca Seaflake or its help

// defined in globals.h
void clear_delays_and_slews(scene_state_t* ss) {
    clear_delays(ss);
    for (int i = 0; i < 4; i++) { aout[i].step = 1; }
}

////////////////////////////////////////////////////////////////////////////////
// key handling

void process_keypress(uint8_t key, uint8_t mod_key, bool is_held_key,
                      bool is_release) {
    // alt+d: the screensaver now (to test it without waiting)
    if (!is_release && ss_counter < SS_TIMEOUT && match_alt(mod_key, key, HID_D)) {
        ss_counter = SS_TIMEOUT;
        return;
    }
    // reset inactivity counter. A release doesn't wake the screensaver (the
    // release of alt+d would end it at once)
    if (ss_counter >= SS_TIMEOUT) {
        if (!is_release) exit_screensaver();
        return;
    }
    ss_counter = 0;

    // every key goes to sf_ui.c: Orca Seaflake, or the stock Teletype
    // screens (alt+o switches)
    if (is_release) sf_ui_key_release(key, mod_key);
    else sf_ui_key(key, mod_key, is_held_key);
}



////////////////////////////////////////////////////////////////////////////////
// other

void render_init(void) {
    region_alloc(&line[0]);
    region_alloc(&line[1]);
    region_alloc(&line[2]);
    region_alloc(&line[3]);
    region_alloc(&line[4]);
    region_alloc(&line[5]);
    region_alloc(&line[6]);
    region_alloc(&line[7]);
}

void exit_screensaver(void) {
    ss_counter = 0;
    screen_refresh_config();
    for (u8 i = 0; i < 8; i++) line[i].dirty = 1;  // every line again
    sf_ui_redraw();  // redraw the screen that was on
}

void update_device_config(u8 refresh) {
    screen_set_direction(device_config.flip);
    if (refresh) sf_ui_redraw();
    flash_update_device_config(&device_config);
}

static void setup_midi(void) {
    midi_behavior.note_on = &midi_note_on;
    midi_behavior.note_off = &midi_note_off;
    midi_behavior.channel_pressure = NULL;
    midi_behavior.pitch_bend = NULL;
    midi_behavior.control_change = &midi_control_change;
    midi_behavior.program_change = NULL;
    midi_behavior.clock_tick = &midi_clock_tick;
    midi_behavior.seq_start = &midi_seq_start;
    midi_behavior.seq_stop = &midi_seq_stop;
    midi_behavior.seq_continue = &midi_seq_continue;
    midi_behavior.panic = NULL;
    midi_behavior.aftertouch = NULL;
}


////////////////////////////////////////////////////////////////////////////////
// teletype_io.h

uint32_t tele_get_ticks() {
    return get_ticks();
}

void tele_metro_updated() {
    uint32_t metro_time = scene_state.variables.m;

    bool m_act = scene_state.variables.m_act;
    if (metro_time < METRO_MIN_UNSUPPORTED_MS) {
        metro_time = METRO_MIN_UNSUPPORTED_MS;
    }

    if (m_act && !metro_timer_enabled) {  // enable the timer
        timer_add(&metroTimer, metro_time, &metroTimer_callback, NULL);
        metro_timer_enabled = true;
    }
    else if (!m_act && metro_timer_enabled) {  // disable the timer
        timer_remove(&metroTimer);
        metro_timer_enabled = false;
    }
    else if (metro_timer_enabled) {  // just update the time
        metroTimer.ticks = metro_time;
    }

    if (metro_timer_enabled && ss_get_script_len(&scene_state, METRO_SCRIPT))
        set_metro_icon(true);
    else
        set_metro_icon(false);

    if (grid_connected && grid_control_mode) scene_state.grid.grid_dirty = 1;

    edit_mode_refresh();
}

void tele_metro_reset() {
    if (metro_timer_enabled) timer_reset(&metroTimer);
}

void tele_tr(uint8_t i, int16_t v) {
    uint32_t pin = B08 + (device_config.flip ? 3 - i : i);

    if (v)
        gpio_set_pin_high(pin);
    else
        gpio_set_pin_low(pin);
}

void tele_tr_pulse(uint8_t i, int16_t time) {
    if (i >= TR_COUNT) return;
    timer_remove(&trPulseTimer[i]);
    timer_add(&trPulseTimer[i], time, &trPulseTimer_callback,
              (void*)(int32_t)i);
}

void tele_tr_pulse_clear(uint8_t i) {
    if (i >= TR_COUNT) return;
    timer_remove(&trPulseTimer[i]);
}

void tele_tr_pulse_time(uint8_t i, int16_t time) {
    if (i >= TR_COUNT) return;

    u32 time_spent = trPulseTimer[i].ticks - trPulseTimer[i].ticksRemain;
    timer_set(&trPulseTimer[i], time);
    if (time_spent >= time) { timer_manual(&trPulseTimer[i]); }
    else { trPulseTimer[i].ticksRemain = time - time_spent; }
}

void trPulseTimer_callback(void* obj) {
    int i = (int)obj;
    if (i >= TR_COUNT) return;
    timer_remove(&trPulseTimer[i]);
    tele_tr_pulse_end(&scene_state, i);
}

void tele_cv(uint8_t i, int16_t v, uint8_t s) {
    int16_t t = v + aout[i].off;
    if (t < 0)
        t = 0;
    else if (t > 16383)
        t = 16383;
    aout[i].target = t;
    if (s) {
        aout[i].step = aout[i].slew;
        aout[i].delta = ((aout[i].target - aout[i].now) << 16) / aout[i].step;
    }
    else {
        aout[i].step = 1;
        aout[i].now = aout[i].target;
    }

    aout[i].a = aout[i].now << 16;

    timer_manual(&cvTimer);
}

void tele_cv_slew(uint8_t i, int16_t v) {
    aout[i].slew = v / RATE_CV;
    if (aout[i].slew == 0) aout[i].slew = 1;
}

void tele_cv_off(uint8_t i, int16_t v) {
    aout[i].off = v;
}

uint16_t tele_get_cv(uint8_t i) {
    return aout[i].now;
}

void tele_cv_cal(uint8_t i, int32_t b, int32_t m) {
    if (i > 3) { return; }
    uint8_t n = device_config.flip ? 3 - i : i;
    scene_state.cal.cv_scale[n].b = b;
    scene_state.cal.cv_scale[n].m = m;
    tele_save_calibration();

    // force a CV output update if one is not imminent
    if (aout[i].step == 0) { aout[i].step = 1; }
}

void tele_update_adc(u8 force) {
    if (!force && get_ticks() == last_adc_tick) return;
    last_adc_tick = get_ticks();
    adc_convert(&adc);
    ss_set_in(&scene_state, adc[0] << 2);
    ss_set_param(&scene_state, adc[1] << 2);
}

// every operator's I2C message comes through here; sf_ii_note counts it and
// whether the device acknowledged it (the scan screen and status line show it)
// A timeout (TWI_BUSY) restarts the bus and the message is tried once more.
static int bus_tx(uint8_t addr, uint8_t* data, uint8_t l) {
    int st = i2c_leader_tx(addr, data, l);
    if (st == TWI_BUSY) {
        sf_bus_restart();
        st = i2c_leader_tx(addr, data, l);
        if (st == TWI_BUSY) sf_bus_restart();
    }
    return st;
}

static int bus_rx(uint8_t addr, uint8_t* data, uint8_t l) {
    int st = i2c_leader_rx(addr, data, l);
    if (st == TWI_BUSY) sf_bus_restart();  // (a read is not repeated)
    return st;
}

void tele_ii_tx(uint8_t addr, uint8_t* data, uint8_t l) {
    sf_ii_note(addr, bus_tx(addr, data, l), data, l);
}

void tele_ii_rx(uint8_t addr, uint8_t* data, uint8_t l) {
    sf_ii_note(addr, bus_rx(addr, data, l), 0, 0);
}

// Orca Seaflake device scan (sf.h, sf_ii.c): the raw bus. A follower that
// is not there does not acknowledge its address, and the TWI driver reports
// that; on a dead bus it gives up after its busy counter.
bool sf_io_ii_tx(uint8_t addr, const uint8_t* data, uint8_t len) {
    return bus_tx(addr, (uint8_t*)data, len) == TWI_SUCCESS;
}

bool sf_io_ii_rx(uint8_t addr, uint8_t* data, uint8_t len) {
    return bus_rx(addr, data, len) == TWI_SUCCESS;
}

void tele_scene(uint8_t i, uint8_t init_grid, uint8_t init_pattern) {
    if (i >= SCENE_SLOTS) return;
    preset_select = i;
    flash_read(i, &scene_state, &scene_text, init_pattern, init_grid, 0);
    set_dash_updated();
    if (init_grid) scene_state.grid.scr_dirty = scene_state.grid.grid_dirty = 1;
}

void tele_kill() {
    for (int i = 0; i < 4; i++) {
        aout[i].step = 1;
        tele_tr(i, 0);
    }
}

bool tele_get_input_state(uint8_t n) {
    u8 input = device_config.flip ? 7 - n : n;
    return gpio_get_pin_value(A00 + input) > 0;
}

void tele_vars_updated() {
    set_vars_updated();
}

void tele_save_calibration() {
    flash_update_cal(&scene_state.cal);
}

void grid_key_press(uint8_t x, uint8_t y, uint8_t z) {
    grid_process_key(&scene_state, x, y, z, 1);
}

void device_flip() {
    device_config.flip = !device_config.flip;
    update_device_config(1);

    for (int i = 0; i < 4; i++) {
        // trigger a CV update if one is not imminent
        if (aout[i].step == 0) { aout[i].step = 1; }
        // update TR state
        tele_tr(i, scene_state.variables.tr[i]);
    }
}

void reset_midi_counter() {
    midi_clock_counter = 0;
}

////////////////////////////////////////////////////////////////////////////////
// main

void initialize_module(void) {
    sysclk_init();

    init_dbg_rs232(FMCK_HZ);

    init_gpio();
    assign_main_event_handlers();
    init_events();
    init_tc();
    init_spi();
    init_adc();

    irq_initialize_vectors();
    register_interrupts();
    cpu_irq_enable();

    init_usb_host();
    init_monome();
    init_oled();
    setup_midi();

    // wait to allow for any i2c devices to fully initalise
    delay_ms(1500);

    sf_bus_init();

    print_dbg("\r\n\r\n// teletype! //////////////////////////////// ");

    ss_init(&scene_state);

    // screen init
    render_init();

    if (is_flash_fresh()) {
        char s[36];
        strcpy(s, "SCENES WILL BE OVERWRITTEN!");
        region_fill(&line[4], 0);
        font_string_region_clip(&line[4], s, 0, 0, 0x4, 0);
        region_draw(&line[4]);

        strcpy(s, "PRESS ONLY IF YOU ARE");
        region_fill(&line[5], 0);
        font_string_region_clip(&line[5], s, 0, 0, 0x4, 0);
        region_draw(&line[5]);

        strcpy(s, "UPDATING FIRMWARE");
        region_fill(&line[6], 0);
        font_string_region_clip(&line[6], s, 0, 0, 0x4, 0);
        region_draw(&line[6]);

        strcpy(s, "DO NOT PRESS OTHERWISE!");
        region_fill(&line[7], 0);
        font_string_region_clip(&line[7], s, 0, 0, 0x4, 0);
        region_draw(&line[7]);
        ignore_front_press = 1;
    }

    // prepare flash (if needed)
    flash_prepare();

    // load device config
    flash_get_device_config(&device_config);
    update_device_config(0);

    // load calibration data from flash
    flash_get_cal(&scene_state.cal);
    ss_update_param_scale(&scene_state);
    ss_update_in_scale(&scene_state);
    ss_update_fader_scale_all(&scene_state);

    // load preset from flash
    preset_select = flash_last_saved_scene();
    ss_set_scene(&scene_state, preset_select);
    flash_read(preset_select, &scene_state, &scene_text, 1, 1, 1);

    // setup daisy chain for two dacs
    spi_selectChip(DAC_SPI, DAC_SPI_NPCS);
    spi_write(DAC_SPI, 0x80);
    spi_write(DAC_SPI, 0xff);
    spi_write(DAC_SPI, 0xff);
    spi_unselectChip(DAC_SPI, DAC_SPI_NPCS);

    timer_add(&clockTimer, RATE_CLOCK, &clockTimer_callback, NULL);
    timer_add(&cvTimer, RATE_CV, &cvTimer_callback, NULL);
    timer_add(&keyTimer, 71, &keyTimer_callback, NULL);
    timer_add(&adcTimer, 61, &adcTimer_callback, NULL);
    timer_add(&refreshTimer, 63, &refreshTimer_callback, NULL);
    timer_add(&gridFaderTimer, 25, &grid_fader_timer_callback, NULL);
    timer_add(&midiScriptTimer, 25, &midiScriptTimer_callback, NULL);

    // update IN and PARAM in case Init uses them
    tele_update_adc(1);

    // manually call tele_metro_updated to sync metro to scene_state
    metro_timer_enabled = false;
    tele_metro_updated();

    // init chaos generator
    chaos_init();
    clear_delays(&scene_state);

    aout[0].slew = 1;
    aout[1].slew = 1;
    aout[2].slew = 1;
    aout[3].slew = 1;

    for (uint8_t i = 0; i < TR_COUNT; i++) {
        trPulseTimer[i].next = NULL;
        trPulseTimer[i].prev = NULL;
    }

    // the Teletype scene's init script still runs (its scripts are what
    // $script:N runs), but its live screen is never shown
    init_live_mode();

    run_script(&scene_state, INIT_SCRIPT);
    scene_state.initializing = false;

    // Orca Seaflake: load the last patch (stopped), start its clock and
    // show the grid: the module boots into Orca
    seaflake_init();
    timer_add(&seaflakeTimer, 1, &seaflakeTimer_callback, NULL);
    set_mode(M_SEAFLAKE);
}

int main(void) {
    initialize_module();
#ifdef TELETYPE_PROFILE
    uint32_t count = 0;
#endif
    while (true) {
        midi_read();
        check_events();
#ifdef TELETYPE_PROFILE
        count = (count + 1) % (FCPU_HZ / 10);
        if (count == 0) {
            print_dbg("\r\n\r\nProfile Data (us)");
            for (uint8_t i = 0; i < TOTAL_SCRIPT_COUNT - 1; i++) {
                print_dbg("\r\nScript ");
                print_dbg_ulong(i);
                print_dbg(":\t");
                print_dbg_ulong(profile_delta_us(&prof_Script[i]));
            }
            uint32_t total = 0;
            for (uint8_t i = 0; i < DELAY_SIZE; i++)
                total += profile_delta_us(&prof_Delay[i]);
            print_dbg("\r\nDelays (total):\t");
            print_dbg_ulong(total);
            print_dbg("\r\nCV Write:\t");
            print_dbg_ulong(profile_delta_us(&prof_CV));
            print_dbg("\r\nADC Read:\t");
            print_dbg_ulong(profile_delta_us(&prof_ADC));
            print_dbg("\r\nScreen Refresh:\t");
            print_dbg_ulong(profile_delta_us(&prof_ScreenRefresh));
        }
#endif
    }
}
