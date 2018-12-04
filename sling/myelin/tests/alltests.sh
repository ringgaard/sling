#!/bin/bash

TESTPGM="python sling/myelin/tests/myelin-test.py"

testtype() {
  DT=$1
  echo "Test data type $DT"
  $TESTPGM --dt $DT
  echo "Test data type $DT without AVX512"
  $TESTPGM --dt $DT --cpu=-avx512
  echo "Test data type $DT without AVX2"
  $TESTPGM --dt $DT --cpu=-avx512-avx2
  echo "Test data type $DT without FMA3"
  $TESTPGM --dt $DT --cpu=-avx512-fma3
  echo "Test data type $DT without AVX2 and FMA3"
  $TESTPGM --dt $DT --cpu=-avx512-fma3-avx2
  echo "Test data type $DT without AVX"
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

