#!/bin/sh

PCI_NUM=$1
DRIVER_NAME=$2

PCI_SYSFS=/sys/bus/pci

bind_ctl=${PCI_SYSFS}/drivers/${DRIVER_NAME}/bind

if [ ! -e "${bind_ctl}" ]; then
    echo "No bind control file (right driver?)"
    exit 1
fi

echo ${PCI_NUM} >"${bind_ctl}"
