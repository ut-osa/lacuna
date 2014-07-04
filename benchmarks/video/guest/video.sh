#!/bin/sh

video_mode=$(cat video_mode)

sleep 5
if [ "${video_mode}" = "net" ]; then
    mplayer http://darvill.csres.utexas.edu/basketball.flv
elif [ "${video_mode}" = "local" ]; then
    mplayer basketball.flv
fi