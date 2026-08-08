#!/usr/bin/env bash
# Rebuild render-test frames and assemble git-ignored local view composites.
# By default frames are compared to committed snapshots; --update rewrites them.
set -euo pipefail

update=0
case "${1-}" in
  "") ;;
  --update) update=1 ;;
  *)
    echo "usage: $0 [--update]" >&2
    exit 2
    ;;
esac

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
yaml="esphome/tests/render_test/render_test.yaml"
bin="esphome/tests/render_test/.esphome/build/pixoo64-render-test/.pioenvs/pixoo64-render-test/program"
"$repo/.venv/bin/esphome" compile "$yaml" >/dev/null
if ((update)); then
  PIXOO_UPDATE_SNAPSHOTS=1 "$bin"
else
  (unset PIXOO_UPDATE_SNAPSHOTS; "$bin")
fi
"$repo/.venv/bin/python" tools/render-test-contact-sheet.py
"$repo/.venv/bin/python" tools/render-test-icon-gallery.py
