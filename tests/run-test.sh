#!/bin/bash

SRCLANG=$1
TEST_FILE=$2

SUCCESS=0
if [[ -e "${TEST_FILE}.out" ]] ; then
  SUCCESS=0
elif [[ -e "${TEST_FILE}.err" ]] ; then
  SUCCESS=2
else
  echo "ERROR: no output file present $TEST_FILE.out"
  exit 1
fi

TEMP_FILE=/tmp/srclang.out

$SRCLANG $TEST_FILE  &> $TEMP_FILE 2>&1
out=$?
if [[ $out -ne $SUCCESS ]] ; then
    echo "Test Failed $out != $SUCCESS"
    cat $TEMP_FILE
    exit 1
fi

TEST_OUTPUT_FILE=$TEST_FILE.out
if [[ $SUCCESS -ne 0 ]] ; then
  TEST_OUTPUT_FILE=$TEST_FILE.err
  tail -n +2 $TEMP_FILE > $TEMP_FILE.tmp && mv $TEMP_FILE.tmp $TEMP_FILE
#  head -n -1 $TEMP_FILE > $TEMP_FILE.tmp && mv $TEMP_FILE.tmp $TEMP_FILE
fi


cmp $TEMP_FILE $TEST_OUTPUT_FILE || {
  echo "Test failed, output not identical"
  echo "Expected Output"
  cat $TEST_OUTPUT_FILE

  echo ""
  echo "Actual Output"
  cat $TEMP_FILE
  exit 1
}