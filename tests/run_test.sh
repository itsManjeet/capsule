#!/bin/bash

SRCLANG=${SRCLANG:-'./build/srclang'}
${SRCLANG} ${1} > ${1}.test
if [[ $? == 0 ]] ; then
    outfile=${1}.out
else
    outfile=${1}.err
fi

diff -u "${1}.test" "${outfile}"
if [[ $? != 0 ]] ; then
    rm ${1}.test
    exit 1
fi
rm ${1}.test
