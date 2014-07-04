#!/bin/bash

TAP=$1

brctl delif br0 ${TAP}
ifconfig ${TAP} down
