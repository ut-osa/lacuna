#!/bin/bash

# set -x

# Run with sudo!

dir=${1:-unspec}

eth_name=eth0
bridge_name=br0

PATH=$PATH:/usr/sbin:/sbin

ip_of_intf()
{
    ifconfig $1 | grep 'inet addr' | cut -d: -f2 | cut -d' ' -f1
}

mask_of_intf()
{
    ifconfig $1 | grep 'inet addr' | cut -d: -f4
}

dpkg -s bridge-utils >&/dev/null
if [ "$?" = "1" ]
then
    echo "Please install package \"bridge-utils\" (for brctl)."
    exit 1
fi

if [ "${dir}" != "start" ] && [ "${dir}" != "stop" ];
then
    echo "Invalid argument ${dir}, use start/stop."
    exit 1
fi

if [ "${dir}" = "start" ];
then
    # You also may not get this message before the freeze begins, but you
    # often seem to
    echo "Your network traffic may drop for a few seconds, this is normal."

    eth_ip=$(ip_of_intf ${eth_name})
    eth_mask=$(mask_of_intf ${eth_name})

    # Create bridge
    brctl addbr ${bridge_name}

    # Set bridge IP address to that of card
    ifconfig ${eth_name} 0.0.0.0
    brctl addif ${bridge_name} ${eth_name}
    ifconfig ${bridge_name} ${eth_ip} netmask ${eth_mask} up

else # ${dir} = "stop"

    br_ip=$(ip_of_intf ${bridge_name})
    br_mask=$(mask_of_intf ${bridge_name})

    # Delete bridge
    ifconfig ${bridge_name} down
    brctl delbr ${bridge_name}

    ifconfig ${eth_name} ${br_ip} netmask ${br_mask} up
fi
