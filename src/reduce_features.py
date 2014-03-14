#!/usr/bin/python

# Copyright (C) (2009) (Benoit Favre) <favre@icsi.berkeley.edu>
#
# This program is free software; you can redistribute it and/or 
# modify it under the terms of the GNU Lesser General Public License 
# as published by the Free Software Foundation; either 
# version 2 of the License, or (at your option) any later 
# version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

import sys

if len(sys.argv) != 3:
    sys.stderr.write('USAGE: %s <freq_cutoff> <file>\n' % sys.argv[0])
    sys.exit(1)

freq = int(sys.argv[1])
count = {}
input = None
if sys.argv[2].endswith(".gz"):
    import gzip
    input = gzip.open(sys.argv[2], 'r')
else:
    input = open(sys.argv[2], 'r')

num = 0
for line in input:
    tokens = line.strip().split()
    for token in tokens[1:]:
        if token not in count: count[token] = 1
        else: count[token] += 1
    if num % 1000 == 0:
        sys.stderr.write('\rcounting: %d' % num)
    num += 1
sys.stderr.write('\rcounting: %d\n' % num)

input.seek(0)
num = 0

for line in input:
    tokens = line.strip().split()
    sys.stdout.write(tokens[0])
    for token in tokens[1:]:
        if count[token] > 3:
            sys.stdout.write(' ' + token)
    sys.stdout.write('\n')
    if num % 1000 == 0:
        sys.stderr.write('\rfiltering: %d' % num)
    num += 1
sys.stderr.write('\rfiltering: %d\n' % num)
