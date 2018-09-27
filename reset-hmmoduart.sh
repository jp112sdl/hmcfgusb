#!/bin/bash

if [ "`id -u`" != "0" ]; then
	exec sudo "${0}"
fi

if [ ! -d "/sys/class/gpio/gpio18" ]; then
	echo 18 >/sys/class/gpio/export
fi

echo out >/sys/class/gpio/gpio18/direction
echo 0 >/sys/class/gpio/gpio18/value
sleep 0.2
echo 1 >/sys/class/gpio/gpio18/value
sleep 2
