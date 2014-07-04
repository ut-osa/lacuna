#!/bin/bash

dest=192.168.0.2
count=50

for psize in 1000 300 30 1
do
echo ping to $dest packet payload size $psize
ping $dest -s $psize -c $count
echo -------------------------------------------
echo -------------------------------------------
echo " "
echo " "
done

