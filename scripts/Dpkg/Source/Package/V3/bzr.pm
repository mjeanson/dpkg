#
# bzr support for dpkg-source
#
# Copyright © 2007 Colin Watson <cjwatson@debian.org>.
# Based on Dpkg::Source::Package::V3_0::git, which is:
# Copyright © 2007 Joey Hess <joeyh@debian.org>.
# Copyright © 2008 Frank Lichtenheld <djpig@debian.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

package Dpkg::Source::Package::V3::bzr;

use strict;
use warnings;

use base 'Dpkg::Source::Package';

use Cwd;
use File::Basename;
use File::Find;
use File::Temp qw(tempdir);

use Dpkg;
use Dpkg::Gettext;
use Dpkg::Compression;
use Dpkg::ErrorHandling;
use Dpkg::Source::Archive;
use Dpkg::Exit;
use Dpkg::Source::Functions qw(erasedir);

our $CURRENT_MINOR_VERSION = "0";

sub import {
    foreach my $dir (split(/:/, $ENV{PATH})) {
        if (-x "$dir/bzr") {
            return 1;
        }
    }
    error(_g("This source package can only be manipulated using bzr, which is not in the PATH."));
}

sub sanity_check {
    my $srcdir = shift;

    if (! -d "$srcdir/.bzr") {
        error(_g("source directory is not the top directory of a bzr repository (%s/.bzr not present), but Format bzr was specified"),
              $srcdir);
    }

    # Symlinks from .bzr to outside could cause unpack failures, or
    # point to files they shouldn't, so check for and don't allow.
    if (-l "$srcdir/.bzr") {
        error(_g("%s is a symlink"), "$srcdir/.bzr");
    }
    my $abs_srcdir = Cwd::abs_path($srcdir);
    find(sub {
        if (-l $_) {
            if (Cwd::abs_path(readlink($_)) !~ /^\Q$abs_srcdir\E(\/|$)/) {
                error(_g("%s is a symlink to outside %s"),
                      $File::Find::name, $srcdir);
            }
        }
    }, "$srcdir/.bzr");

    return 1;
}

sub can_build {
    my ($self, $dir) = @_;
    return (-d "$dir/.bzr", _g("doesn't contain a bzr repository"));
}

sub do_build {
    my ($self, $dir) = @_;
    my @argv = @{$self->{'options'}{'ARGV'}};
    # TODO: warn here?
    #my @tar_ignore = map { "--exclude=$_" } @{$self->{'options'}{'tar_ignore'}};
    my $diff_ignore_regexp = $self->{'options'}{'diff_ignore_regexp'};

    $dir =~ s{/+$}{}; # Strip trailing /
    my ($dirname, $updir) = fileparse($dir);

    if (scalar(@argv)) {
        usageerr(_g("-b takes only one parameter with format `%s'"),
                 $self->{'fields'}{'Format'});
    }

    my $sourcepackage = $self->{'fields'}{'Source'};
    my $basenamerev = $self->get_basename(1);
    my $basename = $self->get_basename();
    my $basedirname = $basename;
    $basedirname =~ s/_/-/;

    sanity_check($dir);

    my $old_cwd = getcwd();
    chdir($dir) ||
            syserr(_g("unable to chdir to `%s'"), $dir);

    # Check for uncommitted files.
    # To support dpkg-source -i, remove any ignored files from the
    # output of bzr status.
    open(BZR_STATUS, '-|', "bzr", "status") ||
            subprocerr("bzr status");
    my @files;
    while (<BZR_STATUS>) {
        chomp;
        next unless s/^ +//;
        if (! length $diff_ignore_regexp ||
            ! m/$diff_ignore_regexp/o) {
            push @files, $_;
        }
    }
    close(BZR_STATUS) || syserr(_g("bzr status exited nonzero"));
    if (@files) {
        error(_g("uncommitted, not-ignored changes in working directory: %s"),
              join(" ", @files));
    }

    chdir($old_cwd) ||
            syserr(_g("unable to chdir to `%s'"), $old_cwd);

    my $tmp = tempdir("$dirname.bzr.XXXXXX", DIR => $updir);
    push @Dpkg::Exit::handlers, sub { erasedir($tmp) };
    my $tardir = "$tmp/$dirname";

    system("bzr", "branch", $dir, $tardir);
    $? && subprocerr("bzr branch $dir $tardir");

    # Remove the working tree.
    system("bzr", "remove-tree", $tardir);

    # Some branch metadata files are unhelpful.
    unlink("$tardir/.bzr/branch/branch-name",
           "$tardir/.bzr/branch/parent");

    # Create the tar file
    my $debianfile = "$basenamerev.bzr.tar." . $self->{'options'}{'comp_ext'};
    info(_g("building %s in %s"),
         $sourcepackage, $debianfile);
    my $tar = Dpkg::Source::Archive->new(filename => $debianfile,
                                         compression => $self->{'options'}{'compression'},
                                         compression_level => $self->{'options'}{'comp_level'});
    $tar->create('chdir' => $tmp);
    $tar->add_directory($dirname);
    $tar->finish();

    erasedir($tmp);
    pop @Dpkg::Exit::handlers;

    $self->add_file($debianfile);
}

# Called after a tarball is unpacked, to check out the working copy.
sub do_extract {
    my ($self, $newdirectory) = @_;
    my $fields = $self->{'fields'};

    my $dscdir = $self->{'basedir'};

    my $basename = $self->get_basename();
    my $basenamerev = $self->get_basename(1);

    my @files = $self->get_files();
    if (@files > 1) {
        error(_g("format v3.0 uses only one source file"));
    }
    my $tarfile = $files[0];
    if ($tarfile !~ /^\Q$basenamerev\E\.bzr\.tar\.$comp_regex$/) {
        error(_g("expected %s, got %s"),
              "$basenamerev.bzr.tar.$comp_regex", $tarfile);
    }

    erasedir($newdirectory);

    # Extract main tarball
    info(_g("unpacking %s"), $tarfile);
    my $tar = Dpkg::Source::Archive->new(filename => "$dscdir$tarfile");
    $tar->extract($newdirectory);

    sanity_check($newdirectory);

    my $old_cwd = getcwd();
    chdir($newdirectory) ||
            syserr(_g("unable to chdir to `%s'"), $newdirectory);

    # Reconstitute the working tree.
    system("bzr", "checkout");

    chdir($old_cwd) ||
            syserr(_g("unable to chdir to `%s'"), $old_cwd);
}

1;
