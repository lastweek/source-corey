#!/usr/bin/perl

system("rm -f ips.txt; wget 172.23.64.35/ips.txt &> /dev/null") == 0 || die "system error $?";

open(F, "ips.txt");
while($line = <F>) {
    if ($line =~ /.*bench.*\:(\d+\.\d+\.\d+\.\d+)/) {
	print "$1\n";
    }
}
