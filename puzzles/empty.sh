#!/bin/sh
echo $1
for i in `seq $1`; do
    for j in `seq $1`; do
        printf '. '
    done
    echo
done
