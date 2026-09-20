#!/bin/sh
# Wait until xfel sees BROM FEL. Silent except the done line.
set -eu
export PATH="${HOME}/.local/bin:${PATH}"
XFEL=${XFEL:-xfel}

while true; do
  if "$XFEL" version 2>/dev/null | grep -q AWUSBFEX; then
    printf '%s\n' 'DONE: FEL present'
    exit 0
  fi
  sleep 2
done
