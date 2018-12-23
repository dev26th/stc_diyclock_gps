//
// STC15F204EA DIY LED Clock
// Copyright 2016, Jens Jensen
//
// UART functionality is from http://www.stcmicro.com/datasheet/STC15F204EA-en.pdf

#include <stc12.h>
#include <stdint.h>

#include "uart.h"
#include "config.h"
#include "adc.h"
#include "ds1302.h"
#include "led.h"
#include "gps.h"

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
#define DP_OFF 0x00
#define DP_ON  0x80

// should the PM be shown, negative logic
#define PM_OFF 0x00
#define PM_ON  0x20

#define BAUD 9600
#define RXB  P3_7

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
	M_SECONDS_DISP,
	M_SET_OFFSET,
};

/* ------------------------------------------------------------------------- */

volatile uint8_t timerTicksNow;
// delay may be only tens of ms
void _delay_ms(uint8_t ms)
{
	uint8_t stop = timerTicksNow + ms / 10;
	while(timerTicksNow != stop) {
		gps_cycle();
	}
}

// GLOBALS
uint8_t i;
uint16_t count;
int16_t temp;      // temperature sensor value
uint8_t lightval;  // light sensor value
uint8_t beep;      // actual number of sound-request

#if CFG_ALARM == 1
#define ALARM_DURATION_NO (uint16_t)-1
uint16_t alarmDuration = ALARM_DURATION_NO;
#endif // CFG_ALARM == 1

#if CFG_CHIME == 1
#define CHIME_DURATION_NO (uint8_t)-1
uint8_t chimeDuration = CHIME_DURATION_NO;
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

struct HourToShow {
	uint8_t tens;
	uint8_t ones;
	uint8_t pm;
};
struct HourToShow hourToShow1, hourToShow2;

void convertHourToShow(uint8_t hour, struct HourToShow * toShow) {
#if CFG_HOUR_MODE == 12
	toShow->pm = 0;
	if(hour >= 12) {
		toShow->pm = 1;
		hour -= 12;
	}
	if(hour == 0) hour = 12;
#else // CFG_HOUR_MODE == 12
	// pm should be already 0
#endif // CFG_HOUR_MODE == 12
	toShow->tens = hour / 10;
	toShow->ones = hour % 10;
}

volatile uint8_t displaycounter;
uint8_t dbuf[4];             // led display buffer, next state
uint8_t dbufCur[4];          // led display buffer, current state
uint8_t dmode = M_NORMAL;    // display mode state
__bit display_colon;         // flash colon

__bit  flash_d1d2;
__bit  flash_d3d4;

volatile uint8_t debounce[2];      // switch debounce buffer
volatile uint8_t switchcount[2];

// uart
uint8_t RBUF;
__bit REND;
static uint8_t RDAT;
static uint8_t RCNT;
static uint8_t RBIT;
static __bit RING;

void timer0_isr() __interrupt 1 __using 1
{
	// display refresh ISR
	// cycle thru digits one at a time
	uint8_t digit = displaycounter % 4;
	//P3_1 = 1;

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
	
	// uart rx
	if(RING) {
		if(--RCNT == 0) {
			RCNT = 3;                // reset send baudrate counter
			if(--RBIT == 0) {
				RBUF = RDAT;         // save the data to RBUF
				RING = 0;            // stop receive
				REND = 1;            // set receive completed flag
			}
			else {
				RDAT >>= 1;
				if(RXB) RDAT |= 0x80; //shift RX data to RX buffer
			}
		}
	}
	else if(!RXB) {
		RING = 1; // set start receive flag
		RCNT = 4; // initial receive baudrate counter
		RBIT = 9; // initial receive bit number (8 data bits + 1 stop bit)
	}
	//P3_1 = 0;
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
	
	gps_cycle10ms();
}

void Timer0Init(void) // ~34.7 us for 9600 UART
{
	TL0 = (uint8_t)(0xFF-(FOSC/3/BAUD/12)); // Initial timer value
	TH0 = 0xFF;          // Initial timer value
	TF0 = 0;             // Clear TF0 flag
	TR0 = 1;             // Timer0 start run
	ET0 = 1;             // enable timer0 interrupt
	EA = 1;              // global interrupt enable
}

void Timer1Init(void) // 10ms @ 11.0592MHz
{
	TL1 = 0xD5;          // Initial timer value
	TH1 = 0xDB;          // Initial timer value
	TF1 = 0;             // Clear TF1 flag
	TR1 = 1;             // Timer1 start run
	ET1 = 1;             // enable Timer1 interrupt
	EA = 1;              // global interrupt enable
}

void uart_init()
{
	RING = 0;
	REND = 0;
	RCNT = 0;
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

#if CFG_HOUR_MODE == 12
#define CFG_HOUR_LEADING_ZERO 0
#define displayPm(pos, pm) if(pm) dbuf[pos] &= ~PM_ON
#else
#define displayPm(pos, pm)
#endif // CFG_HOUR_MODE == 12

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

uint8_t daysInMonth(uint8_t y, uint8_t m) {
	switch(m) {
		case 4:
		case 6:
		case 9:
		case 11:
			return 30;
			
		case 2:
			return (y % 4 == 0 ? 29 : 28);
			
		default:
			return 31;
	}
}

uint8_t dayOfWeek(uint8_t y, uint8_t m, uint8_t d) {
	static const uint8_t OFFSETS[] = { 1, 4, 4, 0, 2, 5, 0, 3, 6, 1, 4, 6 };
	uint8_t v = y / 4 + d + OFFSETS[m-1];
	if((y % 4 == 0) && (m == 1 || m == 2)) v -= 1;
	v += 6 /* correct for 2000-2099 */ + y + 5;
	v %= 7;
	return (v + 1);
}

// in 100 ms ticks
#define GPS_MAX_DATA_EXPIRE 10ul*60*60*24
uint32_t gpsDataExpire;

#define timeChanged() gpsDataExpire = 0

void gpsCopyToRtc() {
	uint8_t y = gps_datetime.tenyear * 10 + gps_datetime.year;
	uint8_t m = gps_datetime.tenmonth * 10 + gps_datetime.month;
	uint8_t d = gps_datetime.tenday * 10 + gps_datetime.day;
	uint8_t h = gps_datetime.tenhour * 10 + gps_datetime.hour;
	
	if(config.time_offset < 0) {
		// FIXME
	}
	else {
		h += config.time_offset;
		if(h > 23) {
			h -= 24;
			++d;
			if(d > daysInMonth(y, m)) {
				d = 1;
				++m;
				if(m > 12) {
					m = 1;
					++y;
				}
			}
		}
	}
	
	rtc.tenyear    = y / 10;
	rtc.year       = y % 10;
	rtc.tenmonth   = m / 10;
	rtc.month      = m % 10;
	rtc.tenday     = d / 10;
	rtc.day        = d % 10;
	rtc.weekday    = dayOfWeek(y, m, d);

	rtc.tenhour    = h / 10;
	rtc.hour       = h % 10;
	rtc.tenminutes = gps_datetime.tenminutes;
	rtc.minutes    = gps_datetime.minutes;
	rtc.tenseconds = gps_datetime.tenseconds;
	rtc.seconds    = gps_datetime.seconds;

	//P3_1 = 1;
	ds_writeburst((uint8_t const *) &rtc); // write rtc
	gpsDataExpire = GPS_MAX_DATA_EXPIRE;
	//P3_1 = 0;
	// FIXME there is about +890.5 ms offset here
}

/*********************************************/
int main()
{
	// SETUP
	// set ds1302, photoresistor & ntc pins to open-drain output, already have strong pullups
	P1M1 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);
	P1M0 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);

	// init rtc
	ds_init();
	// init/read ram config
	ds_ram_config_init((uint8_t *) &config);

	Timer0Init(); // display refresh
	Timer1Init(); // switch debounce
	
	uart_init();
	gps_init();

	// LOOP
	while(1)
	{
		_delay_ms(100);
		count++;
		WDT_CLEAR();

		// run every ~1 secs
		if (count % 4 == 0) {
			lightval = getADCResult(ADC_LIGHT) >> 5;
			temp = gettemp(getADCResult(ADC_TEMP)) + config.temp_offset;

			// constrain dimming range
			if (lightval < 4)
				lightval = 4;
		}

		if(gps_datetime.valid && !gps_datetime.wait) {
			gpsCopyToRtc();
			gps_datetime.valid = 0;
		}
		else {
			if(gpsDataExpire > 0)
				--gpsDataExpire;
		}
		ds_readburst((uint8_t *) &rtc); // read rtc
		convertNow();

		#if CFG_ALARM == 1
		// check alarm
		if(alarmDuration == ALARM_DURATION_NO) {
			if(config.alarm_on) {
				if(config.alarm_hour == now.hour && config.alarm_minute == now.minutes) {
					alarmDuration = CFG_ALARM_DURATION;
					++beep;
				}
			}
		}
		else if(alarmDuration == 0) {
			if(config.alarm_hour != now.hour) {
				alarmDuration = ALARM_DURATION_NO; // forget about last alarm after one hour
			}
		}
		else {
			if(getkeypress(S1) || getkeypress(S2)) {
				alarmDuration = 0;
				--beep;
				continue; // don't interpret same key again
			}
			--alarmDuration;
			if(alarmDuration == 0) --beep;
		}
		#endif // CFG_ALARM == 1

		#if CFG_CHIME == 1
		// check chime
		if(chimeDuration == CHIME_DURATION_NO) {
			if(config.chime_on) {
				if(now.minutes == 0 && now.seconds == 0) {
					if((config.chime_hour_start <= config.chime_hour_stop && config.chime_hour_start <= now.hour && now.hour <= config.chime_hour_stop)
						|| (config.chime_hour_start > config.chime_hour_stop && (config.chime_hour_start <= now.hour || now.hour <= config.chime_hour_stop)))
					{
						chimeDuration = CFG_CHIME_DURATION;
						++beep;
					}
				}
			}
		}
		else if(chimeDuration == 0) {
			if(now.minutes != 0) {
				chimeDuration = CHIME_DURATION_NO; // forget about last chime
			}
		}
		else {
			--chimeDuration;
			if(chimeDuration == 0) --beep;
		}
		#endif // CFG_CHIME == 1

		BUZZER = (beep ? 0 : 1);

		// display decision tree
		display_colon = 0;
		switch (dmode) {
			#if CFG_SET_DATE_TIME == 1
			case M_SET_HOUR:
				display_colon = 1;
				flash_d1d2 = !flash_d1d2;
				if (getkeypress(S2)) {
					ds_hours_incr(now.hour);
					timeChanged();
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
					timeChanged();
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
					timeChanged();
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
					timeChanged();
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
					alarmDuration = ALARM_DURATION_NO; // reset alarm state
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
					alarmDuration = ALARM_DURATION_NO; // reset alarm state
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
					alarmDuration = ALARM_DURATION_NO; // reset alarm state
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

			case M_SET_OFFSET:
				flash_d1d2 = !flash_d1d2;
				flash_d3d4 = !flash_d3d4;
				if (getkeypress(S2)) {
					config.time_offset++;
					if(config.time_offset > 14) config.time_offset = -12;
					configModified = 1;
				}
				if (getkeypress(S1)) {
					flash_d1d2 = 0;
					#if CFG_SET_DATE_TIME == 1
						dmode = M_SET_HOUR;
					#else
						dmode = 0;  // M_ALARM_HOUR, M_CHIME_START or M_NORMAL
					#endif
				}
				break;

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
				if (getkeypress(S1)) {
					ds_weekday_incr(&rtc);
					timeChanged();
				}
				#endif // CFG_SET_DATE_TIME == 1

				if (getkeypress(S2))
					dmode = M_SECONDS_DISP;
				break;

			case M_SECONDS_DISP:
				if (count % 10 < 4)
					display_colon = 1;

				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1)) {
					ds_seconds_reset();
					timeChanged();
				}
				#endif // CFG_SET_DATE_TIME == 1

				if (getkeypress(S2))
					dmode = M_NORMAL;
				break;

			case M_NORMAL:
			default:
				if (count % 10 < 4)
					display_colon = 1;

				if (getkeypress(S1) == PRESS_LONG && getkeypress(S2) == PRESS_LONG) {
					ds_reset_clock();
					timeChanged();
				}

				if (getkeypress(S1 == PRESS_SHORT)) {
					dmode = M_SET_OFFSET;
				}

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

				convertHourToShow(now.hour, &hourToShow1);
				display(CFG_HOUR_LEADING_ZERO, hourToShow1.tens, hourToShow1.ones, 1, rtc.tenminutes, rtc.minutes);
				displayPm(0, hourToShow1.pm);

				if(gpsDataExpire)
					displayDp(0);

				#if CFG_ALARM == 1
				if(dmode == M_NORMAL && config.alarm_on) displayDp(3);
				#endif // CFG_ALARM == 1

				break;

			case M_DATE_DISP:

			#if CFG_SET_DATE_TIME == 1
			case M_SET_MONTH:
			case M_SET_DAY:
			#endif

				#if CFG_DATE_FORMAT == 1
				display(CFG_DAY_LEADING_ZERO, rtc.tenday, rtc.day, CFG_MONTH_LEADING_ZERO, rtc.tenmonth, rtc.month);
				#else
				display(CFG_MONTH_LEADING_ZERO, rtc.tenmonth, rtc.month, CFG_DAY_LEADING_ZERO, rtc.tenday, rtc.day);
				#endif

				displayDp(1);
				break;

			#if CFG_ALARM == 1
			case M_ALARM_HOUR:
			case M_ALARM_MINUTE:
			case M_ALARM_ON:
				convertHourToShow(config.alarm_hour, &hourToShow1);
				display(CFG_HOUR_LEADING_ZERO, hourToShow1.tens, hourToShow1.ones, 1, config.alarm_minute / 10, config.alarm_minute % 10);
				displayPm(0, hourToShow1.pm);
				if(config.alarm_on) displayDp(3);
				break;
			#endif

			#if CFG_CHIME == 1
			case M_CHIME_START:
			case M_CHIME_STOP:
			case M_CHIME_ON:
				convertHourToShow(config.chime_hour_start, &hourToShow1);
				convertHourToShow(config.chime_hour_stop, &hourToShow2);
				display(CFG_HOUR_LEADING_ZERO, hourToShow1.tens, hourToShow1.ones, CFG_HOUR_LEADING_ZERO, hourToShow2.tens, hourToShow2.ones);
				displayPm(0, hourToShow1.pm);
				displayPm(2, hourToShow2.pm);
				if(config.chime_on) displayDp(3);
				break;
			#endif

			case M_SET_OFFSET:
				{
					int8_t v = config.time_offset;
					if(v >= 10)
						display(0, LED_BLANK, LED_BLANK, 1, ds_int2bcd_tens(v), ds_int2bcd_ones(v));
					else if(v >= 0)
						display(0, LED_BLANK, LED_BLANK, 1, LED_BLANK, v);
					else if(v <= -10)
						display(0, LED_BLANK, LED_DASH, 1, ds_int2bcd_tens(-v), ds_int2bcd_ones(-v));
					else
						display(0, LED_BLANK, LED_BLANK, 1, LED_DASH, -v);

					displayDp(0);
				}

				break;

			case M_WEEKDAY_DISP:
				display(0, LED_BLANK, LED_DASH, 1, rtc.weekday, LED_DASH);
				break;

			case M_TEMP_DISP:
				display(0, ds_int2bcd_tens(temp), ds_int2bcd_ones(temp), 1, LED_TEMP, (temp >= 0) ? LED_BLANK : LED_DASH);
				displayDp(2);
				break;

			case M_SECONDS_DISP:
				display(0, LED_BLANK, LED_BLANK, 1, rtc.tenseconds, rtc.seconds);

				if(gpsDataExpire)
					displayDp(0);

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
	}
}
/* ------------------------------------------------------------------------- */

