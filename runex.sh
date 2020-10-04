#!/bin/sh

./build.sh debug
PIXIE_DEBUG=yes ./pixie < x.pixie > x.ppm

