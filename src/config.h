#ifndef CONFIG_H
#define CONFIG_H

// build-time configuration options:
// CFG_HOUR_MODE 24 or 12
// CFG_HOUR_LEADING_ZERO 0 (e.g. 6:00, implied by 12-hour format) or 1 (e.g. 06:00)
// CFG_DATE_FORMAT 1 (DD/MM) or 2 (MM/DD)
// CFG_MONTH_LEADING_ZERO 1 or 0
// CFG_DAY_LEADING_ZERO 1 or 0
// CFG_TEMP_UNIT 'C' or 'F'
// CFG_SET_DATE_TIME 1 or 0
// CFG_ALARM 1 or 0
// CFG_CHIME 1 or 0
// CFG_GPS_CORRECTION in 10 ms ticks
// other durations are in 100 ms ticks
// defaults for the configuration options:

#ifndef CFG_ALARM_DURATION
#define CFG_ALARM_DURATION 600
#endif

#ifndef CFG_CHIME_DURATION
#define CFG_CHIME_DURATION 1
#endif

#ifndef CFG_HOUR_MODE
#define CFG_HOUR_MODE 24
#endif

#ifndef CFG_HOUR_LEADING_ZERO
#define CFG_HOUR_LEADING_ZERO 0
#endif

#ifndef CFG_DATE_FORMAT
#define CFG_DATE_FORMAT 1
#endif

#ifndef CFG_MONTH_LEADING_ZERO
#define CFG_MONTH_LEADING_ZERO 1
#endif

#ifndef CFG_DAY_LEADING_ZERO
#define CFG_DAY_LEADING_ZERO 0
#endif

#ifndef CFG_TEMP_UNIT
#define CFG_TEMP_UNIT 'C'
#endif

#ifndef CFG_SET_DATE_TIME
#define CFG_SET_DATE_TIME 0
#endif

#ifndef CFG_ALARM
#define CFG_ALARM 1
#endif

#ifndef CFG_CHIME
#define CFG_CHIME 1
#endif

#ifndef CFG_GPS_CORRECTION
#define CFG_GPS_CORRECTION 80
#endif

#endif // CONFIG_H

