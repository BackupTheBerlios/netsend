#!/bin/bash -x

TESTFILE=$(mktemp /tmp/netsendXXXXXX)
NETSEND_BIN=./netsend

# generate a file for transfer
echo Generate testfile
dd if=/dev/zero of=${TESTFILE} bs=4k count=10000

${NETSEND_BIN} tcp receive &
RPID=$1

# give netsend 2 seconds to set up everything and
# bind properly
sleep 2

${NETSEND_BIN} tcp transmit ${TESTFILE} localhost &

# wait for receiver and check return code
wait $RPID
if [ $? -ne 0] ; then
  echo testcase 1 failed
  exit 1
fi

echo Delete testfile
rm -f ${TESTFILE}
