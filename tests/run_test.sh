#!/bin/bash

SRCLANG=${1}
TEST_FILE=${2}
${SRCLANG} ${TEST_FILE} > ${TEST_FILE}.test
if [[ $? == 0 ]] ; then
    outfile=${TEST_FILE}.out
else
    outfile=${TEST_FILE}.err
fi

if [[ ! -e ${outfile} ]] ; then
    cp ${TEST_FILE}.test ${outfile}
fi

diff -u "${TEST_FILE}.test" "${outfile}"
if [[ $? != 0 ]] ; then
    rm ${TEST_FILE}.test
    exit 1
fi
rm ${TEST_FILE}.test
