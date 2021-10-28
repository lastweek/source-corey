#!/bin/bash
if [ $# -ne 2 ]
then
echo "usage: httpd_test.sh <http_server_name | http_server_ip> http_server_port"
exit
fi
killall -9 sync_server
client_num=2
running_time=10

client[0]="172.23.164.243"
client[1]="172.23.166.118"

working_directory="~/httpc"

./sync_server $client_num $running_time &

client_index=0
while [ $client_index -lt $client_num ]
do
ssh -l root ${client[$client_index]} "killall -9 httpc"
ssh -l root ${client[$client_index]} "mkdir -p $working_directory"
scp ./httpc root@${client[$client_index]}:$working_directory/
scp ./httpc.conf root@${client[$client_index]}:$working_directory/
ssh -l root ${client[$client_index]} "cd ${working_directory} && ./httpc $1 $2 -f httpc.conf > ./log 2>&1" &
client_index=$[client_index+1]
done
sleep $running_time

