#!/bin/bash

TESTPGM="python sling/myelin/tests/myelin-test.py"

testtype() {
  DT=$1
  $TESTPGM --dt $DT
  $TESTPGM --dt $DT --cpu=-avx512
  $TESTPGM --dt $DT --cpu=-avx512-avx2
  $TESTPGM --dt $DT --cpu=-avx512-fma3
  $TESTPGM --dt $DT --cpu=-avx512-fma3-avx2
  $TESTPGM --dt $DT --cpu=-avx512-fma3-avx2-avx
}

set -e

testtype float32
testtype float64
testtype int8
testtype int16
testtype int32
testtype int64

echo "==== ALL TESTS PASSED ====="

