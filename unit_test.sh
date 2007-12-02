#!/bin/sh

TESTFILE=$(mktemp /tmp/netsendXXXXXX)
NETSEND_BIN=./netsend

pre()
{
  # generate a file for transfer
  echo Initialize test environment
  dd if=/dev/zero of=${TESTFILE} bs=4k count=10000 1>/dev/null 2>&1
}

post()
{
  echo Cleanup test environment
  rm -f ${TESTFILE}
}

die()
{
  post
  exit 1
}

case1()
{
  echo -n "Trivial receive transmit test ..."

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
    die
  fi

  echo passed
}

case2()
{
  echo -n "IPv4 (AF_INET) enforce test ..."

  ${NETSEND_BIN} -4 tcp receive 1>/dev/null 2>&1 &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} -4 tcp transmit ${TESTFILE} localhost 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  echo passed
}

case3()
{
  echo -n "IPv6 (AF_INET6) enforce test ..."

  ${NETSEND_BIN} -6 tcp receive 1>/dev/null 2>&1 &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} -6 tcp transmit ${TESTFILE} localhost 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  echo passed
}

case4()
{
  echo -n "Statictic output test (both, machine and human) ..."

  # start with human output

  R_OPT="-T human tcp receive"
  T_OPT="-T human tcp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  # and machine output

  R_OPT="-T machine tcp receive"
  T_OPT="-T machine tcp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo testcase failed
    die
  fi

  echo passed
}



echo "\nnetsend unit test script - (C) 2007\n"

pre
trap post INT
case1
case2
case3
case4
post


