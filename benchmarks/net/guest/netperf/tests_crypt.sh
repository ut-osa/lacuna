#!/bin/bash

dest=192.168.0.2
duration=30
bufsize=204800

for psize in 1000 300 30
do
echo TCP_STREAM to $dest packet payload size $psize
netperf -l $duration -t TCP_STREAM -H $dest -- -m $psize -D -s $bufsize -S $bufsize 
echo -------------------------------------------
echo -------------------------------------------
echo " "
done


echo -------------------------------------------
echo TCP_RR to $dest
netperf -l $duration -H $dest -t TCP_RR

echo TCP_CC to $dest
netperf -l $duration -H $dest -t TCP_CC

echo TCP_CRR to $dest
netperf -l $duration -H $dest -t TCP_CRR

