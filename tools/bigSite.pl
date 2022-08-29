#!/opt/local/bin/perl -w

# usage: takess 1 argument, the trace file to analyze.
# Returns:
# <transition> <total time spent in transition> <average time per instance>


use warnings;

use Scalar::Util qw(looks_like_number);

sub tab($$) {
    my $file = shift;
    my $num = shift;

    for(my $i = 0; $i < $num; ++$i) {
        printf($file "    ");
    }
}

open(IN,$ARGV[0]) or die 'coud not open';

while(defined($line = <IN>)) {
    chop($line);
    if ($line =~ /TRACE/) {
        @ws = split(/ +/, $line);
        my $from = $ws[1];
        my $to = $ws[3];
        my $dur = $ws[4];

        my $siteStr=$from."-".$to;
        $siteDurs{$siteStr} += $dur;
        $siteCnt{$siteStr} += 1;
    }
}

close(IN);

foreach $f (keys %siteDurs) {
    printf("$f %.1f avg:%.1f\n", $siteDurs{$f},
           $siteDurs{$f}/$siteCnt{$f});
}


