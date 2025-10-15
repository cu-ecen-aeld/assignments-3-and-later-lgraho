#!/bin/bash

if [ $# -ne 2 ]
then
    echo Invalid number of arguments
    exit 1
fi

writefile=$1
writestr=$2

mkdir -p "$(dirname "$1")"
touch $writefile

if [ ! $? -eq 0 ]
then
    echo File could not be created
    exit 1
fi

echo $writestr > $writefile