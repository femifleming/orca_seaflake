
#line 1 "scanner.rl"
#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"
#include "match_token.h"
#include "teletype.h"

const size_t kMaxTokenLength = 32;


#line 17 "scanner.c"
static const int scanner_start = 0;
static const int scanner_first_final = 0;
static const int scanner_error = -1;

static const int scanner_en_main = 0;


#line 16 "scanner.rl"


error_t scanner(const char *data, tele_command_t *out,
                char error_msg[TELE_ERROR_MSG_LENGTH]) {
    const size_t len = strlen(data);

    // required ragel declarations
    int cs;                       // machine state
    int act;                      // used with '=>'
    const char* ts;               // token start
    const char* te;               // token end

    const char* p = data;         // pointer to data
    const char* pe = data + len;  // pointer to end of data
    const char* eof = pe;         // pointer to eof
    (void)scanner_en_main;        // fix unused variable warning
    (void)act;                    // fix unused variable warning

    // reset outputs
    error_msg[0] = 0;
    out->length = 0;
    out->separator = -1;

    
#line 50 "scanner.c"
	{
	cs = scanner_start;
	ts = 0;
	te = 0;
	act = 0;
	}

#line 58 "scanner.c"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
tr1:
#line 115 "scanner.rl"
	{te = p+1;}
	goto st0;
tr4:
#line 50 "scanner.rl"
	{te = p;p--;{
            // token matched

            // copy the matched token to buf
            char buf[kMaxTokenLength];
            size_t len = te-ts;
            if (len > kMaxTokenLength) len = kMaxTokenLength;
            memcpy(buf, ts, len);
            buf[len] = '\0';

            tele_data_t tele_data;
            if (match_token(buf, len, &tele_data)) {
                // if we have a match, copy data to the the command
                out->data[out->length] = tele_data;

                // increase the command length
                out->length++;

                // if the command length is now too long, abort
                if (out->length >= COMMAND_MAX_LENGTH) return E_LENGTH;
            }
            else {
                // can't match the token, fail
                strcpy(error_msg, buf);
                return E_PARSE;
            }
        }}
	goto st0;
tr5:
#line 106 "scanner.rl"
	{te = p;p--;{
            return E_NEED_SPACE_PRE_SEP;
        }}
	goto st0;
tr6:
#line 78 "scanner.rl"
	{te = p+1;{
            // ';' pre separator matched

            // it's a PRE_SEP, we need to record it's position
            // (validate checks for too many PRE_SEP tokens)
            out->data[out->length].tag = PRE_SEP;
            out->data[out->length].value = 0;
            out->separator = out->length;

            // increase the command length
            out->length++;

            // if the command length is now too long, abort
            if (out->length >= COMMAND_MAX_LENGTH) return E_LENGTH;
        }}
	goto st0;
tr7:
#line 110 "scanner.rl"
	{te = p;p--;{
            return E_NEED_SPACE_SUB_SEP;
        }}
	goto st0;
tr8:
#line 94 "scanner.rl"
	{te = p+1;{
            // ':' mod separator matched
            out->data[out->length].tag = SUB_SEP;
            out->data[out->length].value = 0;

            // increase the command length
            out->length++;

            // if the command length is now too long, abort
            if (out->length >= COMMAND_MAX_LENGTH) return E_LENGTH;
        }}
	goto st0;
st0:
#line 1 "NONE"
	{ts = 0;}
	if ( ++p == pe )
		goto _test_eof0;
	/* fall through */
case 0:
#line 1 "NONE"
	{ts = p;}
#line 151 "scanner.c"
	switch( (*p) ) {
		case 32: goto tr1;
		case 58: goto st2;
		case 59: goto st3;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto tr1;
	goto st1;
st1:
	if ( ++p == pe )
		goto _test_eof1;
	/* fall through */
case 1:
	if ( (*p) == 32 )
		goto tr4;
	if ( (*p) > 10 ) {
		if ( 58 <= (*p) && (*p) <= 59 )
			goto tr4;
	} else if ( (*p) >= 9 )
		goto tr4;
	goto st1;
st2:
	if ( ++p == pe )
		goto _test_eof2;
	/* fall through */
case 2:
	if ( (*p) == 32 )
		goto tr6;
	goto tr5;
st3:
	if ( ++p == pe )
		goto _test_eof3;
	/* fall through */
case 3:
	if ( (*p) == 32 )
		goto tr8;
	goto tr7;
	}
	_test_eof0: cs = 0; goto _test_eof; 
	_test_eof1: cs = 1; goto _test_eof; 
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 

	_test_eof: {}
	if ( p == eof )
	{
	switch ( cs ) {
	case 1: goto tr4;
	case 2: goto tr5;
	case 3: goto tr7;
	}
	}

	}

#line 125 "scanner.rl"


    if (cs == scanner_error || cs < scanner_first_final) {
        strcpy(error_msg, "SCAN!");
        return E_PARSE;
    }

    return E_OK;
}
