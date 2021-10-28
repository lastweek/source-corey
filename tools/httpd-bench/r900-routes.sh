#!/bin/sh

# static options for local machine
ssh_key=~/keys/root-r900-id_rsa
client_ip_command=./client-list.pl
irq_affinity_script=irq-affinity.pl
pushf_command=./pushf.sh
arp_flush_script=arp_flush.sh
arp_setup_script=arp_setup.sh

if [ $# -ne "6" ]
    then
    echo "usage: $0 server-dev-num client-per-dev server-dev-file"
    echo -e "\t\t\tserver-mac-file server-ip-file arp-config-file"
    echo " server-dev-num : number of devices on the server to use"
    echo " client-per-dev : how many clients connect to one device"
    echo " server-dev-file: list of server device names (eth0, eth1, ...)"
    echo " server-mac-file : list of server mac devices"
    echo " server-ip-file : list of server IP addresses, only used for login"
    echo " arp-config-file : list of ip addresses and mac address pairs"
    exit 1
fi

server_dev_num=$1
client_per_dev=$2
client_num=$(( $client_per_dev * $server_dev_num ))
server_dev_file=$3
server_mac_file=$4
server_ip_file=$5
arp_config_file=$6

$client_ip_command > client_ip.txt

exec 10<client_ip.txt
let count=0
while read LINE <&10; do
    client[$count]=$LINE
    ((count++))
done
exec 10>&-

exec 11<$server_ip_file
let count=0
while read LINE <&11; do
    server[$count]=$LINE
    ((count++))
done
exec 11>&-

exec 12<$server_dev_file
let count=0
while read LINE <&12; do
    dev[$count]=$LINE
    ((count++))
done
exec 12>&-

exec 13<$server_mac_file
let count=0
while read LINE <&13; do
    mac[$count]=$LINE
    ((count++))
done
exec 13>&-

# Setup routing table on R900
dev_index=0
client_index=0
while [ $dev_index -lt $server_dev_num ]
  do
  client_it=0
  while [ $client_it -lt $client_per_dev ]
    do
    d=${dev[$dev_index]}
    c=${client[$client_index]}
    printf "route %s out %s\n" $c $d
    ssh -i $ssh_key -l root ${server[0]} "route del $c; route add -net $c netmask 255.255.255.255 dev $d"
    client_it=$(( $client_it + 1 ))
    client_index=$(( $client_index + 1 ))
  done
  dev_index=$(( $dev_index + 1 ))
done

# Setup IRQ affinity on r900
scp -i $ssh_key $server_dev_file $irq_affinity_script root@${server[0]}:.
ssh -i $ssh_key -l root ${server[0]} "./irq-affinity.pl $server_dev_num linux-devs.txt $client_per_dev"

# Setup ARP tables on clients
$pushf_command $arp_setup_script .
$pushf_command $arp_config_file .

client_index=0;
while [ $client_index -lt $client_num ]
  do
  c=${client[$client_index]}
  printf "ARP on %s\n" $c
  ssh -i $ssh_key -l root $c "./$arp_setup_script $arp_config_file"
  client_index=$(( $client_index + 1 ))
done

#dev_index=0
#client_index=0
#while [ $dev_index -lt $server_dev_num ]
#  do
#  client_it=0
#  while [ $client_it -lt $client_per_dev ]
#    do
#    m=${mac[$dev_index]}
#    s=${server[$dev_index]}
#    c=${client[$client_index]}
#    printf "ARP on %s: %s -> %s\n" $c $m $s
#    ssh -i $ssh_key -l root $c "arp -d $s; arp -s $s $m"
#    client_it=$(( $client_it + 1 ))
#    client_index=$(( $client_index + 1 ))
#  done
#  dev_index=$(( $dev_index + 1 ))
#done
