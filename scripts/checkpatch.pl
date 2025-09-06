#!/usr/bin/env perl
# Wrapper script for Linux kernel checkpatch.pl
# Downloads and uses the official kernel checkpatch tool

use strict;
use warnings;
use File::Basename;
use File::Path qw(make_path);
use LWP::Simple;

my $script_dir = dirname(__FILE__);
my $checkpatch_path = "$script_dir/kernel-checkpatch.pl";
my $checkpatch_url = "https://raw.githubusercontent.com/torvalds/linux/master/scripts/checkpatch.pl";

# Download checkpatch.pl if it doesn't exist
if (!-f $checkpatch_path) {
    print "Downloading Linux kernel checkpatch.pl...\n";
    my $content = get($checkpatch_url);
    if (!$content) {
        die "Failed to download checkpatch.pl from $checkpatch_url\n";
    }
    
    open(my $fh, '>', $checkpatch_path) or die "Cannot write $checkpatch_path: $!\n";
    print $fh $content;
    close($fh);
    chmod 0755, $checkpatch_path;
    print "Downloaded checkpatch.pl successfully\n";
}

# Execute checkpatch.pl with provided arguments
exec($^X, $checkpatch_path, @ARGV) or die "Failed to execute checkpatch.pl: $!\n";