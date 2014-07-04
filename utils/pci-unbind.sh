#!/bin/sh

PCI_NUM=$1

PCI_SYSFS=/sys/bus/pci

driver_type_file=${PCI_SYSFS}/devices/${PCI_NUM}/driver

if [ ! -h ${driver_type_file} ]; then
    echo "No driver type file (device already unbound?)"
    exit 1
fi

driver_name=$(basename "$(readlink ${driver_type_file})")

unbind_ctl=${PCI_SYSFS}/drivers/${driver_name}/unbind

if [ ! -e "${unbind_ctl}" ]; then
    # This shouldn't happen...
    echo "No unbind control file..."
    exit 1
fi

echo ${PCI_NUM} >${unbind_ctl}
