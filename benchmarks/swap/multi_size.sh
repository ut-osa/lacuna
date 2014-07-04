#!/bin/bash

function call_awk() {
    awk "$1"
}
DEV="sda"
PART=5
#DEV="ram0"
PRIVOS_DIR="/home/mzlee/privos"
TEST_DIR="benchmarks/swap"
TEST_LIST="8gb.rand"
CMD_LIST="pfork"
echo "Script"                                        | awk '{print "[META] " $0}'
cat $0                                               | awk '{print "[META] " $0}'
echo                                                 | awk '{print "[META] " $0}'
echo "Scheduler"                                     | awk '{print "[META] " $0}'
cat /sys/block/${DEV}/queue/scheduler                | awk '{print "[META] " $0}'
echo                                                 | awk '{print "[META] " $0}'
echo "git-revision"                                  | awk '{print "[META] " $0}'
git rev-parse HEAD                                   | awk '{print "[META] " $0}'
echo                                                 | awk '{print "[META] " $0}'
echo "kernel"                                        | awk '{print "[META] " $0}'
uname -a                                             | awk '{print "[META] " $0}'
grep -i priv ${PRIVOS_DIR}/host/kbuild/.config       | awk '{print "[META] " $0}'
echo                                                 | awk '{print "[META] " $0}'
head -2 /proc/slabinfo                               | awk '{print "[META] " $0}'
grep "scratch_page\|swapped_page" /proc/slabinfo     | awk '{print "[META] " $0}'
for t in ${TEST_LIST}; do
    for cmd in ${CMD_LIST}; do
        awkstr="{ print \"[${t}:${cmd}] \" \$0 }"
        for i in $(seq 0 4); do
            echo "[META] Resetting for ${t} - ${cmd}"
            swapoff -a
            sleep 1
            echo 3 > /proc/sys/vm/drop_caches
            sleep 1
            swapon /dev/mapper/swap
            #swapon /dev/${DEV}${PART}
            sleep 2
            echo "[${t}:${cmd}] Iteration ${i}"
            echo "[${t}:${cmd}] ------------"
	    ${PRIVOS_DIR}/utils/do_trace > /dev/null &
	    sleep 2
	    kill %1
	    sleep 2
	    ${PRIVOS_DIR}/utils/do_trace > ${t}.${cmd}.${i}.trace &
            echo "[${t}:${cmd}] cpu before ${i}"
            head -1 /proc/stat                               | call_awk "${awkstr}"
            echo "[${t}:${cmd}] ${DEV}${PART} before ${i}"
            if [ -z "${PART}" ]; then
                cat /sys/block/${DEV}/stat                   | call_awk "${awkstr}"
            else
                cat /sys/block/${DEV}/${DEV}${PART}/stat     | call_awk "${awkstr}"
            fi
            /usr/bin/time ${PRIVOS_DIR}/${TEST_DIR}/${t} 2>&1 | call_awk "${awkstr}"
            #/usr/bin/time ${PRIVOS_DIR}/${TEST_DIR}/${cmd} ${PRIVOS_DIR}/${TEST_DIR}/${t} 2>&1 | call_awk "${awkstr}"
            echo "[${t}:${cmd}] cpu after ${i}"
            head -1 /proc/stat                               | call_awk "${awkstr}"
            echo "[${t}:${cmd}] ${DEV}${PART} after ${i}"
            if [ -z "${PART}" ]; then
                cat /sys/block/${DEV}/stat                   | call_awk "${awkstr}"
            else
                cat /sys/block/${DEV}/${DEV}${PART}/stat     | call_awk "${awkstr}"
            fi
            #echo "[${t}:${cmd}]"
            #grep "scratch_page\|swapped_page" /proc/slabinfo | call_awk "${awkstr}"
            #echo "[${t}:${cmd}]"
            sleep 5
            kill %1
            #insmod ${PRIVOS_DIR}/host/mem_scan/mem_scan.ko 2>&1 | call_awk "${awkstr}"
            #sleep 15
            dmesg | tail -10                                 | call_awk "${awkstr}"
        done
    done
done
