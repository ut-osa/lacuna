#!/bin/sh

NAME=${1}
ENC_MODE=${2:-yes}
DISP=${3:-":0"}

if [ `whoami` != "root" ]; then
    echo "Run as root..."
    exit 1
fi

if [ "${NAME}" = "" ]; then
    echo "Need an image name..."
    exit 1
fi

if [ "${ENC_MODE}" = "yes" ]; then
    echo "Encrypting..."
    PRIV_NET="--priv_net"
    PRIV_SOUND="--priv_sound"
fi

SCRIPT_DIR=$(dirname $(readlink -f ${0}))

MAIN_DIR=$(readlink -f ${SCRIPT_DIR}/../..)
BASE_DATA_DIR=${MAIN_DIR}/data/video
DATA_DIR=${BASE_DATA_DIR}/`hostname`_`date +"%m%d%y_%H%M%S"`

mkdir -p ${DATA_DIR}

echo "Data dir is ${DATA_DIR}"

for i in $(seq 1 5); do
    echo "Rep ${i}"

    ifconfig >${DATA_DIR}/ifconfig.${i}.pre

    mpstat -P ALL 1 >${DATA_DIR}/${i}.cpu.dat 2>&1 &
    mpstat_pid=$!
    CMD="utils/kvm_linux --sound --tap ${PRIV_NET} ${PRIV_SOUND} ${NAME}"
    #CMD="utils/kvm_linux --private ${NAME}"
    echo ${CMD}

    # This clobbers your value of display...
    export DISPLAY=${DISP}
    if [ "${ENC_MODE}" != "yes" ]; then
	export DISABLE_GPU_ENC="yes"
    fi

    ${CMD} >${DATA_DIR}/${i}.qemu.log 2>&1 &
    unset DISPLAY
    unset DISABLE_GPU_ENC

    # Without the sleep, the pgrep doesn't seem to work...
    # $! doesn't seem to work with the DISPLAY= part in front...
    sleep 1
    qemu_pid=$(pgrep qemu)

    # Let's let the VM go for a while
    sleep 90

    kill ${qemu_pid} ${mpstat_pid}
    wait %1 %2

    ifconfig >${DATA_DIR}/ifconfig.${i}.post
    dmesg | tail -20 >${DATA_DIR}/${i}.dmesg.log

    echo "Sleeping in between..."
    sleep 5
done

echo "Done"
