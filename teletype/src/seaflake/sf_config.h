// Orca Seaflake for Teletype: the ports configuration block.
//
// 32 bytes at a fixed flash address (SF_CONFIG_ADDR, see module/seaflake.lds)
// that the flasher in the emulator app shows as the "ports" table and can
// change in the .hex before it is flashed. The firmware reads it at boot:
// the Disting EX's I2C address, which Disting EX unit and which ER-301 the
// Orca operators send to, the default trigger pulse, and the Disting EX
// preset to load at power-on. The Disting EX address can also be changed on
// the Teletype (ctrl-i, up/down, enter), which writes the block again.

#ifndef _SEAFLAKE_CONFIG_H_
#define _SEAFLAKE_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#define SF_CONFIG_ADDR 0x80044C00UL
#define SF_CONFIG_MAGIC "SFCF"
// 2 added the startup preset (bytes 9-11), 3 the Disting EX address (byte 12)
#define SF_CONFIG_VERSION 3
// the Disting EX's own default would be 49 (0x31), the ER-301's address;
// the Teletype's EX ops have always used 65 (0x41)
#define SF_EX_ADDR_DEFAULT 65
#define SF_EX_ADDR_MIN 1
#define SF_EX_ADDR_MAX 124  // units 2-4 are the next three addresses: up to 127

typedef struct {
    char magic[4];        // "SFCF"
    uint8_t version;      // 3
    uint8_t ex_unit;      // 0 = first Disting EX found, 1-4 = always that unit (ex_addr + unit - 1)
    uint8_t er_unit;      // 0 = first ER-301 found, 1-3 = always that one (I2C 0x31-0x33)
    uint8_t pulse_ms;     // default trigger pulse, 1-250 ms ($pulse:N changes it per patch)
    uint8_t midi_via_ex;  // 1: the MIDI operators go out through the Disting EX (EX.M.*); always 1
    uint8_t boot_preset_on;  // 1: load the Disting EX preset below once at power-on, 0: send nothing
    uint8_t boot_preset_hi;  // the preset number (EX.PRESET), high byte
    uint8_t boot_preset_lo;  //                                  low byte
    uint8_t ex_addr;      // v3: Disting EX unit 1's I2C address, 1-124 (65 = 0x41); units 2-4 follow
    uint8_t reserved[18];
    uint8_t sum;          // the 31 bytes before it, added up, low byte
} sf_config_t;

// const in flash on the module. In the emulator it can be set (emu_set_config)
// to the block the Flash window will write, so both run the same settings.
#ifdef __AVR32__
#define SF_CONFIG_CONST const
#else
#define SF_CONFIG_CONST
#endif
extern SF_CONFIG_CONST sf_config_t sf_config;

// the block's fields as numbers (any version read, version 3 written)
typedef struct {
    uint8_t version;      // what was read (1-3)
    uint8_t ex_unit, er_unit, pulse_ms, midi_via_ex, boot_preset_on;
    uint16_t boot_preset;
    uint8_t ex_addr;      // version 1-2 blocks: 65 (they had no address)
} sf_ports_t;

// a 32-byte block: true if intact (magic, version 1-3, checksum); fills p
// (out-of-range values replaced by the defaults). Shared with the flasher.
bool sf_config_parse(const uint8_t block[32], sf_ports_t *p);
// p as a version 3 block with its checksum
void sf_config_build(uint8_t block[32], const sf_ports_t *p);
// the defaults compiled into the firmware
void sf_config_defaults(sf_ports_t *p);

// true if the block in flash is intact (magic, version and checksum)
bool sf_config_ok(void);
uint8_t sf_config_ex_unit(void);   // 0 = auto
uint8_t sf_config_er_unit(void);   // 0 = auto
uint8_t sf_config_pulse_ms(void);  // 20 if the block is bad
int16_t sf_config_boot_preset(void);  // -1 = none (also if the block is bad)
uint8_t sf_config_ex_addr(void);   // 65 if the block is bad or older than version 3
// change the Disting EX address and write the block back to flash (the
// other fields kept). false if addr is out of range. Writes only on a change.
bool sf_config_set_ex_addr(uint8_t addr);

// the flash write: module/sf_flash.c on the module, tt_emu.c in the emulator
void sf_io_config_write(const uint8_t block[32]);

#endif
