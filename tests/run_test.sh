#!/bin/bash

SRCLANG=${1}
TEST_FILE=${2}
${SRCLANG} -search-path="./modules/" "${TEST_FILE}" >"${TEST_FILE}.test" 2>&1
# shellcheck disable=SC2181
if [[ ${?} == 0 ]]; then
  outfile=${TEST_FILE}.out
else
  IS_ERROR=1
  outfile=${TEST_FILE}.err
fi
if [[ -n ${IS_ERROR} ]]; then
  sed -i '1d' "${TEST_FILE}.test"
fi
if [[ ! -e ${TEST_FILE}.err ]] && [[ ! -e ${TEST_FILE}.out ]]; then
  cp "${TEST_FILE}".test "${outfile}"
fi

if [[ ! -e ${outfile} ]]; then
  echo "TEST FAILED"
  rm "${TEST_FILE}.test"
  exit 1
fi

diff -u "${TEST_FILE}.test" "${outfile}"
# shellcheck disable=SC2181
if [[ $? != 0 ]]; then
  rm "${TEST_FILE}.test"
  exit 1
fi
rm "${TEST_FILE}.test"
