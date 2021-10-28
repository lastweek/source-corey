#!/bin/sh

if [ $# -ne "1" ]
    then
    echo "usage: $0 ip-list"
    exit 1
fi

cat $1 | while read ipaddr
  do
  arp -d $ipaddr
done
