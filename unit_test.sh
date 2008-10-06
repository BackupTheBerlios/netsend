#!/bin/sh 

TESTFILE=$(mktemp /tmp/netsendXXXXXX)
NETSEND_BIN=./netsend
TEST_FAILED=0

pre()
{
  # generate a file for transfer
  echo Initialize test environment
  dd if=/dev/zero of=${TESTFILE} bs=1 count=1 1>/dev/null 2>&1
}

post()
{
  echo Cleanup test environment
  killall -9 netsend 1>/dev/null 2>&1
  rm -f ${TESTFILE}
}

die()
{
  post
  exit 1
}

further_help()
{
cat <<EOF
One or more testcases failed somewhere!
A series of causales are possible:
   o No kernel support for a specific protocol (e.g. tipc, ipv6, ...)
   o Architecture bugs (uncatched kernel, glibc or netsend bugs)
   o Last but not least: you triggered a real[tm] netsend error ;)

If the testcases doesn't affect you you can skip the test
But if you want to dig into this corner you should start this script
via the debug mode ("sh -x unit_test.sh"), get out the executed netsend command
and execute them manually on the commandline and understand the failure message!

For further information or help -> http://netsend.berlios.de
EOF
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
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi

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
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi

}

case3()
{
  echo -n "IPv6 (AF_INET6) enforce test ..."

  ${NETSEND_BIN} -6 tcp receive 1>/dev/null 2>&1 &
  RPID=$!

  # give netsend 2 seconds to set up everything and
  # bind properly
  sleep 2

  ${NETSEND_BIN} -6 tcp transmit ${TESTFILE} ::1 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    TEST_FAILED=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi

}

case4()
{
  echo -n "Statictic output test (both, machine and human) ..."

  L_ERR=0

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
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
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
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case5()
{
  echo -n "TCP protocol tests ..."

  L_ERR=0

  R_OPT="tcp receive"
  T_OPT="tcp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case6()
{
  echo -n "DCCP protocol tests ..."

  L_ERR=0

  R_OPT="dccp receive"
  T_OPT="dccp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case7()
{
  echo -n "SCTP protocol tests ..."

  L_ERR=0

  R_OPT="sctp receive"
  T_OPT="sctp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case8()
{
  echo -n "TIPC protocol tests ..."

  L_ERR=0

  R_OPT="tipc receive -t SOCK_STREAM"
  T_OPT="-u rw tipc transmit -t SOCK_STREAM ${TESTFILE}"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  # wait for receiver and check return code
  wait $RPID
  if [ $? -ne 0 ] ; then
    L_ERR=1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case9()
{
  echo -n "UDP protocol tests ..."

  L_ERR=0

  R_OPT="udp receive"
  T_OPT="udp transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  else
    # ok, the send process seems fine. The workaround
    # now comes through the fact the udp even doesn't
    # know when the end of data is reached.
    # We therefore kill simple the receiver ;(
    kill -9 $RPID 1>/dev/null 2>&1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

case10()
{
  echo -n "UDP Lite protocol tests ..."

  L_ERR=0

  R_OPT="udplite receive"
  T_OPT="udplite transmit ${TESTFILE} localhost"

  ${NETSEND_BIN} ${R_OPT} 1>/dev/null 2>&1 &
  RPID=$!

  sleep 2

  ${NETSEND_BIN} ${T_OPT} 1>/dev/null 2>&1
  if [ $? -ne 0 ] ; then
    L_ERR=1
  else
    # ok, the send process seems fine. The workaround
    # now comes through the fact the udp-lite even doesn't
    # know when the end of data is reached.
    # We therefore kill simple the receiver ;(
    kill -9 $RPID 1>/dev/null 2>&1
  fi

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}


test_af_local()
{
  echo -n "AF_LOCAL tests..."
  L_ERR=0
  R_OPT="unix receive"
  T_OPT="unix transmit"

  test -S /tmp/.netsend && rm -f /tmp/.netsend

  for sockt in SOCK_STREAM SOCK_SEQPACKET SOCK_DGRAM ; do
    echo -n "$sockt "
    ${NETSEND_BIN} ${R_OPT} $sockt 1>/dev/null 2>&1 &
    RPID=$!

    sleep 2

    ${NETSEND_BIN} ${T_OPT} $sockt ${TESTFILE} 1>/dev/null 2>&1
    if [ $? -ne 0 ] ; then
      L_ERR=1
    fi

    # wait for receiver and check return code
    wait $RPID
    if [ $? -ne 0 ] ; then
      L_ERR=1
    fi
  done

  if [ $L_ERR -ne 0 ] ; then
    echo failed
    TEST_FAILED=1
  else
    echo passed
  fi
}

echo -e "\nnetsend unit test script - (C) 2007\n"

pre

trap post INT

case1
case2
case3
case4
case5
case6
case7
case8
case9
case10
test_af_local

post

if [ $TEST_FAILED -ne 0 ] ; then
  further_help
fi



