#!/usr/bin/env bash
# Refresh the vendored Linux compiler in ./dist from a GitHub release.
# The repo is private, so the playground build can't fetch the asset itself —
# we vendor it here and commit it. Run this whenever you cut a new release.
#
#   ./update-dist.sh            # uses the latest tag
#   ./update-dist.sh v0.2.5     # a specific tag
set -euo pipefail

cd "$(dirname "$0")"
TAG="${1:-$(gh release list --limit 1 --json tagName -q '.[0].tagName')}"
ASSET="eskiuc-linux-x86_64.tar.gz"

echo "Fetching $ASSET from release $TAG…"
gh release download "$TAG" -p "$ASSET" -O /tmp/eskiuc-linux.tar.gz --clobber

rm -rf dist && mkdir -p dist
tar -xzf /tmp/eskiuc-linux.tar.gz -C dist
rm -f /tmp/eskiuc-linux.tar.gz

echo "Vendored eskiuc $TAG into ./dist:"
du -sh dist/bin/eskiuc
echo "Done. Commit the dist/ change, then rebuild the image."
