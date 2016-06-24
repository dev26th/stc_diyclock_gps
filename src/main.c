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
#define SW1     P3_0
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

// UART
#define TXD    P3_1

volatile uint8_t TBUF;
volatile uint8_t TBIT;
volatile __bit   TING;
volatile __bit   TEND;

void uartInit() {
	TING = 0;
	TEND = 1;
}

void uartProcess() {
	if(TING) {
		if(TBIT == 0) {
			TXD = 0;
			TBIT = 9;
		}
		else if(--TBIT == 0) {
			TXD = 1;
			TING = 0;
			TEND = 1;
		}
		else {
			TBUF >>= 1;
			TXD = CY;
		}
	}
}

void uartSend(uint8_t d) {
	if(TEND) {
		TEND = 0;
		TBUF = d;
		TING = 1;
	}
}

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

	#if CFG_DCF77 == 1
	M_SYNC_DISP,
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

// DCF77
#if CFG_DCF77 == 1

#define DCF77_IN P3_6

#define DCF77_ZERO_MIN      6
#define DCF77_ZERO_MAX      14
#define DCF77_ONE_MIN       17
#define DCF77_ONE_MAX       24
#define DCF77_MIN_POS_PULSE 66
#define DCF77_START_MIN     110

#define DCF77_BIT_COUNT     59

enum Dcf77DataState {
	DCF77_DATA_INVALID,
	DCF77_DATA_COLLECTING,
	DCF77_DATA_VALID
};

static const uint8_t DCF77_DATA_POS[] = {
	// start in not used
	1,   //  weather
	9,   //  weather cont
	15,  //  callbit
	16,  //  dstChange
	17,  //  summer
	18,  //  winter
	19,  //  leapSecond
	20,  //  timeStart
	21,  //  minutes
	25,  //  tenMinutes
	28,  //  minutesPar
	29,  //  hour
	33,  //  tenHour
	35,  //  hourPar
	36,  //  day
	40,  //  tenDay
	42,  //  weekday
	45,  //  month
	49,  //  tenMonth
	50,  //  year
	54,  //  tenYear
	58   //  datePar
};

struct Dcf77Data {
	uint8_t  start;

	uint16_t weather;

	uint8_t  callbit;
	uint8_t  dstChange;
	uint8_t  summer;
	uint8_t  winter;
	uint8_t  leapSecond;

	uint8_t  timeStart;
	uint8_t  minutes;
	uint8_t  tenMinutes;
	uint8_t  minutesPar;
	uint8_t  hour;
	uint8_t  tenHour;
	uint8_t  hourPar;
	uint8_t  day;
	uint8_t  tenDay;
	uint8_t  weekday;
	uint8_t  month;
	uint8_t  tenMonth;
	uint8_t  year;
	uint8_t  tenYear;
	uint8_t  datePar;
};

struct Dcf77 {
	enum Dcf77DataState dataState;
	uint8_t actualState;   // which level had out input last time
	uint8_t stateCount;    // how many 10 ms ticks was same input level
	uint8_t bitPos;        // how many bit was already received for current minute
	struct Dcf77Data data;
} dcf77;

struct Dcf77TickFilter {
	uint8_t buf;
	uint8_t count;
} dcf77TickFilter;

struct Dcf77LastSync {
	uint8_t  minutes;
	uint8_t  tenMinutes;
	uint8_t  hour;
	uint8_t  tenHour;
} dcf77LastSync;

void dcf77_reset() {
	uint8_t i;
	for(i = 0; i < sizeof(dcf77); ++i) *((uint8_t*)&dcf77 + i) = 0;
}

void dcf77_addBit(uint8_t bit) {
	uint8_t byte, inByte;
	uint8_t * data = (uint8_t*)&dcf77.data;
	uint8_t const * posPos = DCF77_DATA_POS;
	uint8_t bitPos = dcf77.bitPos;

	if(bitPos >= DCF77_BIT_COUNT) return;

	if(bit) {
		for(byte = 0; byte < sizeof(DCF77_DATA_POS); ++byte) {
			if(DCF77_DATA_POS[byte] > bitPos) break;
		}
		inByte = bitPos - DCF77_DATA_POS[byte-1];
		data[byte] |= (1 << inByte);
	}

	++bitPos;
	dcf77.bitPos = bitPos;
}

void dcf77_commit() {
	// only minimal checks here to save code size
	uint8_t correct = 1;
	if(dcf77.bitPos != DCF77_BIT_COUNT) {
		correct = 0;  // it's not true for the leapsecond, but we ignore it
	}
	else if(dcf77.data.start != 0 || dcf77.data.timeStart != 1) {
		correct = 0;
	}

	// FIXME additional checks?

	if(correct) {
		dcf77.dataState = DCF77_DATA_VALID;
	}
	else {
		dcf77_reset();
	}
}

uint8_t dcf77_received;
uint8_t dcf77_receivedCount;

void dcf77_cycle10ms() {
	uint8_t newState = DCF77_IN;

	dcf77_received <<= 1;
	dcf77_received |= newState;
	if(++dcf77_receivedCount == 8) {
		uartSend(dcf77_received);
		dcf77_receivedCount = 0;
	}

	// pass through filter
	dcf77TickFilter.count += newState;
	dcf77TickFilter.buf <<= 1;
	dcf77TickFilter.count -= (dcf77TickFilter.buf & 0x80) >> 7;
	dcf77TickFilter.buf |= newState;
	newState = (dcf77TickFilter.count > 3) ? 1 : 0;

	if(newState == dcf77.actualState) {
		++dcf77.stateCount;
	}
	else {
		if(newState) { // should be begin of a second
			if(dcf77.stateCount >= DCF77_START_MIN) dcf77_commit(); // first bit in minite - use data from the previous minute
			else if(dcf77.stateCount < DCF77_MIN_POS_PULSE) dcf77_reset();
		}
		else { // decode the bit
			dcf77.dataState = DCF77_DATA_COLLECTING;

			if(dcf77.stateCount >= DCF77_ZERO_MIN && dcf77.stateCount <= DCF77_ZERO_MAX)
				dcf77_addBit(0);
			else if(dcf77.stateCount >= DCF77_ONE_MIN && dcf77.stateCount <= DCF77_ONE_MAX)
				dcf77_addBit(1);
			else
				dcf77_reset();
		}
		dcf77.stateCount = 0;
		dcf77.actualState = newState;
	}
}

// in 100 ms ticks
#define DCF77_MAX_DATA_EXPIRE 10ul*60*60*24
uint32_t dcf77DataExpire;

void dcf77ResetSync() {
	dcf77DataExpire = 0;
	dcf77LastSync.minutes    = 8;
	dcf77LastSync.tenMinutes = 8;
	dcf77LastSync.hour       = 8;
	dcf77LastSync.tenHour    = 8;
}

void dcf77CopyToRtc() {
	// a race-condition is possible here, but it cannot occur if this function will be called oft enough (every ~100 ms)
	if(dcf77.dataState == DCF77_DATA_VALID) {
		// take over new data from DCF77
		rtc.tenyear    = dcf77.data.tenYear;
		rtc.year       = dcf77.data.year;
		rtc.tenmonth   = dcf77.data.tenMonth;
		rtc.month      = dcf77.data.month;
		rtc.tenday     = dcf77.data.tenDay;
		rtc.day        = dcf77.data.day;
		rtc.weekday    = dcf77.data.weekday;

		rtc.tenhour    = dcf77.data.tenHour;
		rtc.hour       = dcf77.data.hour;
		rtc.tenminutes = dcf77.data.tenMinutes;
		rtc.minutes    = dcf77.data.minutes;
		rtc.tenseconds = 0;
		rtc.seconds    = 0;

		ds_writeburst((uint8_t const *) &rtc); // write rtc

		dcf77_reset();
		dcf77DataExpire = DCF77_MAX_DATA_EXPIRE;
		dcf77LastSync.minutes    = dcf77.data.minutes;
		dcf77LastSync.tenMinutes = dcf77.data.tenMinutes;
		dcf77LastSync.hour       = dcf77.data.hour;
		dcf77LastSync.tenHour    = dcf77.data.tenHour;
	}
	else if(dcf77DataExpire > 0) {
		--dcf77DataExpire;
	}
	else {
		dcf77ResetSync();
	}
}

#define timeChanged() dcf77DataExpire = 0

#else // CFG_DCF77 == 1

#define timeChanged()

#endif // CFG_DCF77 == 1

volatile uint8_t debounce[2];      // switch debounce buffer
volatile uint8_t switchcount[2];

void timer0_isr() __interrupt 1 __using 1
{
	// display refresh ISR
	// cycle thru digits one at a time
	uint8_t digit = displaycounter % 4;

	uartProcess();

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
	/*
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
*/
	++timerTicksNow;

	#if CFG_DCF77 == 1
	dcf77_cycle10ms();
	#endif // CFG_DCF77 == 1
}

void Timer0Init(void) // to match 9600 baud
{
	TMOD = 0x00;         // 16-bit auto-reload
	//AUXR = 0x80;         // 1T mode
	TL0 = 0xA0;          // Initial timer value
	TH0 = 0xFF;          // Initial timer value
	TF0 = 0;             // Clear TF0 flag
	TR0 = 1;             // Timer0 start run
	ET0 = 1;             // enable timer0 interrupt
	PT0 = 1;             // high-prio
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

/*********************************************/
int main()
{
	// SETUP
	// set ds1302, photoresistor, & ntc pins to open-drain output, already have strong pullups
	//P1M1 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);
	//P1M0 |= (1 << 0) | (1 << 1) | (1 << 2) | (1<<6) | (1<<7);

	//P3M1 = (1 << 6);
	//P3M0 = (0 << 6);
	P3M1 = (0 << 1);
	P3M0 = (1 << 1);

	// init rtc
	ds_init();
	// init/read ram config
	ds_ram_config_init((uint8_t *) &config);

	uartInit();
	Timer0Init(); // display refresh
	Timer1Init(); // switch debounce

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

		#if CFG_DCF77 == 1
		dcf77CopyToRtc();
		#endif // CFG_DCF77 == 1

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

			#if CFG_DCF77 == 1
			case M_SYNC_DISP:
				if (getkeypress(S2))
					dcf77ResetSync();
				if (getkeypress(S1))
					++dmode; // M_NORMAL
				break;
			#endif

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

				#if CFG_SET_DATE_TIME == 1
				if (getkeypress(S1) == PRESS_LONG && getkeypress(S2) == PRESS_LONG) {
					ds_reset_clock();
					timeChanged();
				}

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

				convertHourToShow(now.hour, &hourToShow1);
				display(CFG_HOUR_LEADING_ZERO, hourToShow1.tens, hourToShow1.ones, 1, rtc.tenminutes, rtc.minutes);
				displayPm(0, hourToShow1.pm);

				#if CFG_DCF77 == 1
				if((dcf77DataExpire > 0)
					|| (dcf77.dataState == DCF77_DATA_COLLECTING && !display_colon))
				{
					displayDp(0);
				}
				#endif // CFG_DCF77 == 1

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

			#if CFG_DCF77 == 1
			case M_SYNC_DISP:
				display(CFG_HOUR_LEADING_ZERO, dcf77LastSync.tenHour, dcf77LastSync.tenHour, 1, dcf77LastSync.tenMinutes, dcf77LastSync.minutes);
				displayDp(0);
				displayDp(1);
				displayDp(2);
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
	}
}

