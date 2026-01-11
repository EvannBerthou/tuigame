#!/usr/bin/env bash

LINES=1000000
WORDS_PER_LINE=5
WORD_LENGTH=6

for ((i=1; i<=LINES; i++)); do
    words=""
    for ((w=1; w<=WORDS_PER_LINE; w++)); do
        word=$(tr -dc 'a-z' </dev/urandom | head -c "$WORD_LENGTH")
        words+="$word "
    done
    echo "$i ${words% }"
done

