// The ports configuration block (see sf_config.h). On the module it is
// placed at SF_CONFIG_ADDR by module/seaflake.lds; on the Mac it is a plain
// variable. The flasher patches the bytes in the .hex (with
// sf_config_build, through the app's bridge) and the Teletype rewrites them
// when its Disting EX address is changed on the scan screen.

#include "sf_config.h"

#include <string.h>

#include "sf.h"

#ifdef __AVR32__
#define SF_CONFIG_SECTION __attribute__((section(".sf_config"), used))
#else
#define SF_CONFIG_SECTION
#endif

// sum: 'S'+'F'+'C'+'F' (290) + version 3 + pulse 20 + midi_via_ex 1 + ex_addr 65 = 379 = 0x17B
SF_CONFIG_CONST sf_config_t SF_CONFIG_SECTION sf_config = {
    .magic = { 'S', 'F', 'C', 'F' },
    .version = SF_CONFIG_VERSION,
    .ex_unit = 0,
    .er_unit = 0,
    .pulse_ms = SF_PULSE_DEFAULT,
    .midi_via_ex = 1,
    .boot_preset_on = 0,
    .boot_preset_hi = 0,
    .boot_preset_lo = 0,
    .ex_addr = SF_EX_ADDR_DEFAULT,
    .reserved = { 0 },
    .sum = 0x7B,
};

static uint8_t sum31(const uint8_t *b) {
    uint16_t s = 0;
    for (uint8_t i = 0; i < 31; i++) s += b[i];
    return (uint8_t)s;
}

void sf_config_defaults(sf_ports_t *p) {
    memset(p, 0, sizeof(*p));
    p->version = SF_CONFIG_VERSION;
    p->pulse_ms = SF_PULSE_DEFAULT;
    p->midi_via_ex = 1;
    p->ex_addr = SF_EX_ADDR_DEFAULT;
}

bool sf_config_parse(const uint8_t b[32], sf_ports_t *p) {
    sf_config_defaults(p);
    if (b[0] != 'S' || b[1] != 'F' || b[2] != 'C' || b[3] != 'F') return false;
    if (b[4] < 1 || b[4] > SF_CONFIG_VERSION || sum31(b) != b[31]) return false;
    p->version = b[4];
    if (b[5] <= 4) p->ex_unit = b[5];
    if (b[6] <= 3) p->er_unit = b[6];
    if (b[7] >= 1 && b[7] <= 250) p->pulse_ms = b[7];
    p->midi_via_ex = b[8] ? 1 : 0;
    if (b[4] >= 2) {
        p->boot_preset_on = b[9] == 1;
        p->boot_preset = ((uint16_t)b[10] << 8 | b[11]) & 0x7FFF;
    }
    if (b[4] >= 3 && b[12] >= SF_EX_ADDR_MIN && b[12] <= SF_EX_ADDR_MAX) p->ex_addr = b[12];
    return true;
}

void sf_config_build(uint8_t b[32], const sf_ports_t *p) {
    memset(b, 0, 32);
    b[0] = 'S', b[1] = 'F', b[2] = 'C', b[3] = 'F';
    b[4] = SF_CONFIG_VERSION;
    b[5] = p->ex_unit;
    b[6] = p->er_unit;
    b[7] = p->pulse_ms;
    b[8] = p->midi_via_ex;
    b[9] = p->boot_preset_on ? 1 : 0;
    b[10] = (p->boot_preset >> 8) & 0x7F;
    b[11] = p->boot_preset & 0xFF;
    b[12] = p->ex_addr;
    b[31] = sum31(b);
}

// read the block in flash. Through a volatile pointer: the block is a const
// with an initializer, and the compiler would otherwise fold its fields to
// the compiled-in defaults, ignoring what the flasher or the Teletype wrote
// (the 2026-11-19 build c3fc4a6 did exactly that: sf_config_pulse_ms was
// "return 20").
static bool current(sf_ports_t *p) {
    uint8_t b[32];
    const volatile uint8_t *f = (const volatile uint8_t *)&sf_config;
    for (uint8_t i = 0; i < 32; i++) b[i] = f[i];
    return sf_config_parse(b, p);
}

bool sf_config_ok(void) {
    sf_ports_t p;
    return current(&p);
}

uint8_t sf_config_ex_unit(void) {
    sf_ports_t p;
    current(&p);
    return p.ex_unit;
}

uint8_t sf_config_er_unit(void) {
    sf_ports_t p;
    current(&p);
    return p.er_unit;
}

uint8_t sf_config_pulse_ms(void) {
    sf_ports_t p;
    current(&p);
    return p.pulse_ms;
}

int16_t sf_config_boot_preset(void) {
    sf_ports_t p;
    if (!current(&p) || !p.boot_preset_on) return -1;
    return (int16_t)p.boot_preset;
}

uint8_t sf_config_ex_addr(void) {
    sf_ports_t p;
    current(&p);
    return p.ex_addr;
}

bool sf_config_set_ex_addr(uint8_t addr) {
    if (addr < SF_EX_ADDR_MIN || addr > SF_EX_ADDR_MAX) return false;
    sf_ports_t p;
    const bool ok = current(&p);  // a bad block: the defaults, plus the new address
    if (ok && p.version == SF_CONFIG_VERSION && p.ex_addr == addr) return true;
    p.ex_addr = addr;
    uint8_t b[32];
    sf_config_build(b, &p);
    sf_io_config_write(b);
    return true;
}
