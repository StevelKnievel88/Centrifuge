#!/bin/sh
set -eu
pack="$1"
assets=$(dirname "$0")
[ "$(wc -c < "$pack")" -eq 17666908 ]
[ "$(dd if="$pack" bs=44 count=1582 2>/dev/null | md5sum | cut -d ' ' -f 1)" = 1160eaec6cbb0e6e13ea22232a4b2edf ]
[ "$(wc -c < "$assets/splash.rgb565")" -eq 768000 ]
[ "$(wc -c < "$assets/logo.rgb565")" -eq 103040 ]
dd if="$assets/splash.rgb565" of="$pack" bs=8 seek=1541365 conv=notrunc 2>/dev/null
dd if="$assets/logo.rgb565" of="$pack" bs=8 seek=1779173 conv=notrunc 2>/dev/null
