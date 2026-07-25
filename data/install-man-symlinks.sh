#!/bin/sh
# Install man page symlinks: hexagonrpcd-<domain>.8 -> hexagonrpcd.8

mandir="$1"
[ -n "$DESTDIR" ] && mandir="${DESTDIR}${mandir}"

for domain in adsp adsp-audiopd adsp-sensorspd cdsp sdsp; do
  ln -sf hexagonrpcd.8 "${mandir}/hexagonrpcd-${domain}.8"
done
