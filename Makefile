-include Makefile.local

SDCC ?= sdcc
SDCCOPTS ?= --iram-size 256 --code-size 4089 --xram-size 0
COMPILEOPT ?=
STCGAL ?= stcgal/stcgal.py
STCGALOPTS ?= 
STCGALPORT ?= /dev/ttyUSB0
FLASHFILE ?= main.hex
SYSCLK ?= 11059

SRC = src/ds1302.c src/gps.c

OBJ=$(patsubst src%.c,build%.rel, $(SRC))

all: main

build/%.rel: src/%.c
	mkdir -p $(dir $@)
	$(SDCC) $(SDCCOPTS) $(COMPILEOPT) -o $@ -c $<

main: $(OBJ)
	$(SDCC) $(COMPILEOPT) -o build/ src/$@.c $(SDCCOPTS) $^
	cp build/$@.ihx $@.hex
	
flash:
	$(STCGAL) -p $(STCGALPORT) -P stc15a -t $(SYSCLK) $(STCGALOPTS) $(FLASHFILE)

clean:
	rm -f *.ihx *.hex *.bin
	rm -rf build/*

