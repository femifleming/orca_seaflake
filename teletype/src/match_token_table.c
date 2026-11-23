// Hand-written replacement for the Ragel-generated match_token.c.
//
// The Ragel state machine for ~1000 op names compiles to about 64 KB of
// flash. Every op and mod already carries its name in tele_ops[] and
// tele_mods[], so matching a token against those tables gives the same
// result in a few hundred bytes. That is what makes room for Seaflake.
//
// Behaviour is kept identical to match_token.rl (which stays in the tree as
// the reference): tests/match_token_tests.c compares the two on every op,
// every mod and a large set of number-like strings.

#include "match_token.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "ops/op.h"
#include "ops/op_enum.h"

// number = (('-')? [0-9]+) | ([X] [0-9A-F]+) | ([B|R] [0-1]+);
// Note that the Ragel class [B|R] also contains a literal '|'; kept as is.
static bool is_number(const char *t, size_t len) {
    if (len == 0) return false;
    size_t i = 0;
    if (t[0] == 'X') {
        if (len < 2) return false;
        for (i = 1; i < len; i++)
            if (!(isdigit((unsigned char)t[i]) || (t[i] >= 'A' && t[i] <= 'F')))
                return false;
        return true;
    }
    if (t[0] == 'B' || t[0] == 'R' || t[0] == '|') {
        if (len < 2) return false;
        for (i = 1; i < len; i++)
            if (t[i] != '0' && t[i] != '1') return false;
        return true;
    }
    if (t[0] == '-') i = 1;
    if (i >= len) return false;
    for (; i < len; i++)
        if (!isdigit((unsigned char)t[i])) return false;
    return true;
}

static bool name_is(const char *name, const char *token, size_t len) {
    return strncmp(name, token, len) == 0 && name[len] == 0;
}

bool match_token(const char *token, const size_t len, tele_data_t *out) {
    if (is_number(token, len)) {
        out->tag = NUMBER;
        uint8_t base = 0;
        uint8_t binhex = 0;
        uint8_t bitrev = 0;
        if (token[0] == 'X') {
            out->tag = XNUMBER;
            binhex = 1;
            base = 16;
            token++;
        }
        else if (token[0] == 'B') {
            out->tag = BNUMBER;
            binhex = 1;
            base = 2;
            token++;
        }
        else if (token[0] == 'R') {
            out->tag = RNUMBER;
            binhex = 1;
            bitrev = 1;
            base = 2;
            token++;
        }
        int32_t val = strtol(token, NULL, base);
        if (binhex) val = (int16_t)((uint16_t)val);
        if (bitrev) val = rev_bitstring_to_int(token);
        val = val > INT16_MAX ? INT16_MAX : val;
        val = val < INT16_MIN ? INT16_MIN : val;
        out->value = val;
        return true;
    }

    for (size_t i = 0; i < E_OP__LENGTH; i++) {
        if (name_is(tele_ops[i]->name, token, len)) {
            out->tag = OP;
            out->value = i;
            return true;
        }
    }

    for (size_t i = 0; i < E_MOD__LENGTH; i++) {
        if (name_is(tele_mods[i]->name, token, len)) {
            out->tag = MOD;
            out->value = i;
            return true;
        }
    }

    return false;
}
