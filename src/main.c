//
// STC15F204EA DIY LED Clock
// Copyright 2016, Jens Jensen
//

#include <stc12.h>
#include <stdint.h>

#include "config.h"
#include "adc.h"
#include "ds1302.h"
#include "led.h"
    
#define FOSC    11059200

// clear wdt
#define WDT_CLEAR()    (WDT_CONTR |= 1 << 4)

// alias for relay and buzzer outputs
#define RELAY   P1_4
#define BUZZER  P1_5
    
// adc channels for sensors
#define ADC_LIGHT 6
#define ADC_TEMP  7

// three steps of dimming. Photoresistor adc value is 0-255. Lower values = brighter.
#define DIM_HI  100
#define DIM_LO  190

// button switch aliases
#define SW2     P3_0
#define S2      1
#define SW1     P3_1
#define S1      0

// button press states
#define PRESS_NONE   0
#define PRESS_SHORT  1
#define PRESS_LONG   2

// should the DP be shown, negative logic
#define DP_OFF 0x0
#define DP_ON 0x80

// display mode states, order is important
enum display_mode {
	#if CFG_SET_DATE_TIME == 1
	M_SET_MONTH,
	M_SET_DAY,
	M_SET_HOUR,
	M_SET_MINUTE,
	#endif

	#if CFG_ALARM == 1
	M_ALARM_HOUR,
	M_ALARM_MINUTE,
	M_ALARM_ON,
	#endif

	#if CFG_CHIME == 1
	M_CHIME_START,
	M_CHIME_STOP,
	M_CHIME_ON,
	#endif

	M_NORMAL,
	M_TEMP_DISP,
	M_DATE_DISP,
	M_WEEKDAY_DISP,
	M_SECONDS_DISP
};

/* ------------------------------------------------------------------------- */

volatile uint8_t timerTicksNow;
// delay may be only tens of ms
void _delay_ms(uint8_t ms)
{
	uint8_t stop = timerTicksNow + ms / 10;
	while(timerTicksNow != stop);
}

// GLOBALS
uint8_t i;
uint16_t count;
int16_t temp;      // temperature sensor value
uint8_t lightval;   // light sensor value
__bit  beep;

#if CFG_ALARM == 1
enum TriState {
	TRI_NONE,
	TRI_ON,
	TRI_OFF
};
enum TriState alarmState;
#endif // CFG_ALARM == 1

#if CFG_CHIME == 1
__bit chimeState;
#endif // CFG_CHIME == 1

struct ds1302_rtc rtc;
struct ram_config config;
__bit  configModified;

// to work with current time, only actualy used fields are defined
struct DateTime {
	uint8_t hour;
	uint8_t minutes;
	uint8_t seconds;
};
struct DateTime now;

void convertNow() {
	now.hour = rtc.tenhour * 10 + rtc.hour;
	now.minutes = rtc.tenminutes * 10 + rtc.minutes;
	now.seconds = rtc.tenseconds * 10 + rtc.seconds;
}

volatile uint8_t displaycounter;
uint8_t dbuf[4];             // led display buffer, next state
uint8_t dbufCur[4];          // led display buffer, current state
uint8_t dmode = M_NORMAL;    // display mode state
__bit display_colon;         // flash colon
uint8_t showDp;

__bit  flash_d1d2;
__bit  flash_d3d4;

volatile uint8_t debounce[2];      // switch debounce buffer
volatile uint8_t switchcount[2];

void timer0_isr() __interrupt 1 __using 1
{
	// display refresh ISR
	// cycle thru digits one at a time
	uint8_t digit = displaycounter % 4; 

	// turn off all digits, set high    
	P3 |= 0x3C;

	// auto dimming, skip lighting for some cycles
	if (displaycounter % lightval < 4 ) {
		// fill digits
		P2 = dbufCur[digit];
		// turn on selected digit, set low
		P3 &= ~((0x1 << digit) << 2);  
	}
	displaycounter++;
}

void timer1_isr() __interrupt 3 __using 1 {
	// debounce ISR

	uint8_t s0 = switchcount[0];
	uint8_t s1 = switchcount[1];
	uint8_t d0 = debounce[0];
	uint8_t d1 = debounce[1];

	// debouncing stuff
	// keep resetting halfway if held long
	if (s0 > 250)
		s0 = 100;
	if (s1 > 250)
		s1 = 100;

	// increment count if settled closed
	if ((d0 & 0x0F) == 0x00)    
		s0++;
	else
		s0 = 0;

	if ((d1 & 0x0F) == 0x00)
		s1++;
	else
		s1 = 0;

	switchcount[0] = s0;
	switchcount[1] = s1;

	// read switch positions into sliding 8-bit window
	debounce[0] = (d0 << 1) | SW1;
	debounce[1] = (d1 << 1) | SW2;  

	++timerTicksNow;

	#if CFG_ALARM == 1 || CFG_CHIME == 1
	if(beep) BUZZER = !BUZZER;
	#endif // CFG_ALARM == 1 || CFG_CHIME == 1
}

void Timer0Init(void) // 100us @ 11.0592MHz
{
	TL0 = 0xA3;     // Initial timer value
	TH0 = 0xFF;     // Initial timer value
	TF0 = 0;        // Clear TF0 flag
	TR0 = 1;        // Timer0 start run
	ET0 = 1;        // enable timer0 interrupt
	EA = 1;         // global interrupt enable
}

void Timer1Init(void) // 10ms @ 11.0592MHz
{
	TL1 = 0xD5;     // Initial timer value
	TH1 = 0xDB;     // Initial timer value
	TF1 = 0;        // Clear TF1 flag
	TR1 = 1;        // Timer1 start run
	ET1 = 1;        // enable Timer1 interrupt
	EA = 1;         // global interrupt enable
}

uint8_t getkeypress(uint8_t keynum)
{
	if (switchcount[keynum] > 150) {
		_delay_ms(30);
		return PRESS_LONG;  // ~1.5 sec
	}
	if (switchcount[keynum]) {
		_delay_ms(60);
		return PRESS_SHORT; // ~100 msec
	}
	return PRESS_NONE;
}

void resetkeypress(uint8_t keynum)
{
	switchcount[keynum] = 0;
}

int8_t gettemp(uint16_t raw) {
#if CFG_TEMP_UNIT == 'F'
	// formula for ntc adc value to approx F
	// note: 354 ~= 637*5/9; 169 ~= 9*76/5+32
	return 169 - raw * 64 / 354;
#else
	// formula for ntc adc value to approx C
	return 76 - raw * 64 / 637;
#endif
}

// store display bytes
// logic is inverted due to bjt pnp drive, i.e. low = on, high = off
#define displayChar(pos, val)  dbuf[pos] = ledtable[val]

#define displayDp(pos) dbuf[pos] &= ~DP_ON

// rotate third digit, by swapping bits fed with cba
#define rotateThirdChar() dbuf[2] = dbuf[2] & 0b11000000 | (dbuf[2] & 0b00111000) >> 3 | (dbuf[2] & 0b00000111) << 3;

void display(uint8_t d1Always, uint8_t d1, uint8_t d2, uint8_t d3Always, uint8_t d3, uint8_t d4)
{
	if(flash_d1d2) {
		displayChar(0, LED_BLANK);
		displayChar(1, LED_BLANK);
	}
	else {
		displayChar(0, (d1Always || d1) ? d1 : LED_BLANK);
		displayChar(1, d2);
	}

	if(flash_d3d4) {
		displayChar(2, LED_BLANK);
		displayChar(3, LED_BLANK);
	}
	else {
		displayChar(2, (d3Always || d3) ? d3 : LED_BLANK);
		displayChar(3, d4);
	}

	if(display_colon) {
		displayDp(1);
		displayDp(2);
	}
}

/*********************************************/
int main()
{
	// SETUP
	// set ds1302, photoresistor, & ntc pins to open-drain output, already have strong pullups
	P1M1 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);
	P1M0 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);

	// init rtc
	ds_init();
	// init/read ram config
	ds_ram_config_init((uint8_t *) &config);    

	Timer0Init(); // display refresh
	Timer1Init(); // switch debounce

	// LOOP
	while(1)
	{
		// run every ~1 secs
		if (count % 4 == 0) {
			lightval = getADCResult(ADC_LIGHT) >> 5;
			temp = gettemp(getADCResult(ADC_TEMP)) + config.temp_offset;

			// constrain dimming range
			if (lightval < 4) 
				lightval = 4;
		}

		ds_readburst((uint8_t *) &rtc); // read rtc
		convertNow();

		beep = 0;

		#if CFG_ALARM == 1
		// check alarm
		if(config.alarm_on) {
			switch(alarmState) {
				case TRI_NONE:
					if(config.alarm_hour == now.hour && config.alarm_minute == now.minutes) {
						alarmState = TRI_ON;
					}
					break;

				case TRI_ON:
					if(getkeypress(S1) || getkeypress(S2)) {
						alarmState = TRI_OFF;
						resetkeypress(S1); // FIXME doesn't work
						resetkeypress(S2);
					}
					// continue, i.e. alarm will go off automatically after one minute

				case TRI_OFF:
					if(config.alarm_hour != now.hour || config.alarm_minute != now.minutes) {
						alarmState = TRI_NONE; // forget about last alarm
					}
					break;
			}
			if(alarmState == TRI_ON) beep = 1;
		}
		#endif // CFG_ALARM == 1

		#if CFG_CHIME == 1
		// check chime
		if(config.chime_on) {
			if(!chimeState) {
				if(now.minutes == 0 && now.seconds == 0) {
					if((config.chime_hour_start <= config.chime_hour_stop && config.chime_hour_start <= now.hour && now.hour <= config.chime_hour_stop)
						|| (config.chime_hour_start > config.chime_hour_stop && (config.chime_hour_start <= now.hour || now.hour <= config.chime_hour_stop)))
					{
						chimeState = 1;
					}
				}
			}
			else {
				if(now.minutes != 0 || now.seconds != 0) {
					chimeState = 0;
				}
			}
			if(chimeState) beep = 1;
		}
		#endif // CFG_CHIME == 1

		// display decision tree
		display_colon = 0;
		switch (dmode) {
			#if CFG_SET_DATE_TIME == 1 
			case M_SET_HOUR:
				display_colon = 1;
				flash_d1d2 = !flash_d1d2;
				if (getkeypress(S2)) {
					ds_hours_incr(now.hour);
				}
				if (getkeypress(S1)) {
					flash_d1d2 = 0;
					dmode = M_SET_MINUTE;
				}
				break;

			 case M_SET_MINUTE:
				display_colon = 1;
				flash_d3d4 = !flash_d3d4;
				if (getkeypress(S2)) {
					ds_minutes_incr(now.minutes);
				}
				if (getkeypress(S1)) {
					flash_d3d4 = 0;
					++dmode; // M_ALARM_HOUR, M_CHIME_START or M_NORMAL
				}
				break;

			case M_SET_MONTH:
				flash_d1d2 = !flash_d1d2;
				if (getkeypress(S2)) {
					ds_month_incr(&rtc);
				}
				if (getkeypress(S1)) {
					flash_d1d2 = 0;
					dmode = M_SET_DAY;
				}
				break;

			case M_SET_DAY:
				flash_d3d4 = !flash_d3d4;
				if (getkeypress(S2)) {
					ds_day_incr(&rtc);
				}
				if (getkeypress(S1)) {
					flash_d3d4 = 0;
					dmode = M_DATE_DISP;
				}
				break;
			#endif // CFG_SET_DATE_TIME == 1 

			#if CFG_ALARM == 1
			case M_ALARM_HOUR:
				display_colon = 1;
				flash_d1d2 = !flash_d1d2;
				if(getkeypress(S2)) {
					++config.alarm_hour;
					if(config.alarm_hour >= 24) config.alarm_hour = 0;
					config.alarm_on = 1;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d1d2 = 0;
					dmode = M_ALARM_MINUTE;
				}
				break;

			case M_ALARM_MINUTE:
				display_colon = 1;
				flash_d3d4 = !flash_d3d4;
				if(getkeypress(S2)) {
					++config.alarm_minute;
					if(config.alarm_minute >= 60) config.alarm_minute = 0;
					config.alarm_on = 1;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d3d4 = 0;
					dmode = M_ALARM_ON;
				}
				break;

			case M_ALARM_ON:
				display_colon = 1;
				flash_d1d2 = !flash_d1d2;
				flash_d3d4 = !flash_d3d4;
				if(getkeypress(S2)) {
					config.alarm_on = !config.alarm_on;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d1d2 = 0;
					flash_d3d4 = 0;
					++dmode; // M_CHIME_START or M_NORMAL
				}
				break;
			#endif // CFG_ALARM == 1

			#if CFG_CHIME == 1
			case M_CHIME_START:
				flash_d1d2 = !flash_d1d2;
				if(getkeypress(S2)) {
					++config.chime_hour_start;
					if(config.chime_hour_start >= 24) config.chime_hour_start = 0;
					config.chime_on = 1;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d1d2 = 0;
					dmode = M_CHIME_STOP;
				}
				break;

			case M_CHIME_STOP:
				flash_d3d4 = !flash_d3d4;
				if(getkeypress(S2)) {
					++config.chime_hour_stop;
					if(config.chime_hour_stop >= 24) config.chime_hour_stop = 0;
					config.chime_on = 1;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d3d4 = 0;
					dmode = M_CHIME_ON;
				}
				break;

			case M_CHIME_ON:
				flash_d1d2 = !flash_d1d2;
				flash_d3d4 = !flash_d3d4;
				if(getkeypress(S2)) {
					config.chime_on = !config.chime_on;
					configModified = 1;
				}
				if(getkeypress(S1)) {
					flash_d1d2 = 0;
					flash_d3d4 = 0;
					++dmode; // M_NORMAL
				}
				break;
			#endif // CFG_CHIME == 1

			case M_TEMP_DISP:
				if (getkeypress(S1)) {
					config.temp_offset++;
					if(config.temp_offset > 5) config.temp_offset = -5;
					configModified = 1;
				}
				if (getkeypress(S2))
					dmode = M_DATE_DISP;
				break;

			case M_DATE_DISP:
				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1))
					dmode = M_SET_MONTH;
				#endif // CFG_SET_DATE_TIME == 1 

				if (getkeypress(S2))
					dmode = M_WEEKDAY_DISP;
				break;

			case M_WEEKDAY_DISP:
				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1))
					ds_weekday_incr(&rtc);
				#endif // CFG_SET_DATE_TIME == 1 

				if (getkeypress(S2))
					dmode = M_SECONDS_DISP;
				break;

			case M_SECONDS_DISP:
				if (count % 10 < 4)
					display_colon = 1;

				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1))
					ds_seconds_reset();
				#endif // CFG_SET_DATE_TIME == 1 

				if (getkeypress(S2))
					dmode = M_NORMAL;
				break;

			case M_NORMAL:
			default:
				if (count % 10 < 4)
					display_colon = 1;

				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1) == PRESS_LONG && getkeypress(S2) == PRESS_LONG)
					ds_reset_clock();   

				if (getkeypress(S1 == PRESS_SHORT)) {
					dmode = M_SET_HOUR;
				}
				#endif // CFG_SET_DATE_TIME == 1 

				if (getkeypress(S2 == PRESS_SHORT)) {
					dmode = M_TEMP_DISP;
				}

		};

		// display execution tree
		switch (dmode) {
			case M_NORMAL:

			#if CFG_SET_DATE_TIME == 1
			case M_SET_HOUR:
			case M_SET_MINUTE:
			#endif
				showDp = DP_OFF;

				#if CFG_SET_DATE_TIME == 1
				if(dmode == M_SET_HOUR || dmode == M_SET_MINUTE) showDp = DP_OFF;
				#endif

				display(0, rtc.tenhour, rtc.hour, 1, rtc.tenminutes, rtc.minutes);

				break;

			case M_DATE_DISP:

			#if CFG_SET_DATE_TIME == 1
			case M_SET_MONTH:
			case M_SET_DAY:
			#endif
				display(0, rtc.tenday, rtc.day, 1, rtc.tenmonth, rtc.month);
				displayDp(1);
				break;

			#if CFG_ALARM == 1
			case M_ALARM_HOUR:
			case M_ALARM_MINUTE:
			case M_ALARM_ON:
				display(0, config.alarm_hour / 10, config.alarm_hour % 10, 1, config.alarm_minute / 10, config.alarm_minute % 10);
				if(config.alarm_on) displayDp(3);
				break;
			#endif

			#if CFG_CHIME == 1
			case M_CHIME_START:
			case M_CHIME_STOP:
			case M_CHIME_ON:
				display(0, config.chime_hour_start / 10, config.chime_hour_start % 10, 0, config.chime_hour_stop / 10, config.chime_hour_stop % 10);
				if(config.chime_on) displayDp(3);
				break;
			#endif

			case M_WEEKDAY_DISP:
				display(0, LED_BLANK, LED_DASH, 1, rtc.weekday, LED_DASH);
				break;

			case M_TEMP_DISP:
				display(0, ds_int2bcd_tens(temp), ds_int2bcd_ones(temp), 1, LED_TEMP, (temp >= 0) ? LED_BLANK : LED_DASH);  
				displayDp(2);
				break;

			case M_SECONDS_DISP:
				display(0, LED_BLANK, LED_BLANK, 1, rtc.tenseconds, rtc.seconds);
				break;

		}

		rotateThirdChar();
		dbufCur[0] = dbuf[0];
		dbufCur[1] = dbuf[1];
		dbufCur[2] = dbuf[2];
		dbufCur[3] = dbuf[3];

		// save ram config
		if(configModified) {
			ds_ram_config_write((uint8_t *) &config); 
			configModified = 0;
		}
		_delay_ms(100);
		count++;
		WDT_CLEAR();
	}
}
/* ------------------------------------------------------------------------- */

