#!/bin/sh

log=XXX
pwd=XXX
ip=XXX

ipmicli=tools/ipmicli.x86_64
restartfile=tools/ipmicli_restart.batch

$ipmicli $ip $log $pwd $restartfile
