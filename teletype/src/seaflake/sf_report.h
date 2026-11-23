#ifndef _SEAFLAKE_REPORT_H_
#define _SEAFLAKE_REPORT_H_

#include <stdbool.h>
#include <stdint.h>

// text written to a USB stick for the Mac emulator (see sf_report.c)
typedef void (*sf_put_fn)(const char *text);
void sf_report_scan(sf_put_fn put);
// grid: an 80 x 40 scratch buffer
void sf_report_patches(sf_put_fn put, char *grid);

// read SFPATCH.TXT back (the module from the USB stick, the emulator from a
// file): every PATCH and SONG in it is stored in its slot. Character by
// character, like Teletype's own scene reader; data goes to both callbacks.
// Returns patches + songs stored; *np / *ns get the counts if not NULL.
typedef uint16_t (*sf_getc_fn)(void *data);
typedef bool (*sf_eof_fn)(void *data);
int sf_import_patches(sf_getc_fn getc, sf_eof_fn eof, void *data, uint8_t *np, uint8_t *ns);

#endif
