#!/bin/bash

declare -a server

# static options for local machine
ssh_key=~/keys/root-r900-id_rsa
client_ip_command=./client-list.pl
sync_server_command=../../obj/test/sync_server

# static options for remote machine
httpc_command="~/josmp-all/trunk/obj/test/httpc"
working_dir="~/httpc"
threads="2"
sockets="15"

# static options for results machine
results_ip="172.23.64.35"
results_path="/usr/local/www/apache22/data/sync_server"

if [ $# -ne "6" ]
    then
    echo "usage: $0 server-num client-per-server running-time"
    echo -e "\t\t\tport-base port-increment server-ip-file"
    echo " server-num : number of servers to connect to"
    echo " client-per-server : number of clients per server"
    echo " running-time: length of time to run"
    echo " port-base : start port for server ip"
    echo " port-increment : port increment per server ip"
    echo " server-ip-file : list of server ip addresses, separated by newlines"
    exit 1
fi

server_num=$1
client_per_server=$2
running_time=$3
port_base=$4
port_inc=$5
server_ip_file=$6

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

killall -s SIGKILL sync_server
$sync_server_command $(( $server_num * $client_per_server)) $running_time &

ssh -i $ssh_key -l root $results_ip "rm -f $results_path/*"

server_index=0
client_index=0
while [ $server_index -lt $server_num ]
  do
  port=$port_base
  client_it=0
  while [ $client_it -lt $client_per_server ]
    do
    s=${server[$server_index]}
    c=${client[$client_index]}

    printf "setting up: %s -> %s:%u\n" $c $s $port
    ssh -i $ssh_key -l root $c "killall -s SIGKILL httpc; rm -rf $working_dir; mkdir $working_dir"
    scp -i $ssh_key httpc.conf root@$c:$working_dir/
    #ssh -i $ssh_key -l root $c "$httpc_command $s $port -c $threads -s $sockets -q -r -f $working_dir/httpc.conf &> $working_dir/\`hostname\`; scp $working_dir/\`hostname\` $results_ip:$results_path/" &
    ssh -i $ssh_key -l root $c "$httpc_command $s $port -c $threads -s $sockets -f $working_dir/httpc.conf &> $working_dir/\`hostname\`; scp $working_dir/\`hostname\` $results_ip:$results_path/" &

    port=$(( $port + $port_inc ))
    client_it=$(( $client_it + 1 ))
    client_index=$(( $client_index + 1 ))
  done
  server_index=$(( $server_index + 1 ))
done
