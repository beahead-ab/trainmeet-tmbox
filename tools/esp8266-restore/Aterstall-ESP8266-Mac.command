#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMBOX_BACKUP_EXIT=1
TMBOX_PYTHON_FOUND=0
for TMBOX_PYTHON in python3 python; do
  if command -v "$TMBOX_PYTHON" >/dev/null 2>&1 && "$TMBOX_PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' >/dev/null 2>&1; then
    TMBOX_PYTHON_FOUND=1
    "$TMBOX_PYTHON" "$SCRIPT_DIR/restore_esp8266.py" "$@"
    TMBOX_BACKUP_EXIT=$?
    break
  fi
done
if [ "$TMBOX_PYTHON_FOUND" = 0 ]; then
  printf '\nPython 3.10 eller senare behövs. Installera Python från python.org och försök igen.\nLäs LAS-MIG.txt i samma mapp.\n'
fi
if [ "${TMBOX_BACKUP_NO_PAUSE:-0}" != 1 ] && [ -t 0 ]; then
  printf '\nTryck Enter för att stänga fönstret.'
  read -r _
fi
exit "$TMBOX_BACKUP_EXIT"
