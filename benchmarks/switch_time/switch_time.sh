#!/bin/sh

NAME=${1}
WITH_MOUSE=${2}
MODE=${3:-usb}
ENC_MODE=${4:-yes}

PRIV_DEV_SWITCHES="--priv_kbd"

if [ "${NAME}" = "" ]; then
    echo "Need an image name..."
    exit 1
fi

if [ "${WITH_MOUSE}" = "yes" ]; then
    echo "Testing private mouse too..."
    PRIV_DEV_SWITCHES="${PRIV_DEV_SWITCHES} --priv_mouse"
fi

if [ "${MODE}" != "usb" -a "${MODE}" != "pci" ]; then
    echo "MODE should be usb or pci"
    exit 1
fi

if [ "${ENC_MODE}" = "yes" ]; then
    echo "Encrypting input..."
    ENC_SWITCHES="--priv_input_encrypt"
fi

echo "Mode ${MODE}"

CURDIR=$(pwd)
SCRIPT_DIR=$(dirname $(readlink -f ${0}))

MAIN_DIR=$(readlink -f ${SCRIPT_DIR}/../..)
BASE_DATA_DIR=${MAIN_DIR}/data/switch_time
DATA_DIR=${BASE_DATA_DIR}/`hostname`_`date +"%m%d%y_%H%M%S"`

CPUSPEED=${MAIN_DIR}/utils/cpuspeed

mkdir -p ${DATA_DIR}
sleep 1

sudo echo "Begin"
echo "Data dir is ${DATA_DIR}"

cd ${MAIN_DIR}

${CPUSPEED} high
# Keep track of the lsusb output for records on what was connected
lsusb > ${DATA_DIR}/lsusb

for i in $(seq 1 5); do
    echo "Switch in and press keys rapidly when QEMU begins..."
    CMD="utils/kvm_linux --priv_${MODE} ${PRIV_DEV_SWITCHES} --priv_switch_bench ${ENC_SWITCHES} ${NAME}"
    sudo ${CMD} >${DATA_DIR}/${i}.dat 2>&1

if [ "${MODE}" = "pci" ]; then
    # Extra recovery action required here for now, this may well not
    # be the same on different computers...
    sudo ${MAIN_DIR}/utils/pci-bind.sh 0000:00:1a.0 ehci_hcd
    sudo ${MAIN_DIR}/utils/pci-bind.sh 0000:00:1d.0 ehci_hcd
fi

done

${CPUSPEED} low

cd ${CURDIR}

echo "End"
