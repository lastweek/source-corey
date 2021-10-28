#!/usr/bin/perl

# static options
$amd_mode = 0;

if (@ARGV != 3) {
    print "usage: irq-affinity.pl num-dev dev-file cores-per-irq\n";
    exit 1;
}

open(FP, "cat /proc/cpuinfo | grep \"processor\" |");
@procs = <FP>;
close(FP);
$max_cores = @procs;
$intel_core_stride = $max_cores / $ARGV[2];

open(F0, $ARGV[1]);

$count = 0;
$assigned = 0;
while (($eth = <F0>) && ($count < $ARGV[0])) {
    chomp($eth);
    $found = 0;
    open(F1, "find /proc/irq -name $eth |");
    while ($line = <F1>) {
	chomp($line);
	$line =~ s/$eth/smp_affinity/;
	$found = 1;
	$mask = 0;

	if ($amd_mode) {
	    for ($k = 0; $k < $ARGV[2]; $k++) {
		$add = 1 << $assigned;
		$mask |= $add;
		$assigned++;
	    }
	} else {
	    $stride_shift = $assigned;
	    for ($k = 0; $k < $ARGV[2]; $k++) {
		$add = 1 << $stride_shift;
		$mask |= $add;
		$stride_shift += $intel_core_stride;
	    }
	    $assigned++;
	}

	$hex_mask = sprintf("%x", $mask);
	print "$eth: $line -> " . $hex_mask . "\n"; 
	system("echo $hex_mask > $line");
    }
    close(F1);
    $found || die "could not find $eth";
    $count++;
}

close(F0);
