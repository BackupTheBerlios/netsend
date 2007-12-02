#!/bin/sh 

TESTFILE=$(mktemp /tmp/netsendXXXXXX)
NETSEND_BIN=./netsend


pre()
{
  # generate a file for transfer
  echo Generate testfile
  dd if=/dev/zero of=${TESTFILE} bs=4k count=10000 1>/dev/null 2>&1
}

case1()
{

  echo -n testcase 1 ...

  ${NETSEND_BIN} tcp receive &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} tcp transmit ${TESTFILE} localhost &

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo testcase failed
    exit 1
  fi

  echo passed
}


post()
{
  echo Delete testfile
  rm -f ${TESTFILE}
}


echo "\nnetsend unit test script - (C) 2007\n"

pre
trap post INT
case1
post


