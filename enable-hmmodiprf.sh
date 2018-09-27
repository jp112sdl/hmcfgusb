#!/bin/sh

modprobe cp210x
echo 1b1f c020 >/sys/bus/usb-serial/drivers/cp210x/new_id
