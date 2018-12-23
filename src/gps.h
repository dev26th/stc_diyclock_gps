#ifndef GPS_H
#define GPS_H

#include <stdint.h>

struct gps_DateTime {
    uint8_t valid;
    uint8_t wait;

    uint8_t seconds;
    uint8_t tenseconds;
    uint8_t minutes;
    uint8_t tenminutes;
    uint8_t hour;
    uint8_t tenhour;
    uint8_t day;
    uint8_t tenday;
    uint8_t month;
    uint8_t tenmonth;
    uint8_t year;
    uint8_t tenyear;
};

extern struct gps_DateTime gps_datetime;

void gps_init();

void gps_cycle();

void gps_cycle10ms();

#endif // GPS_H
