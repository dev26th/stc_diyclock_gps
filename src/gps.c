#include "gps.h"

#include <stc12.h>
#include "ds1302.h"
#include "uart.h"

#define STATE_NAME    1
#define STATE_TIME    2
#define STATE_BETWEEN 3
#define STATE_DATE    4
#define STATE_START   5

struct gps_DateTime gps_datetime;
static uint8_t state = STATE_START;
static uint8_t pos;

//$GPRMC,182600.00,V,,,,,,,210916,,,N*7D

uint8_t gps_minutes;
uint8_t gps_tenminutes;
uint8_t gps_hour;
uint8_t gps_tenhour;

void gps_init() {

}

void gps_cycle() {
	uint8_t b;
	if(REND != 1) return;
	
	REND = 0;
	b = RBUF;
	
	if(b == '$') {
		state = STATE_NAME;
		pos = 0;
	}
	else {
		switch(state) {
			case STATE_START: // wait
				break;
				
			case STATE_BETWEEN: // just count commas
				if(b == ',') {
					++pos;
					if(pos == 8) {
						state = STATE_DATE;
						pos = 0;
					}
				}
				break;
				
			default: // name, time or date - fill the buffer
				if(pos < 6) {
					uint8_t v = (b - '0');
					switch(state) {
						case STATE_NAME:
							if(pos == 2 && b != 'R') { // not GPRMC
								state = STATE_START;
								return;
							}
							break;
							
						case STATE_TIME:
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
							switch(pos) {
								case 0: gps_datetime.tenday    = v; break;
								case 1: gps_datetime.day       = v; break;
								case 2: gps_datetime.tenmonth  = v; break;
								case 3: gps_datetime.month     = v; break;
								case 4: gps_datetime.tenyear   = v; break;
								case 5: 
									gps_datetime.year = v;	
									gps_datetime.wait = CFG_GPS_CORRECTION; 
									gps_datetime.valid = 1; 
									break;
							}
							break;
					}
					if(++pos == 6) {
						++state; // next state
						pos = 0;
					}
				}
				else {
					state = STATE_START; // something is wrong
				}
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
