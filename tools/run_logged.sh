#!/usr/bin/env bash
# Runs a command; on failure, publishes the end of its output as a GitHub annotation (readable via the API).
set -o pipefail
log=$(mktemp)
"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
if [ "$status" -ne 0 ]; then
  msg=$(grep -iE "error|exception|traceback|fatal|failed" "$log" | tail -25; echo "----- last lines -----"; tail -25 "$log")
  msg=$(printf '%s' "$msg" | sed 's/%/%25/g; s/\r//g' | awk '{printf "%s%%0A", $0}')
  echo "::error title=$1 failed::$msg"
fi
exit $status
