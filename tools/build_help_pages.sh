#!/bin/sh

rm --force include/commands.h

for cmd in assets/help/*; do
    cmd_name=$(basename $cmd)
    xxd -p -c0 $cmd | sed 's/../\\x&/g' | sed "s/^/const char ${cmd_name}_help[] = \"/; s/$/\";/" >> include/commands.h
done
