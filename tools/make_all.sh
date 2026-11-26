#!/usr/bin/env bash
# Regenerate tunings/*.oft, tunings/test/broken.oft and source/builtins.h from scales/*.scl.
# File names are "<group>-<name>.oft": the pack shows each file in its group (README.md).
set -euo pipefail
T=$(cd "$(dirname "$0")/.." && pwd); S=$T/scales; O=$T/tunings
oft() { python3 -I -B "$T/tools/oft.py" "$@"; }
mkdir -p "$O/test"
rm -f "$O"/*.oft "$O/test"/*.oft
for m in rast bayati; do oft make "$O/maqam-$m.oft" "$S/$m.scl" chromatic12; done
for m in just pythagorean; do oft make "$O/pure-$m.oft" "$S/$m.scl" chromatic12; done
oft make "$O/equal-24 equal.oft" "$S/24equal.scl" linear
for g in mesem madu; do
  oft make "$O/gamelan-slendro $g.oft" "$S/slendro-$g.scl" white5
  oft make "$O/gamelan-slendro $g black.oft" "$S/slendro-$g.scl" black5
done
oft make "$O/gamelan-pelog mesem.oft" "$S/pelog-mesem.scl" white7
oft make "$O/gamelan-pelog siratmadu.oft" "$S/pelog-siratmadu.scl" white7
oft make "$O/gamelan-mesem.oft" --layer "$S/pelog-mesem.scl:white7" --layer "$S/slendro-mesem.scl:black5"
# jogja: Madukentir (slendro) and Siratmadu (pelog), a pair of the kraton Yogyakarta
oft make "$O/gamelan-jogja.oft" --layer "$S/pelog-siratmadu.scl:white7" --layer "$S/slendro-madu.scl:black5"
# broken.oft (no hyphen: shown under "user"): maqam-rast.oft with one ratio byte changed (CRC no longer matches)
python3 -I -B -c 'import sys; d=bytearray(open(sys.argv[1],"rb").read()); d[200]^=0x01; open(sys.argv[2],"wb").write(d)' \
  "$O/maqam-rast.oft" "$O/test/broken.oft"
oft check "$O"/*.oft
if oft check "$O/test/broken.oft"; then echo "FAIL: broken.oft passed" >&2; exit 1; fi
oft header "$T/source/builtins.h" "equal/24 equal=$O/equal-24 equal.oft" \
  "maqam/rast=$O/maqam-rast.oft" "maqam/bayati=$O/maqam-bayati.oft" \
  "gamelan/slendro=$O/gamelan-slendro mesem.oft" "gamelan/pelog=$O/gamelan-pelog mesem.oft" "gamelan/gamelan=$O/gamelan-mesem.oft" \
  "pure/just=$O/pure-just.oft" "pure/pythagorean=$O/pure-pythagorean.oft"
