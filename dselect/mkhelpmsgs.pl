#!/usr/bin/perl

$maxnlines= 22;

open(SRC,$ARGV[0]) || die $!;
open(NC,">helpmsgs.cc.new") || die $!;
open(NH,">helpmsgs.h.new") || die $!;

&autowarn('NC'); &autowarn('NH');

print(NC "#include \"helpmsgs.h\"\n") || die $!;
print(NH <<'END') || die $!;
#ifndef HELPMSGS_H
#define HELPMSGS_H
struct helpmessage { const char *title; const char *text; };
END

$state= 'start';
$nblanks= 0; $nlines= 0;
while (<SRC>) {
    s/\"/\\\"/g;
    if (m/^\@\@\@ (\w+)\s+(\S.*\S)\s+$/) {
        &finishif;
        $currentname= $1; $currenttitle= $2;
        print(NH "extern const struct helpmessage hlp_$currentname;\n") || die $!;
        print(NC
              "const struct helpmessage hlp_$currentname = {\n".
              "  \"$currenttitle\", \"") || die $!;
    } elsif (m/^\@\@\@/) {
        die;
    } elsif (!m/\S/) {
        $nblanks++;
    } else {
        if ($state ne 'start' && $nblanks) {
            print(NC ("\\n"x$nblanks)."\\\n") || die $!;
            $nlines+= $nblanks;
        }
        $state= 'middle'; $nblanks= 0;
        s/\s*\n$//;
        print(NC "\\\n".$_."\\n") || die $!;
        $nlines++;
    }
}

&finishif;

close(NC) || die $!;
print(NH "#endif /* HELPMSGS_H */\n") || die $!;
close(NH) || die $!;

rename("helpmsgs.cc.new","helpmsgs.cc") || die $!;
rename("helpmsgs.h.new","helpmsgs.h") || die $!;
    
sub finishif {
    if ($state ne 'start') {
        print(NC "\"\n};\n") || die $!;
        printf "\t\t%s: %d lines\n",$currentname,$nlines;
        if ($nlines > $maxnlines) { warn "Too many lines in $currentname"; }
    }
    $state= 'start';
    $nblanks= 0; $nlines= 0;
}


sub autowarn {
    $fh= $_[0];
    print($fh <<'END') || die $!;
/*
 * WARNING - THIS FILE IS GENERATED AUTOMATICALLY - DO NOT EDIT
 * It is generated by mkhelpmsgs.pl from helpmsgs.src.
 */

END
}
