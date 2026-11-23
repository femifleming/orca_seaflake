// Tiny string helpers. The Teletype firmware does not link printf (it would
// cost ~15 KB of flash), so Seaflake builds its text with these.

#ifndef _SEAFLAKE_STR_H_
#define _SEAFLAKE_STR_H_

#include <stdint.h>

// append s, return the new end (always 0 terminated)
static inline char *sf_cat(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

// append v in decimal, at least width digits padded with pad
static inline char *sf_num(char *p, int32_t v, uint8_t width, char pad) {
    char t[12];
    uint8_t n = 0;
    uint32_t u = v < 0 ? -v : v;
    do {
        t[n++] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (v < 0) t[n++] = '-';
    while (n < width) t[n++] = pad;
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static inline char *sf_chr(char *p, char c) {
    *p++ = c;
    *p = 0;
    return p;
}

// copy at most max - 1 characters
static inline void sf_copy(char *dst, const char *src, uint8_t max) {
    uint8_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

#include <stdarg.h>

// a small snprintf: %s %c %d %X with an optional 0 flag and width (%02d, %3d)
static inline void sf_fmt(char *out, uint8_t max, const char *f, ...) {
    char buf[96];
    char *p = buf;
    va_list ap;
    va_start(ap, f);
    for (; *f && p - buf < 80; f++) {
        if (*f != '%') {
            *p++ = *f;
            continue;
        }
        f++;
        char pad = ' ';
        uint8_t width = 0;
        if (*f == '0') {
            pad = '0';
            f++;
        }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == 'd')
            p = sf_num(p, va_arg(ap, int), width, pad);
        else if (*f == 'X') {
            unsigned v = va_arg(ap, int);
            char t[8];
            uint8_t n = 0;
            do {
                t[n++] = "0123456789ABCDEF"[v & 15];
                v >>= 4;
            } while (v && n < 8);
            while (n < width) t[n++] = pad;
            while (n) *p++ = t[--n];
        }
        else if (*f == 'c')
            *p++ = (char)va_arg(ap, int);
        else if (*f == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s && p - buf < 80) *p++ = *s++;
        }
        else if (!*f)
            break;
    }
    va_end(ap);
    *p = 0;
    sf_copy(out, buf, max);
}

#endif
