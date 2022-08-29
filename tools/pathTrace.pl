#!/opt/local/bin/perl -w

use warnings;

use Scalar::Util qw(looks_like_number);
use Getopt::Std;


sub tab($$) {
    my $file = shift;
    my $num = shift;

    for(my $i = 0; $i < $num; ++$i) {
        printf($file "    ");
    }
}

sub VERSION_MESSAGE() {}
    
sub HELP_MESSAGE() {
    print("-f <file> : input trace file from A-PMPI (mandatory)\n");
    print("-p        : print pattern\n");
    print("-g        : print Graphviz graph\n");
    print("-c        : print C++ motif skeleton\n");
    print("-C <file> : filename for C++ skeleton\n");
    exit(1);
}

# 'main'

getopts("cpgf:C:", \%opt) or usage();
if (!defined($opt{f})) {
    HELP_MESSAGE();
}

open(IN,$opt{f}) or die 'coud not open';

$commIter = 0;

while(defined($line = <IN>)) {
    chop($line);
    if ($line =~ /TRACE/) {
        @ws = split(/ +/, $line);
        my $from = $ws[1];
        my $to = $ws[3];
        my $dur = $ws[4];
        my $call = $ws[5];
        my $type = -1;
        my $size = -1;
        my $comm = "";
        my $isVar = 0;
        my $varSize = 0;
        if (defined($ws[6])) {
            $type = $ws[6];
            if ($type eq "coll") {
                $size = $ws[8];
                my $commStr = join(':',@ws[10..(scalar(@ws)-1)]);
                if ($commStr =~ /[<>]/) {
                    $isVar = 1;
                    my $tmp = $commStr;
                    $tmp =~ s/[0-9]+[<>]//; # strip first rank numberr
                    $tmp =~ s/:[0-9]+//g;  # strip rank numbers
                    $tmp =~ s/[<>]/+/g; # turn < or > in to '+'
                    #printf("$commStr $tmp %d\n", eval($tmp.";"));
                    $varSize = eval($tmp.";");
                    if (!defined($varMin{$from}{$to})) {
                        $varMin{$from}{$to} = 9999999;
                        $varMax{$from}{$to} = 0;
                    }
                    $varMin{$from}{$to} = ($varSize < $varMin{$from}{$to}) ? $varSize : $varMin{$from}{$to};
                    $varMax{$from}{$to} = ($varSize > $varMax{$from}{$to}) ? $varSize : $varMax{$from}{$to};                     
                    #printf("X %d %d %d\n", $from, $to, $varSize);
                    $commStr =~ s/[<>][0-9]+//g;
                }
                #printf($line."\n comm: %s\n", $commStr);
                $comm = $commStr;
                if(!defined($commSet{$commStr})) {
                    # new communicator (or, at least, new list of ranks)
                    $commSet{$commStr} = $commIter."(sz".(scalar(@ws)-10).")";
                    $commIter++;
                } 
            } elsif ($type eq "p2p") {
                $size = $ws[7];
            } else {
                printf("Type error? %s\n", $line);
            }
        } 

        # node label
        $node{$to} = "$to\\n".$call;
        if ($size != -1) {
            if ($isVar) {
                $node{$to} .= ":".$varSize."(v)";
                if ($varMin{$from}{$to} != $varMax{$from}{$to}) {
                    $node{$to} .= "\\n".$varMin{$from}{$to}."-"
                        .$varMax{$from}{$to};
                } 
            } else {
                $node{$to} .= ":".$size;
            }
        }
        if ($comm ne "") {
            $node{$to} .= "\\ncomm".$commSet{$comm};
        }

        # c++ node text
        my $emberCall = $call;
        $emberCall =~ s/MPI_/enQ_/;
        if ($comm ne "") {
            my $cc_comm = $commSet{$comm};
            $cc_comm =~ s/\(.*//;
            $cc_node{$to} = "//".$emberCall."(evQ,comm".$cc_comm.");";
        } else {
            $cc_node{$to} = "//".$emberCall."\\n";
        }

        
        #checking
        if (!looks_like_number($from) || !looks_like_number($to)) {
            printf("Possible Error: $line \n");
        } else {            
            #printf("from %d to %d time:%.1f\n", $from, $to, $ws[4]);
            if (!defined($count{$from}{$to})) {
                $count{$from}{$to} = 1;
                $tDur{$from}{$to} = $dur;
                $last{$from} = $to;
                $lastCount{$from} = 1;
                $transitions{$from} = "${to}:";
            } else {
                $count{$from}{$to}++;
                $tDur{$from}{$to} += $dur;
                if($last{$from} == $to) {
                    # if it is the same transition
                    $lastCount{$from}++;
                } else {
                    # different transition, so write out the last
                    $transitions{$from} .= $lastCount{$from} . " ";
                    #start new count
                    $last{$from} = $to;
                    $lastCount{$from} = 1;
                    $transitions{$from} .= "${to}:";
                }
            }
        }
    }
}

close(IN);


#fix up
foreach $f (keys %last) {
    $transitions{$f} .= $lastCount{$f};
}

#output
if (defined($opt{p})) {
    # branch pattern
    foreach $f (keys %last) {
        printf("$f %s\n", $transitions{$f});
    }
}

if (defined($opt{g})) {
    # graph
    printf("digraph G {\n");
    printf("rankdir=\"LR\"\n");
    foreach $n (keys %node) {
        printf("\t$n [label=\"%s\"];\n", $node{$n});
    }
    foreach $f (keys %last) {
        foreach $t (keys %{$count{$f}}) {
            printf("\t%d -> %d [taillabel=\"%d\" label=\"%.0f\"]\n", $f, $t, $count{$f}{$t}, $tDur{$f}{$t}/$count{$f}{$t});
        }
    }
    printf("}\n");
}

if (defined($opt{c})) {
    #output c++ skeleton
    if (defined($opt{C})) {
        $filename = $opt{C};
    } else {
        $filename = $opt{f};
        $filename =~ s/\..*$/.cc/;
    }
    
    open(OUT,">", $filename) or die 'coud not open';

    tab(OUT,1); printf(OUT "bool done = 0;\n\n");
    
    tab(OUT,1); printf(OUT "switch(state) {\n");

    #foreach MPI callsite
    foreach my $n (sort {$a <=> $b} keys %node) {
        tab(OUT,1); printf(OUT "case $n:\n");
        tab(OUT,2); printf(OUT "{\n");

        # MPI call
        tab(OUT,3); printf(OUT "%s\n\n", $cc_node{$n});


        my @nexts = sort {$a <=> $b} keys %{$count{$n}};
        # compute - for now we assume the first tranition is the
        # compute duration
        if (defined($nexts[0]) && $count{$n}{$nexts[0]}) {
            tab(OUT,3);
            printf(OUT "enQ_compute(evQ,%d);\n\n",
                   $tDur{$n}{$nexts[0]}/$count{$n}{$nexts[0]});
        } else {
            tab(OUT,3); printf(OUT "enQ_compute(evQ,1); //must enque something or it assumes we're done\n\n");
            tab(OUT,3); printf(OUT "// done?\n");
            tab(OUT,3); printf(OUT "done = 1;\n");
        }

        # transition
        if (scalar(@nexts) == 1) {
            tab(OUT,3);
            printf(OUT "state = %d;\n", $nexts[0]);
        } else {
            tab(OUT,3);
            foreach my $next (@nexts){
                printf(OUT "//state = %d;\n", $next);
            }
        }
        
        tab(OUT,2); printf(OUT "}\n");
        tab(OUT,2); printf(OUT "break;\n");
    }
    
    # finish switch statement
    tab(OUT,1);printf(OUT "}\n\n");

    tab(OUT,1);printf(OUT "// signal if we are done\n");
    tab(OUT,1);printf(OUT "if (done) {\n");
    tab(OUT,2);printf(OUT "return true;\n");
    tab(OUT,1);printf(OUT "} else {\n");
    tab(OUT,2);printf(OUT "return false;\n");
    tab(OUT,1);printf(OUT "}\n");
    
    close(OUT);
}
