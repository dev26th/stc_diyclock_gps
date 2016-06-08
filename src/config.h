#ifndef CONFIG_H
#define CONFIG_H

// build-time configuration options:
// CFG_HOUR_MODE 24 or 12
// CFG_TEMP_UNIT 'C' or 'F'
// CFG_SET_DATE_TIME 1 or 0
// CFG_ALARM 1 or 0
// CFG_CHIME 1 or 0

// FIXME option for date format

// defaults for the configuration options:
#ifndef CFG_HOUR_MODE
#define CFG_HOUR_MODE 24
#endif

#ifndef CFG_TEMP_UNIT
#define CFG_TEMP_UNIT 'C'
#endif

#ifndef CFG_SET_DATE_TIME
#define CFG_SET_DATE_TIME 1
#endif

#ifndef CFG_ALARM
#define CFG_ALARM 1
#endif

#ifndef CFG_CHIME
#define CFG_CHIME 1
#endif

#endif // CONFIG_H

