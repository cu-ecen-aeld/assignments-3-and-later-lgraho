#!/bin/bash

if [ $# -ne 2 ]
then
    echo Invalid number of arguments
    exit 1
fi

if [ ! -d $1 ]
then
    echo $1 is not a directory!
    exit 1
fi

filesdir=$1
searchstr=$2

numfiles=$( find $filesdir -type f | wc -l )
nummatches=$( grep -Ro $searchstr $filesdir | wc -l)

echo The number of files are ${numfiles} and the number of matching lines are ${nummatches}