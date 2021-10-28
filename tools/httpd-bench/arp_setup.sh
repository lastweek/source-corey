#!/bin/sh

if [ $# -ne "1" ]
    then
    echo "usage: $0 arp-config-file"
    exit 1
fi

cat $1 | while read arpline
  do
  arp -s $arpline
done
