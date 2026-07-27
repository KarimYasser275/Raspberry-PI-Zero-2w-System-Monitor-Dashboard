#!/bin/sh
#author: Karim Yasser
set -eu
sysmond=/usr/bin/sysmond
usage(){
    echo "Usage: $0 start|stop"
}
echo "Running sysmond_daemon_start-stop"

if [ "$#" -ne 1 ]; then
    usage
    exit 1
fi

mode=$1

echo "Mode: $mode"

if [ "$mode" != "stop" ] && [ "$mode" != "start" ]; then
    usage
    exit 1
fi

if [ "$mode" = "start" ]; then
    echo "Starting daemon"
    ./"${sysmond}" -d
else
    echo "Stopping daemon"
    pid=$(ps aux | grep "sysmond -d" | awk '{print $2}')
    kill -TERM $pid
fi
exit 0