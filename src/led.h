// LED functions for 4-digit seven segment led

#include <stdint.h>


// index into ledtable[]
#define LED_c   10
#define LED_f   11
#define LED_BLANK  12
#define LED_DASH   13
#define LED_h   14

static const uint8_t ledtable[] = {
    // digit to led digit lookup table
    // dp,g,f,e,d,c,b,a
    // negative image
    0b11000000, // 0
    0b11111001, // 1
    0b10100100, // 2
    0b10110000, // 3
    0b10011001, // 4
    0b10010010, // 5
    0b10000010, // 6
    0b11111000, // 7
    0b10000000, // 8
    0b10010000, // 9
    0b11000110, // C
    0b10001110, // F
    0b11111111, // 0x10 - ' '
    0b10111111, // 0x11 - '-'
    0b10001011, // 0x12 - 'h'
};

