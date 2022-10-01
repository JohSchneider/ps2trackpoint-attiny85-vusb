#!/bin/bash

if [ ! -f micronucleus/commandline/micronucleus ]; then
    pushd micronucleus/commandline
    make
    popd
fi

if [ ! -f micronucleus/firmware/main.hex ]; then
    pushd micronucleus/firmware
    make CONFIG=t85_default
    popd
fi

pushd firmware
make clean flash
#resetting the board is enough - no need to actually re-plug the usb cable
popd
