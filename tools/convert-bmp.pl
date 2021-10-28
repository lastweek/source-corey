#!/usr/bin/perl 

use strict;

my @hdr = qw(
    bfType
    bfSize
    bfReserved1
    bfReserved2
    bfOffBits
    biSize
    biWidth
    biHeight
    biPlanes
    biBitCount
    biCompression
    biSizeImage
    biXPelsPerMeter
    biYPelsPerMeter
    biClrUsed
    biClrImportant
);

# input bmp file
my $BMPFILE = $ARGV[0]; 
# output data file
my $OUTFILE = $ARGV[1];

open BMP, $BMPFILE or die $!;
binmode BMP;

my $hdr_data ;
read BMP, $hdr_data, 54 ; 

my @hdr_dat = unpack "SLSSLLLLSSLLLLLL", $hdr_data;
my %header;
@header{@hdr}=@hdr_dat;
print "$_\t$header{$_}\n" for @hdr;

#check if it is a valid bmp file.
if ($header{bfType} != 19778 ||  $header{biBitCount} != 24 ) {
	print "invalid bitmap file\n";
#	die $!;
}

open OUT, ">$OUTFILE" or die $!;
binmode OUT;
print OUT "const char hist_datafile[] = \"" ;

my $buffer ;

while (
    read (BMP, $buffer, 65536)     # read in (up to) 64k chunks, write
    and print OUT $buffer      # exit if read or write fails
  ) {};

print OUT "\";\n" ;
close OUT;
close BMP;
