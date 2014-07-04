#!/bin/bash

for i in `seq 8 8 256`; do
    bgcolor=`printf "%02x" $i`
    fgcolor=`expr 256 - $i`
    fgcolor=`printf "%02x" $fgcolor`
    if [ $bgcolor -eq "100" ]; then
	bgcolor=ff
    fi
    wget http://dummyimage.com/600x400/${bgcolor}${bgcolor}${bgcolor}/${fgcolor}${fgcolor}${fgcolor}
    mv ${fgcolor}${fgcolor}${fgcolor}  ${fgcolor}${fgcolor}${fgcolor}.gif
done
