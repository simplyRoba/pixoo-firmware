#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: ./tools/esptool-readonly.sh --port <serial-port> [--baud <baud>] [--dump] [--dump-baud <baud>]
Examples:
  ./tools/esptool-readonly.sh --port <serial-port>
  ./tools/esptool-readonly.sh --port <serial-port> --dump
  ./tools/esptool-readonly.sh --port <serial-port> --dump --dump-baud 230400

Full backups are written below local/backups/ (git-ignored) and can contain
plaintext device credentials. Never publish them.
EOF
}

port=""
baud="115200"
dump_baud="230400"
do_dump=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      port="$2"
      shift 2
      ;;
    --baud)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      baud="$2"
      shift 2
      ;;
    --dump)
      do_dump=1
      shift
      ;;
    --dump-baud)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      dump_baud="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

[[ -n "$port" ]] || { usage; exit 1; }

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -x "$repo/.venv/bin/python" ]]; then
  py="$repo/.venv/bin/python"
else
  py=python3
fi

if ! command -v "$py" >/dev/null 2>&1; then
  echo "Python not found: $py" >&2
  exit 1
fi

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="$repo/local/backups/flash/${timestamp}-readonly"
mkdir -p "$out_dir"

run_esptool() {
  local name="$1"
  shift
  local log="$out_dir/${name}.txt"
  echo "[esptool-readonly] $name -> $log"
  {
    echo "$py -m esptool --before no-reset --after no-reset --port $port --baud $baud $*"
    "$py" -m esptool --before no-reset --after no-reset --port "$port" --baud "$baud" "$@"
  } | tee "$log"
}

run_espefuse() {
  local name="$1"
  shift
  local log="$out_dir/${name}.txt"
  echo "[esptool-readonly] $name -> $log"
  {
    echo "$py -m espefuse --port $port --baud $baud --before no-reset --after no-reset $*"
    "$py" -m espefuse --port "$port" --baud "$baud" --before no-reset --after no-reset "$@"
  } | tee "$log"
}

flash_size_to_hex() {
  case "$1" in
    1MB) echo 0x100000 ;;
    2MB) echo 0x200000 ;;
    4MB) echo 0x400000 ;;
    8MB) echo 0x800000 ;;
    16MB) echo 0x1000000 ;;
    32MB) echo 0x2000000 ;;
    *) return 1 ;;
  esac
}

parse_security_clean() {
  local file="$1"

  if grep -Eqi 'flash encryption[^[:alpha:]]*[:=][[:space:]]*disabled|flash_encryption[^[:alpha:]]*[:=][[:space:]]*false' "$file" \
     && grep -Eqi 'secure boot[^[:alpha:]]*[:=][[:space:]]*disabled|secure_boot[^[:alpha:]]*[:=][[:space:]]*false' "$file"; then
    return 0
  fi

  if grep -Eq 'FLASH_CRYPT_CNT .* = 0 ' "$file" \
     && grep -Eq 'ABS_DONE_0 .* = False ' "$file" \
     && grep -Eq 'ABS_DONE_1 .* = False ' "$file"; then
    return 0
  fi

  return 1
}

cat <<EOF
Read-only ESP32 check:
  port:    $port
  baud:    $baud
  output:  $out_dir
  dump:    $( [[ "$do_dump" -eq 1 ]] && echo yes || echo no )

Full backups can contain plaintext device credentials. Never publish them.
Put the device in ROM download mode first (hold IO0 low during cold power-on).
EOF

run_esptool chip-id chip-id
if ! run_esptool security-info get-security-info; then
  echo "[esptool-readonly] get-security-info not usable on this target; falling back to espefuse summary" >&2
  run_espefuse security-info summary
fi
run_esptool flash-id flash-id

flash_size="$(grep -Eo 'Detected flash size: [0-9]+MB' "$out_dir/flash-id.txt" | tail -n1 | awk '{print $4}')"
if [[ -n "$flash_size" ]]; then
  flash_size_hex="$(flash_size_to_hex "$flash_size")"
  echo "$flash_size" > "$out_dir/detected-flash-size.txt"
  echo "[esptool-readonly] detected flash size: $flash_size ($flash_size_hex)"
else
  flash_size_hex=""
  echo "[esptool-readonly] could not parse detected flash size from flash_id output" >&2
fi

if [[ "$do_dump" -eq 0 ]]; then
  echo "[esptool-readonly] metadata capture done; rerun with --dump after review"
  exit 0
fi

if ! parse_security_clean "$out_dir/security-info.txt"; then
  echo "[esptool-readonly] refusing dump: security state not clearly parsed as Secure Boot disabled + Flash Encryption disabled" >&2
  echo "[esptool-readonly] inspect $out_dir/security-info.txt manually" >&2
  exit 1
fi

if [[ -z "$flash_size_hex" ]]; then
  echo "[esptool-readonly] refusing dump: flash size could not be parsed" >&2
  exit 1
fi

out_bin="$out_dir/full-flash.bin"
out_sha="$out_bin.sha256"

echo "[esptool-readonly] dumping $flash_size from flash to $out_bin at $dump_baud baud"
{
  echo "$py -m esptool --before no-reset --after no-reset --port $port --baud $dump_baud read-flash 0x000000 $flash_size_hex $out_bin"
  "$py" -m esptool --before no-reset --after no-reset --port "$port" --baud "$dump_baud" read-flash 0x000000 "$flash_size_hex" "$out_bin"
} | tee "$out_dir/read-flash.txt"

shasum -a 256 "$out_bin" > "$out_sha"

echo "[esptool-readonly] done"
echo "  $out_bin"
echo "  $out_sha"
