#!/bin/bash
set -x

MCU=attiny85
AVRDUDE="avrdude -c usbtiny -p $MCU -B 32"

function dump-firmware() {
    NAME=trinket3V_backup_$(date +%F)
#    $AVRDUDE  -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h
    $AVRDUDE  -U lfuse:r:${NAME}.lfuse.hex:i \
              -U hfuse:r:${NAME}.hfuse.hex:i \
              -U efuse:r:${NAME}.efuse.hex:i \
              -U flash:r:${NAME}.flash.hex:i \
              -U eeprom:r:${NAME}.eeprom.hex:i
}

function first-time-install-micronucleos() {
    cd micronucleus/firmware
#    while (true); do
        make PROGRAMMER='-c usbtiny' fuse && break
#        sleep 5
#    done
#
#2022-09-04 only failed because i didn't connect Vcc...
#    while (true); do
        make PROGRAMMER='-c usbtiny' flash && break
#        sleep 5
#    done
#    ../commandline/micronucleus --run upgrade.hex
   echo "initial installation of micronucleos done"
}

#dump-firmware()
first-time-install-micronucleos
