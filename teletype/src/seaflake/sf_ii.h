#ifndef _SEAFLAKE_II_H_
#define _SEAFLAKE_II_H_

#include <stdbool.h>
#include <stdint.h>

// why the last ; or = message failed (Teletype's error text), "" if it did not
extern char sf_ii_error[24];
// the Teletype command the last ; or = message was turned into
extern char sf_ii_last[40];

void sf_ii_reset(void);

// I2C messages the operators sent (not the scan's probes), and how many the
// device did not acknowledge: the last address that failed and when (ticks).
// The hardware host (module/main.c tele_ii_tx/rx) and the emulator call
// sf_ii_note after every transfer.
extern uint16_t sf_ii_sent, sf_ii_failed;
extern uint8_t sf_ii_fail_addr;
// Just Friends at 0x70: 0 not asked yet, 1 answered, 2 no answer
extern uint8_t sf_jf_state;
extern uint32_t sf_ii_fail_at;
// the last failure: status (TWI: -5 = no acknowledge, -8 = gave up waiting,
// -2 = arbitration lost), its first byte (the command) and length
extern int8_t sf_ii_fail_code;
extern uint8_t sf_ii_fail_cmd, sf_ii_fail_len;
// status: 0 = acknowledged, else the TWI driver's code
void sf_ii_note(uint8_t addr, int status, const uint8_t *data, uint8_t len);
// short name of a status ("NACK", "BUSY", ...)
const char *sf_ii_status_name(int status);

// the I2C bus as the hardware host found it at power-on (module/main.c
// sf_bus_init): bit 0 = SDA, bit 1 = SCL. ext_pullup: the line rose by itself
// with the chip's pull-ups off (something on the bus pulls it up). stuck: the
// line was low at rest (a follower holding it, or a short). recovered: SDA was
// stuck and 9 clocks freed it. khz: the real SCL rate.
typedef struct sf_bus {
    uint8_t ext_pullup, stuck, recovered, internal_pullup;
    uint8_t restarts;  // transfers that timed out and restarted the bus
    uint16_t khz;
} sf_bus_t;
extern sf_bus_t sf_bus;

// what the device scan found (sf_scan_run: at power-on and ctrl-i)
typedef struct sf_scan {
    uint8_t done;
    uint8_t ex_addr;        // unit 1's I2C address (sf_config_ex_addr: 65 = 0x41 by default)
    uint8_t ex[4];          // Disting EX unit 1-4 answered (ex_addr + 0..3)
    int16_t ex_alg[4];      // its algorithm
    int16_t ex_preset[4];   // its preset
    uint8_t er[3];          // ER-301 1-3 answered (0x31-0x33)
    uint8_t ex_unit;        // unit the MIDI operators and > use, 0 = none found
    uint8_t er_unit;        // ER-301 the a-z outputs and < use, 0 = none found
} sf_scan_t;
extern sf_scan_t sf_scan;
void sf_scan_run(void);
// ER-301s known to be there (bit n-1) without a write: the emulator's
// imported scan report. The next sf_scan_run counts them as found
void sf_scan_er_known(uint8_t mask);
// the Disting EX the operators use answered the last scan
bool sf_scan_ex_here(void);
// the startup preset from the ports configuration (sf_config.h): sends
// EX.PRESET once if one is set and the Disting EX answered. true if sent
bool sf_boot_preset(void);

// Disting EX units: a MIDI note number as an EX.NOTE pitch (0 V = note 48,
// 16384 = 10 V), Orca velocity 0-16 as an EX.NOTE velocity (0..16384)
int16_t sf_ex_pitch(int16_t note);
int16_t sf_ex_velocity(int16_t v);
// Just Friends units: MIDI note -> volts with 4C (72) = 0 V; Orca 0-16 -> 0-8 V
int16_t sf_jf_pitch(int16_t note);
int16_t sf_jf_velocity(int16_t v);

// emulator: called with the text of every Teletype command Seaflake runs
extern void (*sf_trace)(const char *command);

#endif
