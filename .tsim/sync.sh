#!/usr/bin/env bash
# Rebase our T-SIM delta onto a new IoTThinks/MeshCore PowerSaving branch head.
# Usage: .tsim/sync.sh <esm_branch> <new_sha> <esm_release_tag>   (run at the tsim branch head)
# Our delta = diff(.tsim/upstream.sha -> HEAD) outside .github/.tsim. It is applied 3-way on
# the new upstream tree and committed as ONE commit on top of HEAD (upstream's own workflow
# files are not imported, so GITHUB_TOKEN can push it). Exit 3 = conflicts (conflicts.txt).
set -euo pipefail
BR="$1"; NEW="$2"; REL="$3"
OLD=$(cat .tsim/upstream.sha)
BASE=$(git rev-parse HEAD)
git remote get-url esm >/dev/null 2>&1 || git remote add esm https://github.com/IoTThinks/MeshCore.git
git fetch -q --no-tags esm "$BR"
git cat-file -e "$OLD^{commit}" 2>/dev/null || git fetch -q --no-tags esm "$OLD"
git cat-file -e "$NEW^{commit}"
git diff --binary "$OLD" "$BASE" -- . ':(exclude).github' ':(exclude).tsim' > "$RUNNER_TEMP/ours.patch"
echo "our delta: $(grep -c '^diff --git' "$RUNNER_TEMP/ours.patch") files"
git checkout -q --detach "$NEW"
if ! git apply --3way --index "$RUNNER_TEMP/ours.patch" 2> "$RUNNER_TEMP/apply.err"; then
  cat "$RUNNER_TEMP/apply.err" >&2
  { git diff --name-only --diff-filter=U; grep -oE "(patch failed|conflicts?)[^:]*: [^ ]+" "$RUNNER_TEMP/apply.err" | sed 's/.*: //'; } | sort -u > conflicts.txt
  [ -s conflicts.txt ] || echo "(see the run log)" > conflicts.txt
  exit 3
fi
git rm -r -q --cached --ignore-unmatch .github .tsim; rm -rf .github .tsim
git checkout "$BASE" -- .github .tsim
echo "$NEW" > .tsim/upstream.sha; echo "$BR" > .tsim/upstream.branch; echo "$REL" > .tsim/upstream.release
git add -A .github .tsim
TREE=$(git write-tree)
C=$(git commit-tree "$TREE" -p "$BASE" -m "Sync EasySkyMesh $REL (IoTThinks/MeshCore $BR @ ${NEW:0:7}) with the T-SIM changes" -m "Upstream: https://github.com/IoTThinks/MeshCore/commit/$NEW")
git checkout -q --detach "$C"
echo "$C"
