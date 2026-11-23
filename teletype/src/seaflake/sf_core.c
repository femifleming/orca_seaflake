// Orca Seaflake engine, ported from terminal/src/core/{orca,operator,library}.js
// of the orca_seaflake repo. Operator behaviour follows that source line by
// line; comments quote it where the reason for something is not obvious.

#include "sf.h"
#include "sf_config.h"

#include <ctype.h>
#include <string.h>

sf_t sf;

////////////////////////////////////////////////////////////////////////////
// grid helpers

static inline bool inb(int x, int y) {
    return x >= 0 && x < SF_W && y >= 0 && y < SF_H;
}

// glyph at x,y. 0 outside the grid (the JS returns an empty string there)
static char G(int x, int y) {
    return inb(x, y) ? sf.grid[y * SF_W + x] : 0;
}

static bool W(int x, int y, char g) {
    if (!g || !inb(x, y)) return false;
    char *c = &sf.grid[y * SF_W + x];
    if (*c == g) return false;
    *c = (g > 32 && g < 127) ? g : '.';
    sf.dirty = 1;
    return true;
}

static void L(int x, int y) {
    if (!inb(x, y)) return;
    int i = y * SF_W + x;
    sf.lock[i >> 3] |= 1 << (i & 7);
}

// a picked cell: locked, and marked so the screen can show it as Orca does
static void Pk(int x, int y) {
    if (!inb(x, y)) return;
    L(x, y);
    int i = y * SF_W + x;
    sf.port[i >> 3] |= 1 << (i & 7);
}

static inline bool locked_i(int i) {
    return sf.lock[i >> 3] & (1 << (i & 7));
}

// base 36 value of a glyph. -1 for glyphs that are not a digit or letter
static int V(char g) {
    if (!g || g == '.' || g == '*') return 0;
    if (g >= '0' && g <= '9') return g - '0';
    if (g >= 'a' && g <= 'z') return g - 'a' + 10;
    if (g >= 'A' && g <= 'Z') return g - 'A' + 10;
    return -1;
}

static char K(int v) {
    if (v < 0) return 0;
    return "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36];
}

static int clampi(int v, int mn, int mx) {
    return v < mn ? mn : v > mx ? mx : v;
}

// listen(port): the glyph, or the port's default when the cell is empty
static char LG(int x, int y, char def) {
    char g = G(x, y);
    return ((g == '.' || g == '*') && def) ? def : g;
}

// listen(port, true): the clamped value
static int LV(int x, int y, char def, int mn, int mx) {
    return clampi(V(LG(x, y, def)), mn, mx);
}

static bool has_bang(int x, int y) {
    return G(x + 1, y) == '*' || G(x - 1, y) == '*' || G(x, y + 1) == '*' ||
           G(x, y - 1) == '*';
}

// an output below the operator that copies the case of the right operand
static void out_s(int x, int y, char g) {
    if (!g) return;
    char r = G(x + 1, y);
    if (r >= 'A' && r <= 'Z') g = toupper((unsigned char)g);
    W(x, y + 1, g);
}

static void bang_out(int x, int y, bool b) {
    W(x, y + 1, b ? '*' : '.');
    L(x, y + 1);
}

static uint32_t rnd(void) {
    uint32_t r = sf.rng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return sf.rng = r;
}

static uint32_t isqrt(uint32_t v) {
    uint32_t r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        }
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

////////////////////////////////////////////////////////////////////////////
// variables

static int var_i(char k) {
    return (k > 32 && k < 127) ? k - 32 : -1;
}

static void var_set(char k, char v) {
    int i = var_i(k);
    if (i >= 0) sf.var[i] = v;
}

// the glyph stored under k. '.' when unset, 0 when it holds a scale mask
static char var_get(char k) {
    int i = var_i(k);
    if (i < 0 || !sf.var[i]) return '.';
    return sf.var[i] == 1 ? 0 : sf.var[i];
}

////////////////////////////////////////////////////////////////////////////
// notes

int16_t sf_note_id(char note, int16_t octave) {
    // transpose.js: A-G are octave 0, H-Z carry on up the natural scale
    // (H = A, I = B, J = C one octave up...). Lowercase is the sharp.
    static const int8_t semi[7] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G
    int n, sharp = 0;
    if (note >= 'A' && note <= 'Z')
        n = note - 'A';
    else if (note >= 'a' && note <= 'z') {
        n = note - 'a';
        sharp = 1;
    }
    else
        return -1;
    int oct_off = n < 7 ? 0 : (n - 2) / 7;
    int total = oct_off * 12 + semi[n % 7] + sharp;
    int value = total % 12;
    int oct = clampi(octave + total / 12, 0, 8);
    return clampi(oct * 12 + value + 24, 0, 127);
}

static uint8_t midi_vel(int v) {
    return (v * 127 + 8) / 16;
}

static void note_start(sf_note_t *n) {
    if (n->ex) {
        sf_io_ex_note(n->note, n->vel, true);
        n->played = 1;
        return;
    }
    if (n->bent) sf_io_midi_pb(n->ch, n->pb & 127, (n->pb >> 7) & 127);
    sf_io_midi_on(n->ch, n->note, midi_vel(n->vel));
    n->played = 1;
}

static void note_stop(sf_note_t *n) {
    if (n->played) {
        if (n->ex)
            sf_io_ex_note(n->note, 0, false);
        else {
            sf_io_midi_off(n->ch, n->note);
            if (n->bent) sf_io_midi_pb(n->ch, 0, 64);
        }
    }
    n->active = 0;
}

// a note that lasts len frames. ex: a Disting EX voice (the N operator),
// otherwise MIDI on channel ch
static void note_push_to(bool ex, int ch, int note, int vel, int len, bool bent,
                         uint16_t pb, bool now) {
    sf_note_t *slot = NULL;
    for (int i = 0; i < SF_NOTES; i++) {
        sf_note_t *n = &sf.notes[i];
        // "Retrigger duplicates"
        if (n->active && n->ex == ex && n->ch == ch && n->note == note) note_stop(n);
        if (!n->active && !slot) slot = n;
    }
    if (!slot) {
        // full: give up the note with the least time left
        slot = &sf.notes[0];
        for (int i = 1; i < SF_NOTES; i++)
            if (sf.notes[i].len < slot->len) slot = &sf.notes[i];
        note_stop(slot);
    }
    slot->active = 1;
    slot->played = 0;
    slot->ex = ex;
    slot->ch = ch;
    slot->note = note;
    slot->vel = vel;
    slot->len = len;
    slot->bent = bent;
    slot->pb = pb;
    if (now) note_start(slot);
}

static void note_push(int ch, int note, int vel, int len, bool bent,
                      uint16_t pb, bool now) {
    note_push_to(false, ch, note, vel, len, bent, pb, now);
}

static void notes_run(void) {
    for (int i = 0; i < SF_NOTES; i++) {
        sf_note_t *n = &sf.notes[i];
        if (!n->active) continue;
        if (!n->played) note_start(n);
        if (n->len-- <= 0) note_stop(n);
    }
    // mono: one note per channel, a new one cuts the old one
    for (int i = 0; i < 16; i++) {
        sf_note_t *n = &sf.mono[i];
        if (!n->active) continue;
        if (n->len < 1) {
            note_stop(n);
            continue;
        }
        if (!n->played) note_start(n);
        n->len--;
    }
    for (int o = 0; o < 36; o++) {
        if (sf.gate[o] > 0 && --sf.gate[o] == 0) sf_io_gate(o, false);
    }
}

static void mono_push(int ch, int note, int vel, int len, bool now) {
    sf_note_t *n = &sf.mono[ch];
    if (n->active) note_stop(n);
    n->active = 1;
    n->played = 0;
    n->ch = ch;
    n->note = note;
    n->vel = vel;
    n->len = len;
    n->bent = 0;
    n->ex = 0;
    if (now && len > 0) note_start(n);
}

// 12 * log2(ratio) - semitone for the just intonation ratios
// 1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 16/9 15/8,
// as a 14 bit MIDI pitch bend (2 semitone range) and in CV units
static const uint16_t ji_pb[12] = { 8192, 8673, 8352, 8833, 7631, 8112,
                                    7792, 8272, 8753, 7551, 8032, 7711 };
static const int8_t ji_cv[12] = { 0, 16, 5, 21, -19, -3, -13, 3, 19, -21, -5, -16 };

////////////////////////////////////////////////////////////////////////////
// trigger + CV outputs

static bool out_ok(int out) {
    return (out >= 1 && out <= 4) || (out >= 10 && out <= 35);
}

static void cv_send(int out, uint16_t slew) {
    sf_io_cv(out, clampi(sf.cv_note[out] + sf.cv_bend[out], 0, 16383), slew);
}

// 1 V/octave from the MIDI note number: octave 0 C (note 24) is 0 V
static int16_t note_cv(int id, int offset) {
    int semis = id - 24;
    if (semis < 0) semis = 0;
    return (semis * 16384 + 60) / 120 + offset;
}

static void pulse(int out, uint16_t ms) {
    if (!ms) return;
    sf.gate[out] = 0;
    sf_io_trig(out, ms);
}

////////////////////////////////////////////////////////////////////////////
// timed events: bursts, shaped sequences and their note-offs

static void due(uint32_t t) {
    if (t < sf.next_due) sf.next_due = t;
}

static void off_send(uint8_t ex, int ch, int note) {
    if (ex == SF_OFF_JF_TR)
        sf_io_jf_tr(ch, 0);
    else if (ex)
        sf_io_ex_note(note, 0, false);
    else
        sf_io_midi_off(ch, note);
}

static void off_push(uint8_t ex, int ch, int note, uint32_t t) {
    for (int i = 0; i < SF_OFFS; i++) {
        if (!sf.offs[i].active) {
            sf.offs[i].active = 1;
            sf.offs[i].ex = ex;
            sf.offs[i].ch = ch;
            sf.offs[i].note = note;
            sf.offs[i].due = t;
            due(t);
            return;
        }
    }
    off_send(ex, ch, note);
}

static uint32_t seq_time(const sf_seq_t *s, int i) {
    uint32_t p = s->count < 2 ? 0 : ((uint32_t)i << 16) / s->count;
    uint32_t q = 65536 - p;
    uint32_t pos = p;
    // sqrt of a Q16 fraction, in Q16
#define SQ(v) ((v) >= 65536 ? 65536 : isqrt((uint32_t)(v) << 16))
    if (s->shape == 1) pos = 65536 - SQ(q);
    if (s->shape == 2) pos = SQ(p);
    if (s->shape == 3) pos = p < 32768 ? SQ(p / 2) : 65536 - SQ(q / 2);
    if (s->shape == 4) pos = p * s->pw / 36;
#undef SQ
    uint32_t t = (uint32_t)(((uint64_t)pos * s->dur) >> 16);
    if (t > s->dur - 1) t = s->dur - 1;
    return s->start + t;
}

static void seq_schedule(const sf_seq_t *proto) {
    sf_seq_t *s = NULL;
    for (int i = 0; i < SF_SEQS; i++)
        if (sf.seqs[i].active && sf.seqs[i].x == proto->x &&
            sf.seqs[i].y == proto->y)
            s = &sf.seqs[i];
    for (int i = 0; i < SF_SEQS && !s; i++)
        if (!sf.seqs[i].active) s = &sf.seqs[i];
    if (!s) s = &sf.seqs[0];
    *s = *proto;
    s->active = 1;
    s->idx = 0;
    s->start = sf.now;
    uint32_t frame_ms = sf_frame_us() / 1000;
    s->dur = (uint32_t)(s->dur < 1 ? 1 : s->dur) * frame_ms;
    if (s->dur < 1) s->dur = 1;
    due(sf.now);
}

static void burst_schedule(int x, int y, int count, uint32_t interval_us) {
    sf_burst_t *b = NULL;
    for (int i = 0; i < SF_BURSTS; i++)
        if (sf.bursts[i].active && sf.bursts[i].x == x && sf.bursts[i].y == y)
            b = &sf.bursts[i];
    for (int i = 0; i < SF_BURSTS && !b; i++)
        if (!sf.bursts[i].active) b = &sf.bursts[i];
    if (!b) b = &sf.bursts[0];
    b->active = 1;
    b->x = x;
    b->y = y;
    b->count = count;
    b->idx = 0;
    b->start = sf.now;
    b->interval_us = interval_us < 1000 ? 1000 : interval_us;
    due(sf.now);
}

static void cancel_timed(void) {
    for (int i = 0; i < SF_SEQS; i++) sf.seqs[i].active = 0;
    for (int i = 0; i < SF_BURSTS; i++) sf.bursts[i].active = 0;
}

////////////////////////////////////////////////////////////////////////////
// operators

static void run_op(char g, int x, int y, bool force);

static bool bang_output(char g) {
    char l = tolower((unsigned char)g);
    return l == 'd' || l == 'f' || l == 'u' || g == '^' || g == '&' || g == '~';
}

// orca.triggerNeighbors: run the operators around a bang written by a burst
static void trigger_neighbors(int x, int y, uint16_t *seen, int *nseen) {
    static const int8_t d[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (int k = 0; k < 4; k++) {
        int ox = x + d[k][0], oy = y + d[k][1];
        if (!inb(ox, oy)) continue;
        uint16_t id = oy * SF_W + ox;
        bool dup = false;
        for (int i = 0; i < *nseen; i++)
            if (seen[i] == id) dup = true;
        if (dup) continue;
        char g = G(ox, oy);
        if (!sf_is_op(g) || g == '*') continue;
        if (*nseen >= 24) return;
        seen[(*nseen)++] = id;
        run_op(g, ox, oy, true);
        if (!bang_output(g)) continue;
        if (G(ox, oy + 1) != '*') continue;
        trigger_neighbors(ox, oy + 1, seen, nseen);
        W(ox, oy + 1, '.');
    }
}

static void move(char g, int x, int y, int dx, int dy) {
    int nx = x + dx, ny = y + dy;
    if (!inb(nx, ny) || G(nx, ny) != '.') {
        W(x, y, '*');  // explode
        return;
    }
    W(x, y, '.');
    W(nx, ny, g);
    L(nx, ny);
}

// read the cells right of ; = $ up to the first empty one, locking them
static uint8_t read_msg(int x, int y, int from, char *msg) {
    uint8_t n = 0;
    for (int i = from; i <= 36; i++) {
        char c = G(x + i, y);
        L(x + i, y);
        if (c == '.' || !c) break;
        msg[n++] = c;
    }
    msg[n] = 0;
    return n;
}

static void op_trig_note(int out, int id, int offset, int vel, int ms) {
    sf.cv_note[out] = note_cv(id, offset);
    cv_send(out, 0);
    if (vel > 0) pulse(out, ms);
}

static void run_op(char g, int x, int y, bool force) {
    bool banged = force || has_bang(x, y);
    char lg = (g >= 'A' && g <= 'Z') ? g + 32 : g;

    switch (lg) {
        case 'a':
        case 'b':
        case 'm': {
            int a = LV(x - 1, y, 0, 0, 36), b = LV(x + 1, y, 0, 0, 36);
            int r = lg == 'a' ? a + b : lg == 'm' ? a * b : (b > a ? b - a : a - b);
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            out_s(x, y, K(r));
            break;
        }
        case 'c': {
            int rate = LV(x - 1, y, 0, 1, 36), mod = LV(x + 1, y, '8', 0, 36);
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            if (mod) out_s(x, y, K((sf.f / rate) % mod));
            break;
        }
        case 'd': {
            int rate = LV(x - 1, y, 0, 1, 36), mod = LV(x + 1, y, '8', 0, 36);
            L(x - 1, y);
            L(x + 1, y);
            bang_out(x, y, (mod && sf.f % (mod * rate) == 0) || mod == 1);
            break;
        }
        case 'e': move(g, x, y, 1, 0); break;
        case 'n': move(g, x, y, 0, -1); break;
        case 's': move(g, x, y, 0, 1); break;
        case 'w': move(g, x, y, -1, 0); break;
        case 'f': {
            bool eq = LG(x - 1, y, 0) == LG(x + 1, y, 0);
            L(x - 1, y);
            L(x + 1, y);
            bang_out(x, y, eq);
            break;
        }
        case 'g': {
            int len = LV(x - 1, y, 0, 1, 36);
            int ox = LV(x - 3, y, 0, 0, 36), oy = LV(x - 2, y, 0, 0, 36) + 1;
            for (int o = 0; o < len; o++) W(x + ox + o, y + oy, G(x + o + 1, y));
            L(x - 3, y);
            L(x - 2, y);
            L(x - 1, y);
            for (int o = 0; o < len; o++) {
                L(x + o + 1, y);
                L(x + ox + o, y + oy);
            }
            break;
        }
        case 'h': L(x, y + 1); break;
        case 'i': {
            int step = LV(x - 1, y, '1', 0, 36), mod = LV(x + 1, y, 0, 0, 36);
            int v = LV(x, y + 1, 0, 0, 36);
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            out_s(x, y, K((v + step) % (mod > 0 ? mod : 36)));
            break;
        }
        case 'j': {
            char v = G(x, y - 1);
            if (v != g) {
                int i = 0;
                while (inb(x, y + i))
                    if (G(x, y + ++i) != g) break;
                L(x, y - 1);
                L(x, y + i);
                W(x, y + i, v);
            }
            break;
        }
        case 'k': {
            int len = LV(x - 1, y, 0, 1, 36);
            L(x - 1, y);
            for (int o = 0; o < len; o++) {
                char key = G(x + o + 1, y);
                L(x + o + 1, y);
                if (key == '.' || !key) continue;
                L(x + o + 1, y + 1);
                W(x + o + 1, y + 1, var_get(key));
            }
            break;
        }
        case 'l': {
            char a = LG(x - 1, y, 0), b = LG(x + 1, y, 0);
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            if (a != '.' && b != '.') {
                int va = V(a), vb = V(b);
                out_s(x, y, K(va < vb ? va : vb));
            }
            else
                W(x, y + 1, '.');
            break;
        }
        case 'o': {
            int ox = LV(x - 2, y, 0, 0, 36), oy = LV(x - 1, y, 0, 0, 36);
            char v = G(x + ox + 1, y + oy);
            L(x - 2, y);
            L(x - 1, y);
            L(x, y + 1);
            Pk(x + ox + 1, y + oy);
            W(x, y + 1, v);
            break;
        }
        case 'p': {
            int len = LV(x - 1, y, 0, 1, 36), key = LV(x - 2, y, 0, 0, 36);
            for (int o = 0; o < len; o++) L(x + o, y + 1);
            L(x - 2, y);
            L(x - 1, y);
            L(x + 1, y);
            Pk(x + key % len, y + 1);
            W(x + key % len, y + 1, G(x + 1, y));
            break;
        }
        case 'q': {
            int len = LV(x - 1, y, 0, 1, 36);
            int ox = LV(x - 3, y, 0, 0, 36), oy = LV(x - 2, y, 0, 0, 36);
            for (int o = 0; o < len; o++)
                W(x + o - len + 1, y + 1, G(x + ox + o + 1, y + oy));
            L(x - 3, y);
            L(x - 2, y);
            L(x - 1, y);
            for (int o = 0; o < len; o++) {
                Pk(x + ox + o + 1, y + oy);
                L(x + o - len + 1, y + 1);
            }
            break;
        }
        case 'r': {
            int mn = LV(x - 1, y, 0, 0, 36), mx = LV(x + 1, y, 0, 0, 36);
            // JS: parseInt(Math.random() * (max - min) + min)
            int range = (mx > 0 ? mx : 36) - mn;
            // the result is always positive, so parseInt is floor (>> on int64)
            int64_t scaled = ((int64_t)rnd() * range) >> 32;
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            out_s(x, y, K(mn + (int)scaled));
            break;
        }
        case 't': {
            int len = LV(x - 1, y, 0, 1, 36), key = LV(x - 2, y, 0, 0, 36);
            for (int o = 0; o < len; o++) L(x + o + 1, y);
            L(x - 2, y);
            L(x - 1, y);
            L(x, y + 1);
            Pk(x + key % len + 1, y);     // the value it picked
            W(x, y + 1, G(x + key % len + 1, y));
            break;
        }
        case 'u': {
            int step = LV(x - 1, y, '1', 0, 36), mx = LV(x + 1, y, '8', 1, 36);
            L(x - 1, y);
            L(x + 1, y);
            bang_out(x, y, (step * (sf.f + mx - 1)) % mx + step >= (uint32_t)mx);
            break;
        }
        case 'v': {
            char w = G(x - 1, y), r = G(x + 1, y);
            L(x - 1, y);
            L(x + 1, y);
            if (w == '.' && r != '.') {
                L(x, y + 1);
                W(x, y + 1, var_get(r));
            }
            else if (w != '.')
                var_set(w, r);
            break;
        }
        case 'x': {
            int ox = LV(x - 2, y, 0, 0, 36), oy = LV(x - 1, y, 0, 0, 36) + 1;
            L(x - 2, y);
            L(x - 1, y);
            L(x + 1, y);
            L(x + ox, y + oy);
            W(x + ox, y + oy, G(x + 1, y));
            break;
        }
        case 'y': {
            char v = G(x - 1, y);
            if (v != g) {
                int i = 0;
                while (inb(x + i, y))
                    if (G(x + ++i, y) != g) break;
                L(x - 1, y);
                L(x + i, y);
                W(x + i, y, v);
            }
            break;
        }
        case 'z': {
            int rate = LV(x - 1, y, '1', 0, 36), target = LV(x + 1, y, 0, 0, 36);
            int v = LV(x, y + 1, 0, 0, 36);
            int mod = v <= target - rate ? rate : v >= target + rate ? -rate : target - v;
            L(x - 1, y);
            L(x + 1, y);
            L(x, y + 1);
            out_s(x, y, K(v + mod));
            break;
        }

        case '*': W(x, y, '.'); break;
        case '#':
            for (int i = x + 1; i <= SF_W; i++) {
                L(i, y);
                if (G(i, y) == '#') break;
            }
            L(x, y);
            break;

        case '^': {
            int chance = LV(x + 1, y, 'i', 0, 35);
            L(x + 1, y);
            // JS: Math.random() < chance / 35
            bang_out(x, y, has_bang(x, y) && (uint64_t)rnd() * 35 < ((uint64_t)chance << 32));
            break;
        }
        case '&': {
            int rate = LV(x - 1, y, '1', 1, 36), len = LV(x + 1, y, '4', 1, 36);
            int phase = LV(x, y - 1, '0', 0, 35) % len;
            L(x - 1, y);
            L(x + 1, y);
            L(x, y - 1);
            bang_out(x, y, sf.f % rate == 0 && (int)((sf.f / rate) % len) == phase);
            break;
        }
        case '~': {
            bool trig = force || G(x - 1, y) == '*' || G(x + 1, y) == '*' ||
                        G(x, y - 1) == '*';
            int rep = LV(x + 1, y, '4', 0, 35);
            int div = LV(x + 2, y, '1', 1, 35), mul = LV(x + 3, y, '1', 1, 35);
            L(x + 1, y);
            L(x + 2, y);
            L(x + 3, y);
            if (trig && rep >= 1)
                burst_schedule(x, y, rep, sf_frame_us() * div / mul);
            if (!force) bang_out(x, y, false);
            break;
        }
        case '|': {
            int w = LV(x - 2, y, '8', 1, 35), h = LV(x - 1, y, '4', 1, 35);
            if (banged) {
                bool muted = G(x, y + 1) == '#';
                for (int k = 1; k <= h; k++) {
                    for (int e = 0; e < 2; e++) {
                        int gx = x + (e ? w + 1 : 0);
                        if (!inb(gx, y + k)) continue;
                        if (!muted)
                            W(gx, y + k, '#');
                        else if (G(gx, y + k) == '#') {
                            W(gx, y + k, '.');
                            // "The # was already queued to run this frame;
                            // lock it so the row runs now."
                            L(gx, y + k);
                        }
                    }
                    if (!muted)
                        for (int dx = 1; dx <= w; dx++) L(x + dx, y + k);
                }
            }
            L(x - 2, y);
            L(x - 1, y);
            break;
        }
        case '+': {
            char key = LG(x - 1, y, '0');
            static const char c_major[] = "101011010101";
            uint16_t m = 0;
            for (int p = 0; p < 12; p++) {
                if (LV(x + 1 + p, y, c_major[p], 0, 1) > 0) m |= 1 << p;
                L(x + 1 + p, y);
            }
            L(x - 1, y);
            int i = var_i(key);
            if (i >= 0) {
                sf.var[i] = 1;
                sf.mask[i] = m;
            }
            break;
        }

        case '$': {
            char msg[40];
            uint8_t n = read_msg(x, y, 1, msg);
            if (banged && n) sf_command(msg, n);
            break;
        }
        // UDP / OSC in Seaflake. The Teletype has no network: they read and
        // lock their message like Seaflake does, and send nothing
        case ';':
        case '=': {
            char msg[40];
            read_msg(x, y, g == '=' ? 2 : 1, msg);
            if (g == '=') L(x + 1, y);
            break;
        }

        // > Disting EX, < ER-301, , Just Friends: a function letter, then
        // fixed operand cells
        case '>':
        case '<':
        case ',': {
            char sel = G(x + 1, y);
            L(x + 1, y);
            const char *fmt = sf_dev_format(g, sel);
            if (!fmt) break;
            uint8_t n = sf_dev_cells(fmt);
            if (g == '>' && (sel == 'n' || sel == 'N' || sel == '@')) {
                // N: a note on the Disting EX's voices, octave (default 4),
                // note, velocity (default f), one frame long, like : with no
                // channel. >@: the same note as a shaped run, like @
                for (uint8_t i = 0; i < n; i++) L(x + 2 + i, y);
                char nc = G(x + 3, y);
                if (!banged || nc == '.' || !nc || isdigit((unsigned char)nc)) break;
                int id = sf_note_id(nc, LV(x + 2, y, '4', 0, 8));
                if (id < 0) break;
                int vel = LV(x + 4, y, 'f', 0, 16);
                if (sel != '@') {
                    note_push_to(true, 0, id, vel, 1, false, 0, force);
                    break;
                }
                sf_seq_t q;
                memset(&q, 0, sizeof(q));
                q.trig = SF_SEQ_EX;
                q.x = x;
                q.y = y;
                q.note = id;
                q.vel = vel;
                q.shape = LV(x + 5, y, '1', 1, 4);
                q.count = LV(x + 6, y, '1', 0, 35);
                q.dur = LV(x + 7, y, '1', 1, 36);
                q.pw = LV(x + 8, y, 'i', 1, 35);
                if (q.count >= 1) seq_schedule(&q);
                break;
            }
            if (g == ',' && sel == '@') {
                // ,@ : Just Friends notes as @'s shaped run (JF.NOTE, no
                // note off: Just Friends' own envelope ends each one)
                for (uint8_t i = 0; i < n; i++) L(x + 2 + i, y);
                char nc = G(x + 3, y);
                if (!banged || nc == '.' || !nc || isdigit((unsigned char)nc)) break;
                int id = sf_note_id(nc, LV(x + 2, y, '4', 0, 8));
                if (id < 0) break;
                sf_seq_t q;
                memset(&q, 0, sizeof(q));
                q.trig = SF_SEQ_JF;
                q.x = x;
                q.y = y;
                q.note = id;
                q.vel = LV(x + 4, y, 'a', 0, 16);
                q.shape = LV(x + 5, y, '1', 1, 4);
                q.count = LV(x + 6, y, '1', 0, 35);
                q.dur = LV(x + 7, y, '1', 1, 36);
                q.pw = LV(x + 8, y, 'i', 1, 35);
                if (q.count >= 1) seq_schedule(&q);
                break;
            }
            char cells[12];
            for (uint8_t i = 0; i < n; i++) {
                cells[i] = G(x + 2 + i, y);
                if (!cells[i]) cells[i] = '.';
                L(x + 2 + i, y);
            }
            if (!banged) break;
            int16_t result = 0;
            int8_t r = sf_io_dev(g, tolower((unsigned char)sel), cells, &result);
            sf.error = r < 0;
            // ,t with the state cell empty: a trigger, JF.TR 1 now and
            // JF.TR 0 one $pulse later
            if (r == 0 && g == ',' && tolower((unsigned char)sel) == 't' && cells[1] == '.') {
                const char c0 = cells[0];
                const int c = c0 >= '0' && c0 <= '9' ? c0 - '0'
                            : isalpha((unsigned char)c0) ? tolower((unsigned char)c0) - 'a' + 10 : 1;
                off_push(SF_OFF_JF_TR, c, 0, sf.now + sf.pulse_ms);
            }
            if (r == 1) {
                L(x, y + 1);
                W(x, y + 1, K(clampi(result, 0, 35)));
            }
            break;
        }

        // MIDI and trigger notes: channel/output, octave, note, velocity, length
        case ':':
        case '%':
        case '"':
        case '/': {
            char c = G(x + 1, y), o = G(x + 2, y), n = G(x + 3, y);
            for (int i = 1; i <= 5; i++) L(x + i, y);
            if (!banged) break;
            if (c == '.' || o == '.' || n == '.' || !n || isdigit((unsigned char)n)) break;
            int ch = LV(x + 1, y, 0, 0, 36);
            int id = sf_note_id(n, LV(x + 2, y, 0, 0, 8));
            int vel = LV(x + 4, y, 'f', 0, 16);
            if (id < 0) break;
            if (g == ':' || g == '%') {
                int len = LV(x + 5, y, '1', 0, 32);
                if (ch > 15) break;
                if (g == ':')
                    note_push(ch, id, vel, len, false, 0, force);
                else
                    mono_push(ch, id, vel, len, force);
            }
            else if (out_ok(ch)) {
                char lc = G(x + 5, y);
                bool empty = lc == '.' || lc == '*' || !lc;
                if (g == '"' || empty)
                    op_trig_note(ch, id, 0, vel, LV(x + 5, y, '1', 0, 35) * sf.pulse_ms);
                else {
                    // gate: held high for a number of frames, like a MIDI note
                    int frames = LV(x + 5, y, 0, 0, 32);
                    sf.cv_note[ch] = note_cv(id, 0);
                    cv_send(ch, 0);
                    if (vel > 0 && frames > 0) {
                        if (sf.gate[ch] == 0) sf_io_gate(ch, true);
                        sf.gate[ch] = frames + 1;
                    }
                }
            }
            break;
        }
        // just intonation note
        case '(':
        case '[': {
            char n = G(x + 3, y);
            for (int i = 1; i <= 5; i++) L(x + i, y);
            if (!banged) break;
            if (n == '.' || !n || isdigit((unsigned char)n)) break;
            int ch = LV(x + 1, y, g == '(' ? '0' : '1', 0, g == '(' ? 15 : 35);
            int id = sf_note_id(n, LV(x + 2, y, '4', 0, 8));
            int vel = LV(x + 4, y, 'f', 0, 16);
            if (id < 0) break;
            int pc = (id - 24) % 12;
            if (g == '(')
                note_push(ch, id, vel, LV(x + 5, y, '1', 0, 32), true, ji_pb[pc], force);
            else if (out_ok(ch))
                op_trig_note(ch, id, ji_cv[pc], vel, LV(x + 5, y, '1', 0, 35) * sf.pulse_ms);
            break;
        }
        // note from a scale stored by +
        case ')':
        case ']': {
            for (int i = 1; i <= 6; i++) L(x + i, y);
            if (!banged) break;
            int vi = var_i(LG(x + 4, y, '0'));
            if (vi < 0 || sf.var[vi] != 1 || !sf.mask[vi]) break;
            uint8_t notes[12], nn = 0;
            for (int p = 0; p < 12; p++)
                if (sf.mask[vi] & (1 << p)) notes[nn++] = p;
            int degree = LV(x + 3, y, '1', 1, 35) - 1;
            int oct = LV(x + 2, y, '4', 0, 8) + degree / nn;
            int id = clampi(24 + oct * 12 + notes[degree % nn], 0, 127);
            int ch = LV(x + 1, y, g == ')' ? '0' : '1', 0, g == ')' ? 15 : 35);
            int vel = LV(x + 5, y, 'f', 0, 16);
            if (g == ')')
                note_push(ch, id, vel, LV(x + 6, y, '1', 0, 32), false, 0, force);
            else if (out_ok(ch))
                op_trig_note(ch, id, 0, vel, LV(x + 6, y, '1', 0, 35) * sf.pulse_ms);
            break;
        }
        // shaped sequence
        case '@':
        case '{': {
            char n = G(x + 3, y);
            for (int i = 1; i <= 8; i++) L(x + i, y);
            if (n == '.' || !n || isdigit((unsigned char)n)) break;
            sf_seq_t s;
            memset(&s, 0, sizeof(s));
            s.trig = g == '{';
            s.x = x;
            s.y = y;
            s.ch = LV(x + 1, y, s.trig ? '1' : '0', 0, s.trig ? 35 : 15);
            int id = sf_note_id(n, LV(x + 2, y, '4', 0, 8));
            s.vel = LV(x + 4, y, 'f', 0, 16);
            s.shape = LV(x + 5, y, '1', 1, 4);
            s.count = LV(x + 6, y, '1', 0, 35);
            s.dur = LV(x + 7, y, '1', 1, 36);
            s.pw = LV(x + 8, y, 'i', 1, 35);
            if (id < 0 || (s.trig && !out_ok(s.ch))) break;
            s.note = id;
            s.cv = note_cv(id, 0);
            if (banged && s.count >= 1) seq_schedule(&s);
            break;
        }
        // - trigger: a pulse ($pulse ms) on the output. No CV, no other inlets
        case '-': {
            char c = G(x + 1, y);
            L(x + 1, y);
            if (!banged || c == '.' || !c) break;
            int out = LV(x + 1, y, 0, 0, 35);
            if (out_ok(out)) pulse(out, sf.pulse_ms);
            break;
        }
        // _ trigger + CV: output, octave (default 4), note -> pitch CV and a
        // pulse ($pulse ms). No velocity, no length, no slew
        case '_': {
            char c = G(x + 1, y), n = G(x + 3, y);
            for (int i = 1; i <= 3; i++) L(x + i, y);
            if (!banged || c == '.' || !c || n == '.' || !n || isdigit((unsigned char)n)) break;
            int out = LV(x + 1, y, 0, 0, 35);
            int id = sf_note_id(n, LV(x + 2, y, '4', 0, 8));
            if (id < 0 || !out_ok(out)) break;
            op_trig_note(out, id, 0, 1, sf.pulse_ms);
            break;
        }
        // } shaped trigger: @'s run of steps as pulses only. output (default
        // 1), shape 1-4, repeats, duration in frames, width (shape 4)
        case '}': {
            for (int i = 1; i <= 5; i++) L(x + i, y);
            sf_seq_t q;
            memset(&q, 0, sizeof(q));
            q.trig = SF_SEQ_TRIG;
            q.x = x;
            q.y = y;
            q.ch = LV(x + 1, y, '1', 0, 35);
            q.shape = LV(x + 2, y, '1', 1, 4);
            q.count = LV(x + 3, y, '1', 0, 35);
            q.dur = LV(x + 4, y, '1', 1, 36);
            q.pw = LV(x + 5, y, 'i', 1, 35);
            if (banged && q.count >= 1 && out_ok(q.ch)) seq_schedule(&q);
            break;
        }
        // ` loadbang: a bang below on the first frame after a patch is
        // loaded (at power-on, once the Disting EX / ER-301 have answered)
        case '`': bang_out(x, y, sf.loadbang == 1); break;

        case '!':
        case '\'': {
            char c = G(x + 1, y), k = G(x + 2, y);
            for (int i = 1; i <= 3; i++) L(x + i, y);
            if (!banged || c == '.') break;
            int ch = LV(x + 1, y, 0, 0, 36);
            int raw = LV(x + 3, y, 0, 0, 36);
            if (g == '!') {
                if (k == '.' || ch > 15) break;
                sf_io_midi_cc(ch, LV(x + 2, y, 0, 0, 36), (127 * raw + 34) / 35);
            }
            else if (out_ok(ch)) {
                // CV level: 0-z is 0 to 10 V, slew in pulse units. CV only:
                // the trigger is left alone (only " and / fire it)
                sf.cv_note[ch] = (int32_t)(raw > 35 ? 35 : raw) * 16383 / 35;
                sf.cv_bend[ch] = 0;
                cv_send(ch, LV(x + 2, y, 0, 0, 35) * sf.pulse_ms);
            }
            break;
        }
        case '?':
        case '\\': {
            char c = G(x + 1, y), l = G(x + 2, y);
            for (int i = 1; i <= 3; i++) L(x + i, y);
            if (!banged || c == '.' || l == '.') break;
            int lsb = (127 * LV(x + 2, y, 0, 0, 36) + 34) / 35;
            int msb = (127 * LV(x + 3, y, 0, 0, 36) + 34) / 35;
            if (lsb > 127) lsb = 127;
            if (msb > 127) msb = 127;
            if (g == '?')
                sf_io_midi_pb(LV(x + 1, y, 0, 0, 15), lsb, msb);
            else {
                int out = LV(x + 1, y, 0, 0, 35);
                if (!out_ok(out)) break;
                // bend of +/- 2 semitones (273 CV units) around the note; CV only
                sf.cv_bend[out] = (((msb << 7) | lsb) - 8192) * 273 / 8192;
                cv_send(out, 0);
            }
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// > and < operand layouts (see sf.h). Shared with sf_ii.c, which sends them.

// The Disting EX functions (2026-11-19 layout, see docs/manual "What
// changed"): the value comes first, the dual-algorithm side (if any) last,
// so a plain single-algorithm setup leaves the last cell empty.
static const char *const ex_fmt[26] = {
    /* a alg     */ "nsn", /* b sel bus */ "aww", /* c ctrl    */ "nl",
    /* d note ch */ "nNv", /* e rec     */ "n",   /* f vox pit */ "nN",
    /* g AL.P    */ "N",   /* h AL.CLK  */ "",    /* i min     */ "ws",
    /* j max     */ "ws",  /* k ch      */ "n",   /* l preset  */ "ws",
    /* m midi    */ "aww", /* n N       */ "Nv",  /* o note off */ "nN",
    /* p EX.P    */ "wws", /* q looper  */ "an",  /* r reset   */ "",
    /* s save    */ "ws",  /* t trig    */ "nv",  /* u (none)  */ 0,  
    /* v EX.PV   */ "wls", /* w voice   */ "nNv", /* x all off */ "",
    /* y play    */ "n",   /* z Z knob  */ "sn"
};
// >@ : N with @'s shape: octave + note, velocity, shape, repeats, duration, width
static const char ex_at_fmt[] = "Nvnnnn";
static const char *const er_fmt[26] = {
    /* a */ 0,    /* b */ 0,    /* c cv     */ "wl", /* d */ 0,
    /* e */ 0,    /* f */ 0,    /* g toggle */ "w",  /* h */ 0,
    /* i */ 0,    /* j */ 0,    /* k */ 0,    /* l polarity */ "wn",
    /* m time  */ "ww", /* n cv note */ "wN", /* o offset */ "wl",
    /* p pulse */ "w",  /* q */ 0,    /* r */ 0,    /* s cv set */ "wl",
    /* t tr    */ "wn", /* u */ 0,    /* v */ 0,    /* w slew   */ "ww",
    /* x */ 0,    /* y */ 0,    /* z */ 0
};

// , Just Friends (Teletype's JF.* ops). Operand letters as above, plus
// V = a Just Friends velocity (0-g = 0-8 V, empty = a = 5 V) and b = a
// bipolar level (0-z = -5..+5 V, empty or i = 0 V). Channels: 1-6, 0 = all
// six, 7-c = channels 1-6 of the second Just Friends, empty = 1
static const char *const jf_fmt[26] = {
    /* a address */ "n",   /* b */ 0,            /* c */ 0,
    /* d god     */ "n",   /* e select  */ "n",  /* f pitch   */ "nN",
    /* g read    */ "a",   /* h */ 0,            /* i */ 0,
    /* j tune    */ "nnn", /* k tick    */ "w",  /* l */ 0,
    /* m mode    */ "n",   /* n note    */ "NV", /* o rmode   */ "n",
    /* p poly    */ "NV",  /* q qt      */ "n",  /* r vtr     */ "nV",
    /* s shift   */ "N",   /* t tr      */ "nn", /* u run     */ "b",
    /* v vox     */ "nNV", /* w */ 0,            /* x poly reset */ "",
    /* y */ 0,             /* z */ 0
};
static const char jf_at_fmt[] = "NVnnnn";

const char *sf_dev_format(char dev, char sel) {
    if (dev == '>' && sel == '@') return ex_at_fmt;
    if (dev == ',' && sel == '@') return jf_at_fmt;
    char c = tolower((unsigned char)sel);
    if (c < 'a' || c > 'z') return NULL;
    return dev == '>' ? ex_fmt[c - 'a'] : dev == ',' ? jf_fmt[c - 'a'] : er_fmt[c - 'a'];
}

uint8_t sf_dev_cells(const char *f) {
    uint8_t n = 0;
    for (; *f; f++) n += (*f == 'w' || *f == 'N') ? 2 : 1;
    return n;
}

bool sf_is_op(char g) {
    if ((g >= 'a' && g <= 'z') || (g >= 'A' && g <= 'Z')) return true;
    return g && strchr("*#$:%!?()+@^&~|=;\"/'\\[]{<>-_}`,", g) != NULL;
}

const char *sf_op_name(char g) {
    static const char *const letters[26] = {
        "ADD",   "SUB",   "CLOCK", "DELAY", "EAST", "IF",    "GEN",
        "HALT",  "INC",   "JUMP",  "KONKAT", "LESS", "MULT",  "NORTH",
        "READ",  "PUSH",  "QUERY", "RANDOM", "SOUTH", "TRACK", "UCLID",
        "VAR",   "WEST",  "WRITE", "JYMP",  "LERP"
    };
    if (g >= 'a' && g <= 'z') return letters[g - 'a'];
    if (g >= 'A' && g <= 'Z') return letters[g - 'A'];
    static const char sym[] = "*#$:%!?()+@^&~|=;\"/'\\[]{><-_}`,";
    static const char *const names[] = {
        "BANG",   "COMMNT", "CMD",    "MIDI",   "MONO",   "CC",
        "PBEND",  "JI",     "SCALE",  "SCLDEF", "SHAPE",  "PROB",
        "PHASE",  "BURST",  "MUTE",   "OSC",    "UDP",    "TRIG NOTE",
        "GATE",   "CV",     "CVBEND", "TR JI",  "TR SCL", "TR SHP",
        "DISTNG", "ER-301", "TRIG",   "TRIG+CV", "TRIG SHAPE", "LOADBANG",
        "JUST FRIENDS"
    };
    const char *p = g ? strchr(sym, g) : NULL;
    return p ? names[p - sym] : "";
}

////////////////////////////////////////////////////////////////////////////
// names of inlets and outlets, for the status line

// > and <: what the selector does, and each operand cell
static const char *const ex_sel[26] = {
    "ALGORITHM", "SELECT BUS", "CONTROLLER", "NOTE ON CH", "WAV RECORD", "VOICE PITCH",
    "AUGUSTUS PIT", "AUGUSTUS CLK", "PARAM MIN", "PARAM MAX", "NOTE CHANNEL",
    "LOAD PRESET", "MIDI", "N", "NOTE OFF", "PARAM", "LOOPER",
    "RESET PRESET", "SAVE PRESET", "TRIGGER", 0, "PARAM LEVEL",
    "VOICE NOTE", "ALL NOTES OFF", "WAV PLAY", "Z KNOB"
};
// one name per operand letter of ex_fmt (wide values and notes use 2 cells)
static const char *const ex_ops[26][3] = {
    { "ALGORITHM", "SIDE", "ALGORITHM 2" }, { "BUS ACTION", "VALUE 1", "VALUE 2" },
    { "CONTROLLER", "LEVEL" }, { "CHANNEL", "NOTE", "VELOCITY" }, { "1 REC 0 STOP" },
    { "VOICE", "NOTE" }, { "NOTE" }, { 0 }, { "PARAM", "SIDE" }, { "PARAM", "SIDE" },
    { "CHANNEL" }, { "PRESET", "SIDE" }, { "MIDI ACTION", "VALUE 1", "VALUE 2" },
    { "NOTE", "VELOCITY" }, { "CHANNEL", "NOTE" },
    { "PARAM", "VALUE", "SIDE" }, { "LOOP ACTION", "LOOP" }, { 0 },
    { "PRESET", "SIDE" }, { "VOICE", "VELOCITY" }, { 0 },
    { "PARAM", "LEVEL", "SIDE" }, { "VOICE", "NOTE", "VELOCITY" }, { 0 },
    { "1 PLAY 0 STOP" }, { "KNOB 1/2", "LEVEL" }
};
// >@ (N@), one name per operand of ex_at_fmt
static const char *const ex_at_ops[6] = { "NOTE", "VELOCITY", "SHAPE 1-4",
                                          "REPEATS", "DURATION", "PULSE WIDTH" };
static const char *const er_sel[26] = {
    0, 0, "CV LEVEL", 0, 0, 0, "TR TOGGLE", 0, 0, 0, 0, "TR POLARITY",
    "PULSE TIME", "CV NOTE", "CV OFFSET", "TR PULSE", 0, 0, "CV SET",
    "TR STATE", 0, 0, "CV SLEW", 0, 0, 0
};
static const char *const er_ops[26] = {
    0, 0, "LEVEL", 0, 0, 0, 0, 0, 0, 0, 0, "POLARITY 0/1", "PULSE MS",
    "NOTE", "LEVEL", 0, 0, 0, "LEVEL", "STATE 0/1", 0, 0, "SLEW MS", 0, 0, 0
};

static const char *const jf_sel[26] = {
    "ADDRESS", 0, 0, "GOD 432HZ", "SELECT JF", "VOICE PITCH", "READ KNOB", 0, 0,
    "TUNE RATIO", "TICK CLOCK", 0, "SYNTH MODE", "NOTE", "RUN MODE", "POLY NOTE",
    "QUANTIZE", "VEL TRIGGER", "SHIFT", "TRIGGER", "RUN LEVEL", "VOICE NOTE", 0,
    "POLY RESET", 0, 0
};
static const char *const jf_ops[26][3] = {
    { "1/2" }, { 0 }, { 0 }, { "1 ON 0 OFF" }, { "1/2" }, { "CHANNEL", "NOTE" },
    { "STRTCFIO" }, { 0 }, { 0 }, { "CHANNEL", "NUMERATOR", "DENOMINATOR" },
    { "DIV/BPM" }, { 0 }, { "1 SYNTH 0 OFF" }, { "NOTE", "VELOCITY" },
    { "1 ON 0 OFF" }, { "NOTE", "VELOCITY" }, { "DIVISIONS" }, { "CHANNEL", "VELOCITY" },
    { "NOTE" }, { "CHANNEL", "STATE" }, { "LEVEL I=0V" }, { "CHANNEL", "NOTE", "VELOCITY" },
    { 0 }, { 0 }, { 0 }, { 0 }
};

static const char *join(char *buf, uint8_t max, const char *a, const char *b) {
    uint8_t n = 0;
    for (; *a && n < max - 1; a++) buf[n++] = *a;
    if (b && *b && n < max - 2) buf[n++] = ' ';
    for (; b && *b && n < max - 1; b++) buf[n++] = *b;
    buf[n] = 0;
    return buf;
}

// a device operator's operand cell k (0 = the selector)
static const char *dev_port(char g, int ox, int oy, int k, char *buf, uint8_t max) {
    const char *dev = g == '>' ? "EX" : g == ',' ? "JF" : "ER";
    char sel = tolower((unsigned char)G(ox + 1, oy));
    const char *fmt = sf_dev_format(g, sel);
    const bool at = (g == '>' || g == ',') && sel == '@';
    int si = (sel >= 'a' && sel <= 'z') ? sel - 'a' : -1;
    if (k == 0) {
        const char *name = at ? (g == ',' ? "NOTE@" : "N@")
                           : si < 0 ? 0 : (g == '>' ? ex_sel[si] : g == ',' ? jf_sel[si] : er_sel[si]);
        return join(buf, max, dev, name ? name : "FUNCTION A-Z");
    }
    if (!fmt) return 0;
    int cell = 1;
    for (int i = 0; fmt[i]; i++) {
        int w = (fmt[i] == 'w' || fmt[i] == 'N') ? 2 : 1;
        if (k >= cell && k < cell + w) {
            const char *name;
            if (at)
                name = i < 6 ? ex_at_ops[i] : 0;
            else if (g == '>')
                name = i < 3 ? ex_ops[si][i] : 0;
            else if (g == ',')
                name = i < 3 ? jf_ops[si][i] : 0;
            else
                name = i == 0 ? "PORT" : er_ops[si];
            if (fmt[i] == 'N') name = k == cell ? "OCTAVE" : "NOTE";
            else if (fmt[i] == 'w') {
                join(buf, max, dev, name);
                return join(buf, max, buf, k == cell ? "HI" : "LO");
            }
            return join(buf, max, dev, name ? name : "");
        }
        cell += w;
    }
    return 0;
}

// the name of cell (dx, dy) relative to operator g at ox, oy, or NULL
static const char *port_of(char g, int ox, int oy, int dx, int dy, char *buf, uint8_t max) {
    static const char *const notes12[12] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
    char lg = (g >= 'A' && g <= 'Z') ? g + 32 : g;
    char op[2] = { (char)toupper((unsigned char)g), 0 };
    const char *n = 0;
    if (dy == 0 && dx < 0) {
        int l = -dx;  // 1 = just left
        switch (lg) {
            case 'a': case 'b': case 'm': case 'f': case 'l': n = l == 1 ? "INPUT A" : 0; break;
            case 'c': case 'd': case 'z': case '&': n = l == 1 ? "RATE" : 0; break;
            case 'i': n = l == 1 ? "STEP" : 0; break;
            case 'r': n = l == 1 ? "MINIMUM" : 0; break;
            case 'u': n = l == 1 ? "STEPS" : 0; break;
            case 'y': case 'j': n = l == 1 && lg == 'y' ? "INPUT" : 0; break;
            case 'v': n = l == 1 ? "WRITE NAME" : 0; break;
            case 'k': n = l == 1 ? "COUNT" : 0; break;
            case 'g': case 'q': n = l == 1 ? "LENGTH" : l == 2 ? "Y OFFSET" : l == 3 ? "X OFFSET" : 0; break;
            case 'o': case 'x': n = l == 1 ? "Y OFFSET" : l == 2 ? "X OFFSET" : 0; break;
            case 'p': case 't': n = l == 1 ? "LENGTH" : l == 2 ? "KEY" : 0; break;
            case '|': n = l == 1 ? "HEIGHT" : l == 2 ? "WIDTH" : 0; break;
            case '+': n = l == 1 ? "SCALE NAME" : 0; break;
        }
    }
    else if (dy == 0 && dx > 0) {
        int r = dx;
        static const char *const midi5[5] = { "CHANNEL", "OCTAVE", "NOTE", "VELOCITY", "LENGTH" };
        static const char *const trig5[5] = { "OUTPUT", "OCTAVE", "NOTE", "VELOCITY", "PULSES" };
        static const char *const shape8[8] = { "CHANNEL", "OCTAVE", "NOTE", "VELOCITY",
                                               "SHAPE 1-4", "REPEATS", "DURATION", "PULSE WIDTH" };
        static const char *const scale6[6] = { "CHANNEL", "OCTAVE", "DEGREE", "SCALE NAME",
                                               "VELOCITY", "LENGTH" };
        switch (lg) {
            case 'a': case 'b': case 'm': case 'f': case 'l': n = r == 1 ? "INPUT B" : 0; break;
            case 'c': case 'd': case 'i': n = r == 1 ? "MODULO" : 0; break;
            case 'r': n = r == 1 ? "MAXIMUM" : 0; break;
            case 'u': case '&': n = r == 1 ? "LENGTH" : 0; break;
            case 'z': n = r == 1 ? "TARGET" : 0; break;
            case 'v': n = r == 1 ? "READ NAME/VAL" : 0; break;
            case 'x': case 'p': n = r == 1 ? "VALUE" : 0; break;
            case 'y': n = G(ox + r, oy) != g ? "OUTPUT" : 0; break;
            case 'g': n = r <= LV(ox - 1, oy, 0, 1, 36) ? "INPUT" : 0; break;
            case 'k': n = r <= LV(ox - 1, oy, 0, 1, 36) ? "VARIABLE NAME" : 0; break;
            case 't': n = r <= LV(ox - 1, oy, 0, 1, 36) ? "TRACK VALUE" : 0; break;
            case '^': n = r == 1 ? "CHANCE" : 0; break;
            case '~': n = r == 1 ? "REPEATS" : r == 2 ? "DIVISION" : r == 3 ? "MULTIPLY" : 0; break;
            case '+':
                if (r <= 12) return join(buf, max, "+", join(buf, max, notes12[r - 1], "ON/OFF"));
                break;
            case ':': case '%': case '(': n = r <= 5 ? midi5[r - 1] : 0; break;
            case '"': case '[': n = r <= 5 ? trig5[r - 1] : 0; break;
            case '/': n = r == 1 ? "OUTPUT" : r <= 4 ? midi5[r - 1] : r == 5 ? "FRAMES" : 0; break;
            case ')': n = r <= 6 ? scale6[r - 1] : 0; break;
            case ']': n = r == 1 ? "OUTPUT" : r <= 5 ? scale6[r - 1] : r == 6 ? "PULSES" : 0; break;
            case '@': n = r <= 8 ? shape8[r - 1] : 0; break;
            case '{': n = r == 1 ? "OUTPUT" : r <= 8 ? shape8[r - 1] : 0; break;
            case '!': n = r == 1 ? "CHANNEL" : r == 2 ? "CC NUMBER" : r == 3 ? "VALUE" : 0; break;
            case '\'': n = r == 1 ? "OUTPUT" : r == 2 ? "SLEW" : r == 3 ? "LEVEL" : 0; break;
            case '?': n = r == 1 ? "CHANNEL" : r == 2 ? "LSB" : r == 3 ? "MSB" : 0; break;
            case '\\': n = r == 1 ? "OUTPUT" : r == 2 ? "LSB" : r == 3 ? "MSB" : 0; break;
            case '-': n = r == 1 ? "OUTPUT" : 0; break;
            case '_': n = r == 1 ? "OUTPUT" : r == 2 ? "OCTAVE" : r == 3 ? "NOTE" : 0; break;
            case '}': n = r == 1 ? "OUTPUT" : r <= 5 ? shape8[r + 2] : 0; break;
            case '$': case ';': case '=': {
                if (g == '=' && r == 1) { n = "OSC PATH"; break; }
                for (int i = 1; i <= r; i++)
                    if (G(ox + i, oy) == '.' || !G(ox + i, oy)) return 0;
                n = g == '$' ? "COMMAND" : g == ';' ? "UDP (NO NET)" : "OSC (NO NET)";
                break;
            }
            case '>': case '<': case ',': return dev_port(g, ox, oy, r - 1, buf, max);
        }
    }
    else if (dx == 0 && dy == 1) {
        switch (lg) {
            case 'a': n = "SUM"; break;
            case 'b': n = "DIFFERENCE"; break;
            case 'm': n = "PRODUCT"; break;
            case 'c': n = "COUNT"; break;
            case 'l': n = "LESSER"; break;
            case 'r': n = "RANDOM"; break;
            case 'i': case 'z': case 'v': case 'o': case 't': n = "OUTPUT"; break;
            case 'h': n = "HALTED"; break;
            case 'j': n = "OUTPUT"; break;
            case 'd': case 'f': case 'u': case '^': case '&': case '~': case '`': n = "BANG OUT"; break;
            case '>': {
                // only the selectors that read a value write below
                char sel = tolower((unsigned char)G(ox + 1, oy));
                if (sel && strchr("lapijq", sel)) n = "EX READ VALUE";
                break;
            }
        }
    }
    else if (dx == 0 && dy == -1) {
        if (lg == '&') n = "STEP";
        if (lg == 'j') n = "INPUT";
    }
    return n ? join(buf, max, op, n) : 0;
}

const char *sf_cell_name(int16_t x, int16_t y, char *buf, uint8_t max) {
    // an inlet or outlet of an operator that runs (one that is itself not
    // an operand of something else), in the order operators run
    // the row itself first: a cell's inlet (what it is for) wins over the
    // output written into it from the row above
    static const int8_t rows[3] = { 0, -1, 1 };
    for (int ri = 0; ri < 3; ri++) {
        int oy = y + rows[ri];
        if (oy < 0 || oy >= SF_H) continue;
        for (int ox = 0; ox < SF_W; ox++) {
            if (ox == x && oy == y) continue;
            char g = G(ox, oy);
            if (g == '.' || !sf_is_op(g) || sf_locked(ox, oy)) continue;
            const char *n = port_of(g, ox, oy, x - ox, y - oy, buf, max);
            if (n) return n;
        }
    }
    // the operator itself
    char g = G(x, y);
    if (g != '.' && sf_is_op(g) && !sf_locked(x, y)) {
        if (g == '>' || g == '<' || g == ',') {
            char tmp[24];
            dev_port(g, x, y, 0, tmp, sizeof(tmp));
            return join(buf, max, g == '>' ? "DISTING" : g == ',' ? "JF" : "ER-301", tmp + 3);
        }
        return sf_op_name(g);
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////
// frames

uint32_t sf_frame_us(void) {
    return 15000000UL / sf.bpm;
}

// "$bpm:120" (or "$apm:30", 4 frames per beat, so 30 apm = 120 bpm)
// written anywhere in the grid sets the tempo once, without a bang.
// returns how many cells the directive covers, 0 if there is none. *apm is
// set when it was an apm directive
static int directive(int i, int *value, bool *apm) {
    int x = i % SF_W;
    if (x + 5 > SF_W) return 0;
    char k1 = tolower((unsigned char)sf.snap[i + 1]);
    if (k1 != 'b' && k1 != 'a') return 0;
    *apm = k1 == 'a';
    static const char want[] = "$?pm:";
    for (int k = 2; k < 5; k++)
        if (tolower((unsigned char)sf.snap[i + k]) != want[k]) return 0;
    int n = 0, v = 0;
    while (n < 3 && x + 5 + n < SF_W && isdigit((unsigned char)sf.snap[i + 5 + n])) {
        v = v * 10 + (sf.snap[i + 5 + n] - '0');
        n++;
    }
    if (!n) return 0;
    *value = v;
    return 5 + n;
}

static bool song_tick(void);

void sf_frame(uint32_t now) {
    sf.now = now;
    if (sf.song_playing && !song_tick()) return;

    memset(sf.lock, 0, sizeof(sf.lock));
    memset(sf.port, 0, sizeof(sf.port));
    memset(sf.var, 0, sizeof(sf.var));
    memcpy(sf.snap, sf.grid, SF_CELLS);

    for (int i = 0; i < SF_CELLS; i++) {
        char g = sf.snap[i];
        if (g == '.') continue;
        if (g == '$') {
            int v;
            bool apm;
            int n = directive(i, &v, &apm);
            if (n) {
                if (apm) v *= 4;
                if (v >= SF_BPM_MIN && v <= SF_BPM_MAX && v != sf.bpm_directive) {
                    sf.bpm_directive = v;
                    sf_set_bpm(v);
                }
                // Seaflake skips $bpm: cells as a directive. $apm: there is
                // an ordinary $ operator (it locks its cells), so it still
                // runs as one; only the tempo is applied here as well.
                if (!apm) {
                    i += n - 1;
                    continue;
                }
            }
        }
        if (!sf_is_op(g) || locked_i(i)) continue;
        int x = i % SF_W, y = i / SF_W;
        bool passive = !(g >= 'a' && g <= 'z');
        if (passive || has_bang(x, y)) run_op(g, x, y, false);
    }
    sf.f++;
    if (sf.loadbang == 1) sf.loadbang = 0;  // a loadbang bangs on one frame only
    notes_run();
    sf.dirty = 1;
}

void sf_ms(uint32_t now) {
    sf.now = now;
    sf.next_due = 0xFFFFFFFF;

    for (int i = 0; i < SF_OFFS; i++) {
        sf_off_t *o = &sf.offs[i];
        if (!o->active) continue;
        if ((int32_t)(now - o->due) >= 0) {
            off_send(o->ex, o->ch, o->note);
            o->active = 0;
        }
        else
            due(o->due);
    }

    for (int i = 0; i < SF_SEQS; i++) {
        sf_seq_t *s = &sf.seqs[i];
        if (!s->active) continue;
        while (s->idx < s->count && (int32_t)(now - seq_time(s, s->idx)) >= 0) {
            if (s->trig == SF_SEQ_TRIG_CV) {
                sf.cv_note[s->ch] = s->cv;
                cv_send(s->ch, 0);
                if (s->vel) pulse(s->ch, sf.pulse_ms);
            }
            else if (s->trig == SF_SEQ_TRIG)
                pulse(s->ch, sf.pulse_ms);  // } : the pulse only, no CV
            else if (s->trig == SF_SEQ_JF)
                sf_io_jf_note(s->note, s->vel);  // ,@ : Just Friends rings out by itself
            else {
                // @ (MIDI) and >@ (Disting EX voice): the note, off half a frame later
                const bool ex = s->trig == SF_SEQ_EX;
                uint32_t half = sf_frame_us() / 2000;
                if (ex)
                    sf_io_ex_note(s->note, s->vel, true);
                else
                    sf_io_midi_on(s->ch, s->note, midi_vel(s->vel));
                off_push(ex, s->ch, s->note, now + (half < 5 ? 5 : half));
            }
            s->idx++;
        }
        if (s->idx >= s->count)
            s->active = 0;
        else
            due(seq_time(s, s->idx));
    }

    for (int i = 0; i < SF_BURSTS; i++) {
        sf_burst_t *b = &sf.bursts[i];
        if (!b->active) continue;
#define BT(b) ((b)->start + (uint32_t)(((uint64_t)(b)->idx * (b)->interval_us) / 1000))
        while (b->active && b->idx < b->count && (int32_t)(now - BT(b)) >= 0) {
            if (G(b->x, b->y) != '~') {
                b->active = 0;
                break;
            }
            uint16_t seen[24];
            int nseen = 1;
            seen[0] = b->y * SF_W + b->x;
            W(b->x, b->y + 1, '*');
            trigger_neighbors(b->x, b->y + 1, seen, &nseen);
            W(b->x, b->y + 1, '.');
            b->idx++;
        }
        if (b->idx >= b->count) b->active = 0;
        if (b->active) due(BT(b));
#undef BT
    }
}

////////////////////////////////////////////////////////////////////////////
// transport

void sf_set_bpm(int16_t bpm) {
    sf.bpm = clampi(bpm, SF_BPM_MIN, SF_BPM_MAX);
    sf.dirty = 1;
}

void sf_silence(void) {
    for (int i = 0; i < SF_NOTES; i++)
        if (sf.notes[i].active) note_stop(&sf.notes[i]);
    for (int i = 0; i < 16; i++)
        if (sf.mono[i].active) note_stop(&sf.mono[i]);
    for (int i = 0; i < SF_OFFS; i++)
        if (sf.offs[i].active) {
            off_send(sf.offs[i].ex, sf.offs[i].ch, sf.offs[i].note);
            sf.offs[i].active = 0;
        }
    for (int o = 0; o < 36; o++)
        if (sf.gate[o]) {
            sf.gate[o] = 0;
            sf_io_gate(o, false);
        }
    cancel_timed();
}

void sf_play(bool on) {
    sf.playing = on;
    if (!on) {
        sf.song_playing = 0;
        sf_silence();
    }
    sf.dirty = 1;
}

void sf_clear(void) {
    memset(sf.grid, '.', SF_CELLS);
    memset(sf.snap, '.', SF_CELLS);
    memset(sf.lock, 0, sizeof(sf.lock));
    memset(sf.port, 0, sizeof(sf.port));
    cancel_timed();
    sf.f = 0;
    sf.bpm_directive = 0;
    sf.modified = 0;
    sf.dirty = 1;
}

void sf_init(void) {
    memset(&sf, 0, sizeof(sf));
    sf.bpm = 120;
    sf.pulse_ms = sf_config_pulse_ms();
    sf.rng = 0x5EAF1A4E;
    sf.next_due = 0xFFFFFFFF;
    sf_clear();
    sf_song_clear();
}

char sf_get(int16_t x, int16_t y) {
    char g = G(x, y);
    return g ? g : '.';
}

void sf_set(int16_t x, int16_t y, char g) {
    if (W(x, y, g)) sf.modified = 1;
}

bool sf_locked(int16_t x, int16_t y) {
    return inb(x, y) && locked_i(y * SF_W + x);
}

bool sf_picked(int16_t x, int16_t y) {
    if (!inb(x, y)) return false;
    const int i = y * SF_W + x;
    return sf.port[i >> 3] & (1 << (i & 7));
}

////////////////////////////////////////////////////////////////////////////
// $ commands

static bool word_is(const char *msg, uint8_t n, const char *w) {
    for (uint8_t i = 0; i < n; i++)
        if (tolower((unsigned char)msg[i]) != w[i]) return false;
    return w[n] == 0;
}

bool sf_command(const char *msg, uint8_t len) {
    uint8_t n = 0;
    while (n < len && isalpha((unsigned char)msg[n])) n++;
    uint8_t p = n;
    if (p < len && msg[p] == ':') p++;
    bool neg = false, has = false;
    int32_t v = 0;
    if (p < len && msg[p] == '-') {
        neg = true;
        p++;
    }
    for (; p < len; p++) {
        if (!isdigit((unsigned char)msg[p])) return false;
        if (v < 100000) v = v * 10 + (msg[p] - '0');
        has = true;
    }
    if (neg) v = -v;

    if (word_is(msg, n, "play") || word_is(msg, n, "run"))
        sf_play(true);
    else if (word_is(msg, n, "stop"))
        sf_play(false);
    else if (!has)
        return false;
    else if (word_is(msg, n, "bpm")) {
        if (v < SF_BPM_MIN || v > SF_BPM_MAX) return false;
        sf_set_bpm(v);
    }
    else if (word_is(msg, n, "apm")) {
        if (v < 1 || v > 300) return false;
        sf_set_bpm(v * 4);
    }
    else if (word_is(msg, n, "frame"))
        sf.f = v < 0 ? 0 : v;
    else if (word_is(msg, n, "skip"))
        sf.f += v;
    else if (word_is(msg, n, "rewind"))
        sf.f = (int32_t)sf.f - v < 0 ? 0 : sf.f - v;
    else if (word_is(msg, n, "pulse")) {
        if (v < 1 || v > 250) return false;
        sf.pulse_ms = v;
    }
    else if (word_is(msg, n, "script") || word_is(msg, n, "s")) {
        if (v < 1 || v > 10) return false;
        sf_io_script(v);
    }
    else if (word_is(msg, n, "patch") || word_is(msg, n, "p")) {
        if (v < 1 || v > SF_PATCH_SLOTS) return false;
        return sf_io_patch_load(v);
    }
    else
        return false;
    return true;
}

////////////////////////////////////////////////////////////////////////////
// patches: the grid run-length packed. A byte below 0x80 is a glyph, a byte
// b >= 0x80 stands for (b - 0x7F) empty cells. Trailing empty cells are
// left out. It starts with the tempo: 0x01, then the BPM in two 7-bit bytes
// (patches saved before there was a tempo have no such start).

int16_t sf_patch_bpm(const uint8_t *src, uint16_t len, uint16_t *start) {
    if (len >= 3 && src[0] == 0x01) {
        *start = 3;
        return (int16_t)((src[1] << 7) | src[2]);
    }
    *start = 0;
    return 0;
}

uint16_t sf_pack(uint8_t *dst, uint16_t max) {
    int last = SF_CELLS - 1;
    while (last >= 0 && sf.grid[last] == '.') last--;
    uint16_t n = 0;
    if (max < 4) return 0;
    dst[n++] = 0x01;
    dst[n++] = (uint8_t)((sf.bpm >> 7) & 127);
    dst[n++] = (uint8_t)(sf.bpm & 127);
    int run = 0;
    for (int i = 0; i <= last; i++) {
        if (sf.grid[i] == '.') {
            run++;
            continue;
        }
        while (run > 0) {
            int r = run > 128 ? 128 : run;
            if (n >= max) return 0;
            dst[n++] = 0x7F + r;
            run -= r;
        }
        if (n >= max) return 0;
        dst[n++] = sf.grid[i];
    }
    if (n == 3) dst[n++] = 0x80;  // an empty patch is still a patch
    return n;
}

void sf_unpack_to(char *grid, const uint8_t *src, uint16_t len) {
    memset(grid, '.', SF_CELLS);
    int i = 0;
    uint16_t k;
    sf_patch_bpm(src, len, &k);
    for (; k < len && i < SF_CELLS; k++) {
        if (src[k] >= 0x80)
            i += src[k] - 0x7F;
        else
            grid[i++] = (src[k] > 32 && src[k] < 127) ? src[k] : '.';
    }
}

void sf_unpack(const uint8_t *src, uint16_t len) {
    uint16_t start;
    const int16_t bpm = sf_patch_bpm(src, len, &start);
    sf_clear();
    sf_unpack_to(sf.grid, src, len);
    if (bpm) sf_set_bpm(bpm);   // the patch's own tempo
    // ` loadbangs fire on the next frame (at power-on: once the boot scan is done)
    if (sf.loadbang != 2) sf.loadbang = 1;
}

////////////////////////////////////////////////////////////////////////////
// song mode

void sf_song_clear(void) {
    memset(&sf.song, 0, sizeof(sf.song));
    sf.song.magic = SF_SONG_MAGIC;
    sf.song.loop = 1;
    sf.song_playing = 0;
    sf.song_modified = 0;
}

uint32_t sf_song_slot_frames(uint8_t i) {
    uint32_t n = (uint32_t)sf.song.slots[i].len * sf.song.slots[i].mul;
    return n < 1 ? 1 : n;
}

static void song_order(void) {
    sf.order_len = 0;
    for (int i = 0; i < SF_SONG_LEN; i++)
        if (sf.song.slots[i].patch) sf.order[sf.order_len++] = i;
    if (sf.song.mode == SF_REV) {
        for (int a = 0, b = sf.order_len - 1; a < b; a++, b--) {
            uint8_t t = sf.order[a];
            sf.order[a] = sf.order[b];
            sf.order[b] = t;
        }
    }
    if (sf.song.mode == SF_RND) {
        for (int i = sf.order_len - 1; i > 0; i--) {
            int j = rnd() % (i + 1);
            uint8_t t = sf.order[i];
            sf.order[i] = sf.order[j];
            sf.order[j] = t;
        }
    }
}

bool sf_song_load_slot(uint8_t i) {
    if (i >= SF_SONG_LEN || !sf.song.slots[i].patch) return false;
    sf_io_patch_load(sf.song.slots[i].patch);
    sf_set_bpm(sf.song.slots[i].bpm);
    return true;
}

void sf_song_start(void) {
    song_order();
    sf.order_idx = 0;
    sf.song_pos = 0;
    if (!sf.order_len) return;
    sf.song_playing = 1;
    sf_song_load_slot(sf.order[0]);
    sf.playing = 1;
    sf.dirty = 1;
}

void sf_song_stop(void) {
    sf_play(false);
}

// advance the song by one frame. false when the song has just ended
static bool song_tick(void) {
    uint8_t i = sf.order[sf.order_idx];
    if (!sf.song.slots[i].patch) {
        sf.song_playing = 0;
        return true;
    }
    if (sf.song_pos >= sf_song_slot_frames(i)) {
        sf.order_idx++;
        if (sf.order_idx >= sf.order_len) {
            if (!sf.song.loop) {
                sf_play(false);
                return false;
            }
            song_order();
            sf.order_idx = 0;
            if (!sf.order_len) {
                sf_play(false);
                return false;
            }
        }
        sf.song_pos = 0;
        sf_song_load_slot(sf.order[sf.order_idx]);
    }
    sf.song_pos++;
    return true;
}
