# STC DIY Clock Kit + GPS firmware
Firmware replacement for STC15F mcu-based DIY Clock Kit (available from banggood [see below for link], aliexpress, et al.) Uses [sdcc](http://sdcc.sf.net) to build and [stcgal](https://github.com/grigorig/stcgal) to flash firmware on to STC15F204EA series microcontroller.

![Image of Banggood SKU972289](http://img.banggood.com/thumb/large/2014/xiemeijuan/03/SKU203096/A3.jpg?p=D9031748980672016067)

[link to Banggood product page for SKU 972289](http://www.banggood.com/DIY-4-Digit-LED-Electronic-Clock-Kit-Temperature-Light-Control-Version-p-972289.html?p=D9031748980672016067)

## features

* time display/set (12/24 hour modes as compile-time option)
* display seconds
* date display/set in MM/DD or DD/MM format (compile-time option)
* display auto-dim
* temperature display in °C or °F (compile-time option)
* alarm
* chime for selected hours
* clock synchronization with [GPS](https://en.wikipedia.org/wiki/GPS), additional hardware required

## hardware

* DIY LED Clock kit, based on STC15F204EA and DS1302, e.g. [Banggood SKU 972289](http://www.banggood.com/DIY-4-Digit-LED-Electronic-Clock-Kit-Temperature-Light-Control-Version-p-972289.html?p=D9031748980672016067)
* connected to PC via cheap USB-UART adapter, e.g. CP2102, CH340G. [Banggood: CP2102 USB-UART adapter](http://www.banggood.com/CJMCU-CP2102-USB-To-TTLSerial-Module-UART-STC-Downloader-p-970993.html?p=D9031748980672016067)
* GPS-receiver, with its Tx connected to P3.7

## requirements
* linux or mac (windows untested, but should work)
* sdcc installed and in the path
* stcgal (or optionally stc-isp). Note you can either do "git clone --recursive ..." when you check this repo out, or do "git submodule update --init --recursive" in order to fetch stcgal.

## usage
```
make clean
make
make flash
```

## options
* override default serial port:
`STCGALPORT=/dev/ttyUSB0 make flash`

* add other options:
`STCGALOPTS="-l 9600 -b 9600" make flash`

* a lot of compile-time options, see config.h; e.g.:
`COMPILEOPT='-D CFG_HOUR_MODE=12' make`

You can not enable all options at once - there is not enough space on the flash.

## firmware usage

If compiled with default options, pressing of S1 (the upper one) on start screen will cycle in:
set hour -> set minute -> set alarm hour -> set alarm minute -> alarm on/off -> chime start hour -> chime stop hour -> chime on/off

Use S2 (the lower one) to change corresponding value.

Pressing of S2 on the start screen will cycle in:
temperature -> date -> weekday -> seconds

To go to change mode, press S1 on corresponding screen.

On the start screen the last dot shows state if alarm (on/off).

If DCF77 is enabled, the first dot on the start screen shows its state: 
* off - no signal
* blinking - collecting data
* on - clock is synchronized

## clock assumptions
Some of the code assumes 11.0592 MHz internal RC system clock (set by stc-isp or stcgal).

## disclaimers
This code is provided as-is, with NO guarantees or liabilities.
As the original firmware loaded on an STC MCU cannot be downloaded or backed up, it cannot be restored. If you are not comfortable with experimenting, I suggest obtaining another blank STC MCU and using this to test, so that you can move back to original firmware, if desired.

### references
stc15f204ea english datasheet:
http://www.stcmcu.com/datasheet/stc/stc-ad-pdf/stc15f204ea-series-english.pdf

[original firmware operation flow state diagram](docs/DIY_LED_Clock_operation_original.png)
[kit instructions w/ schematic](docs/DIY_LED_Clock.png)

