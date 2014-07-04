#!/bin/bash

dest=192.168.0.100
duration=30

psize=(1000 300 30)
bufsize=(204800 102400 51200)

for ((i=0;i<3;i++))
do
echo TCP_STREAM to $dest packet payload size ${psize[$i]}
netperf -l $duration -t TCP_STREAM -H $dest -- -m ${psize[$i]} -s ${bufsize[$i]} -S ${bufsize[$i]} -D
done

