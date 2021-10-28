#!/bin/sh

if [ $# -ne "2" ]
    then
    echo "usage: $0 file dest"
    echo " file : file to copy to all clients"
    echo " dest : path on clients to copy file to"
    exit 1
fi

# static options for local machine
ssh_key=~/keys/root-r900-id_rsa
client_ip_command=./client-list.pl

$client_ip_command | while read ipaddr
  do
  echo "copying $1 -> $ipaddr:$2"
  #ssh -n -i $ssh_key -l root $ipaddr "rm -f $2/$1"
  scp -r -i $ssh_key $1 root@$ipaddr:$2
done
