#!/bin/bash

if [ $# -lt 1 ]; then
    echo "$0 [bench_name]"
    echo
    echo "bench_name: web_browse web_youtube libreoffice"
    echo
    exit 1
fi

if [ ! -e /dev/priv_crypt_ctl ]; then
    crypto/kernel/scripts/priv_crypt_mkdev     
    if [ ! -e /dev/priv_crypt_ctl ] && [ -z "$DISABLE_ENCRYPT" ] ; then
	echo "/dev/priv_crypt_ctl does not exists"
	exit 1
    fi
fi

BENCHMARK=$1

if [ "$BENCHMARK" = "web_browse" ]; then
	TIME_WAIT=70
elif [ "$BENCHMARK" = "web_youtube" ]; then
	TIME_WAIT=107
elif [ "$BENCHMARK" = "libreoffice" ]; then
	TIME_WAIT=220
elif [ "$BENCHMARK" = "video" ]; then
	TIME_WAIT=90
fi

RUN_ID=$2

if [ -z "$RUN_ID" ]; then
	RUN_ID=$(date +%y%m%d_%H%M%S_%N)
fi

#IMAGE_NAME=privos-bench-ubuntu-11.10-amd64.img 
IMAGE_NAME=base.img

./utils/connect_hd $IMAGE_NAME
echo $BENCHMARK > /mnt/vm1/home/osa/xinit_bench_list.txt
./utils/disconnect_hd 

if [ "x${DISABLE_ENCRYPT}" != "x" ]; then
    PRIV_OPTIONS=
    export DISABLE_GPU_ENC=yes
    OUTPUT_DIR=notprivate
    qemu-img create -f qcow2 -b $IMAGE_NAME diff_${IMAGE_NAME}
    IMAGE_NAME=diff_${IMAGE_NAME}

    PRIV_OPTIONS="--sound --tap"

else
    OUTPUT_DIR=private
    qemu-img create -f qcow2 -o encryption -b $IMAGE_NAME diff_${IMAGE_NAME}
    IMAGE_NAME=diff_${IMAGE_NAME}
    if [ "$BENCHMARK" = "video" ]; then
    PRIV_OPTIONS="--sound --priv_sound --priv_net --priv_diff --tap"
    else
    PRIV_OPTIONS="--sound --priv_sound --priv_net --priv_diff"
    fi
fi



killall mpstat

DATA_DIR=./bench_output/${OUTPUT_DIR}/single/${BENCHMARK}/

if [ ! -d ${DATA_DIR} ]; then
    mkdir -p ${DATA_DIR}
fi


mpstat -P ALL 1 >${DATA_DIR}/${RUN_ID}.cpu.dat 2>&1 &
mpstat_pid=$!

echo "DISPLAY=:0 utils/kvm_linux ${PRIV_OPTIONS} ${IMAGE_NAME} & "
DISPLAY=:0 utils/kvm_linux ${PRIV_OPTIONS} ${IMAGE_NAME}  >${DATA_DIR}/${RUN_ID}.qemu.log 2>&1 & 

sleep 10

qemu_pid=`pgrep qemu`
sleep ${TIME_WAIT}

if [ "$BENCHMARK" != "video" ]; then
while ! ssh -p 2222 -i privos.key osa@localhost ls ./benchmark/output/web*; do
    if ! ssh -p 2222 -i privos.key osa@localhost ls ./benchmark/output/office*; then
       sleep 10
    else
       break
    fi
done

mkdir -p ./bench_output/${OUTPUT_DIR}/single/
scp -P 2222 -i privos.key osa@localhost:~/benchmark/output/* $DATA_DIR
fi

kill -s 9 $qemu_pid $mpstat_pid
