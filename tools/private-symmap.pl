#!/usr/bin/perl

while ($line=<STDIN>) {
	($addr, $ty, $name) = split ' ', $line;
	if ($name && ($ty eq "T")) {
		$new_name = "private_" . $name;
		print $name . " " . $new_name . "\n";
	} 
}

