#include "gps.h"

#include <stc12.h>
#include "ds1302.h"
#include "uart.h"

#define STATE_ERROR    0
#define STATE_NAME     1
#define STATE_TIME     2
#define STATE_BETWEEN  3
#define STATE_DATE     4
#define STATE_TAIL     5
#define STATE_CHECKSUM 6

struct gps_DateTime gps_datetime;
static uint8_t state = STATE_ERROR;
static uint8_t pos;
static uint8_t sum1;
static uint8_t sum2;

static const char* ALLOWED_CHARS = "$*.,0123456789ABCDEFGMNPRSVW";

//$GPRMC,182600.00,V,,,,,,,210916,,,N*7D

void gps_init() {

}

void gps_cycle() {
    uint8_t b;

    if(REND != 1) return;

    REND = 0;
    b = RBUF;

    {
        const char* s;
        __bit ok = 0;
        for(s = ALLOWED_CHARS; *s; ++s) {
            if(b == (uint8_t)(*s)) {
                ok = 1;
                break;
            }
        }
        if(!ok) {
            state = STATE_ERROR;
            return;
        }
    }

    if(b == '$') {
        state = STATE_NAME;
        pos = 0;
        sum1 = 0;
    }
    else {
        switch(state) {
            case STATE_ERROR: // wait
                break;

            case STATE_NAME:
            case STATE_TIME:
            case STATE_DATE: // fill the buffer
                if(pos < 6) {
                    uint8_t v = (b - '0');
                    sum1 ^= b;
                    switch(state) {
                        case STATE_NAME:
                            if(pos == 2 && b != 'R') { // not GPRMC
                                state = STATE_ERROR;
                                return;
                            }
                            break;

                        case STATE_TIME:
                            if(v > 9) state = STATE_ERROR;
                            gps_datetime.valid = 0;
                            switch(pos) {
                                case 0: gps_datetime.tenhour    = v; break;
                                case 1: gps_datetime.hour       = v; break;
                                case 2: gps_datetime.tenminutes = v; break;
                                case 3: gps_datetime.minutes    = v; break;
                                case 4: gps_datetime.tenseconds = v; break;
                                case 5: gps_datetime.seconds    = v; break;
                            }
                            break;

                        case STATE_DATE:
                            if(v > 9) state = STATE_ERROR;
                            switch(pos) {
                                case 0: gps_datetime.tenday   = v; break;
                                case 1: gps_datetime.day      = v; break;
                                case 2: gps_datetime.tenmonth = v; break;
                                case 3: gps_datetime.month    = v; break;
                                case 4: gps_datetime.tenyear  = v; break;
                                case 5: gps_datetime.year     = v; break;
                            }
                            break;
                    }
                    if(++pos == 6) {
                        ++state; // next state
                        pos = 0;
                    }
                }
                else {
                    state = STATE_ERROR; // something is wrong
                }
                break;

            case STATE_BETWEEN: // just count commas
                sum1 ^= b;
                if(b == ',') {
                    ++pos;
                    if(pos == 8) {
                        state = STATE_DATE;
                        pos = 0;
                    }
                }
                break;

            case STATE_TAIL: // wait for '*'
                if(b == '*') {
                    state = STATE_CHECKSUM;
                    pos = 0;
                    sum2 = 0;
                }
                else {
                    sum1 ^= b;
                }
                break;

            case STATE_CHECKSUM:
                {
                    uint8_t v = (b > '9') ? (b - 'A' + 10) : (b - '0');

                    if(pos == 0) {
                        sum2 = (v << 4);
                        ++pos;
                    }
                    else {
                        sum2 |= v;
                        if(sum1 == sum2) {
                            gps_datetime.wait = CFG_GPS_CORRECTION;
                            gps_datetime.valid = 1;
                        }
                        state = STATE_ERROR; // EoS
                    }
                }
                break;
        }
    }
}

void gps_cycle10ms() {
    if(gps_datetime.valid) {
        if(gps_datetime.wait) {
            --gps_datetime.wait;
        }
    }
}
