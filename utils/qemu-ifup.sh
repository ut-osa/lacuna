#!/bin/bash

#set -x

TAP=$1

ifconfig ${TAP} up
brctl addif br0 ${TAP}
