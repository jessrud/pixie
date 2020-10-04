#!/bin/sh

if [ -z "$CFLAGS" ]
then
    CFLAGS="--std=gnu11 -Wall -Wextra -pedantic -Wno-gnu"
fi


if [ "$1" = "debug" ]
then
    cc $CFLAGS pixie.c -ggdb3 -o pixie
else
    cc $CFLAGS pixie.c -O3 -o pixie
fi
