#!/bin/sh
# Install man page symlinks: hexagonrpcd-<domain>.8 -> hexagonrpcd.8
mandir="$1"
for domain in adsp cdsp sdsp; do
  ln -sf hexagonrpcd.8 "${mandir}/hexagonrpcd-${domain}.8"
done
