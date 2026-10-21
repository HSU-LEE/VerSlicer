#!/usr/bin/env bash
# List the places where OrcaSlicer's own files call into VerSlicer code.
# These are the files to check by hand when pulling a new OrcaSlicer release
# (see docs/UPSTREAM_SYNC.md). Everything else in the VerSlicer folders below
# is ours and should merge without conflicts.
#
# Usage: ./scripts/verslicer_hooks.sh

set -euo pipefail
cd "$(dirname "$0")/.."

OWN='Assistant/|BambuSmartPrint/|MakerWorld/|AICoach/|AIPipeline/|ModelSearch/|BambuConnect'

echo "VerSlicer folders (ours):"
for d in Assistant BambuSmartPrint MakerWorld AICoach AIPipeline ModelSearch; do
    [ -d "src/slic3r/GUI/$d" ] && printf '  src/slic3r/GUI/%-18s %4s files\n' "$d/" "$(ls src/slic3r/GUI/$d | wc -l | tr -d ' ')"
done
[ -d src/libslic3r/BambuSmartPrint ] && printf '  %-34s %4s files\n' "src/libslic3r/BambuSmartPrint/" "$(ls src/libslic3r/BambuSmartPrint | wc -l | tr -d ' ')"
echo
echo "OrcaSlicer files that include VerSlicer code (hook points):"
grep -rlE "#include \"[^\"]*($OWN)" src --include='*.cpp' --include='*.hpp' --include='*.h' \
    | grep -vE "src/slic3r/GUI/($OWN)|src/libslic3r/BambuSmartPrint/|src/dev-utils/" \
    | while read -r f; do
        printf '  %3d  %s\n' "$(grep -cE "#include \"[^\"]*($OWN)" "$f")" "$f"
      done | sort -rn
