#!/bin/bash

# Copyright (C) 2017 Centro de Computacao Cientifica e Software Livre
# Departamento de Informatica - Universidade Federal do Parana - C3SL/UFPR
#
# This file is part of le-multiterminal
#
# le-multiterminal is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
# USA.

#### Written by: Thiago Abdo - tja14@c3sl.ufpr.br on 2017.

for i in /dev/input/*; do
    if test -c $i; then
        if udevadm info $i | grep -qw ID_INPUT_KEYBOARD; then
            echo $i
        fi
    fi
done
