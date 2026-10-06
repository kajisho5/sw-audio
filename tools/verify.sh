#!/bin/bash
# One product's verification: ASan/UBSan unit tests + validate_all (unit tests + both validators on every plugin).
# usage: tools/verify.sh [CODE]   prints the summary and only the validator lines that are not clean (plus CODE's own)
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
tools/asan.sh
tools/validate_all.sh > /tmp/validate.log 2>&1
if grep -q 'BUILD FAILED' /tmp/validate.log; then cat /tmp/validate.log | head -12; exit 1; fi
head -2 /tmp/validate.log | grep -E "unit|FAILED"
echo "plugins: $(grep -c 'CLAP:' /tmp/validate.log), CLAP failed>0: $(grep 'CLAP:' /tmp/validate.log | grep -vc ' 0 failed')), VST3 failed>0: $(grep 'VST3:' /tmp/validate.log | grep -vc ' 0 tests failed')"
[ -n "$1" ] && grep -i "SW $1 " /tmp/validate.log
grep -E "warnings" /tmp/validate.log | grep -v " 0 warnings" | cut -c1-60 | head -3
grep 'CLAP:' /tmp/validate.log | grep -v ' 0 failed' | grep -q . && { echo 'VALIDATOR FAILURE'; grep 'CLAP:' /tmp/validate.log | grep -v ' 0 failed'; exit 1; }
grep 'VST3:' /tmp/validate.log | grep -v ' 0 tests failed' | grep -q . && { echo 'VST3 FAILURE'; exit 1; }
exit 0
