#!/bin/zsh
set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: $(basename "$0") VOICEVOX_ZIP ONNXRUNTIME_ARCHIVE RUNTIME_ROOT [RESULT_PREFIX]" >&2
  exit 1
fi

voicevox_zip=$1
onnxruntime_archive=$2
runtime_root=$3
result_prefix=${4:-audio-compare/runtime-verify}

script_dir=$(cd "$(dirname "$0")" && pwd)
litevox_root=$(cd "$script_dir/.." && pwd)
if [ -x "$litevox_root/dist/litevox" ]; then
  result_base_dir=$(cd "$litevox_root/.." && pwd)
  litevox_bin="$litevox_root/dist/litevox"
elif [ -x "$litevox_root/litevox" ]; then
  result_base_dir="$litevox_root"
  litevox_bin="$litevox_root/litevox"
else
  echo "missing litevox binary next to tools/: $litevox_root" >&2
  exit 1
fi
compare_short="$script_dir/compare_voicevox_http.py"
compare_full="$script_dir/compare_voicevox_http_full.py"

for required_path in "$litevox_bin" "$compare_short" "$compare_full" "$voicevox_zip" "$onnxruntime_archive"; do
  if [ ! -e "$required_path" ]; then
    echo "missing: $required_path" >&2
    exit 1
  fi
done

if [[ "$result_prefix" = /* ]]; then
  result_prefix_path="$result_prefix"
else
  result_prefix_path="$result_base_dir/$result_prefix"
fi
mkdir -p "$(dirname "$result_prefix_path")"
rm -rf "$runtime_root"

official_engine_root=$(mktemp -d /tmp/litevox-official-engine-verify.XXXXXX)
official_run="$official_engine_root/VOICEVOX/VOICEVOX.app/Contents/Resources/vv-engine/run"

official_engine_entry_count=$(python3 - "$voicevox_zip" "$official_engine_root" <<'PY'
import os
import pathlib
import shutil
import stat
import sys
import zipfile

archive_path = pathlib.Path(sys.argv[1])
output_root = pathlib.Path(sys.argv[2])
prefix = "VOICEVOX/VOICEVOX.app/Contents/Resources/vv-engine/"
entry_count = 0

with zipfile.ZipFile(archive_path) as archive:
    for entry in archive.infolist():
        if not entry.filename.startswith(prefix) or entry.is_dir():
            continue
        target_path = output_root / entry.filename
        target_path.parent.mkdir(parents=True, exist_ok=True)
        mode = (entry.external_attr >> 16) & 0o777
        file_mode = (entry.external_attr >> 16) & 0o170000
        if file_mode == stat.S_IFLNK:
            link_target = archive.read(entry).decode("utf-8")
            os.symlink(link_target, target_path)
        else:
            with archive.open(entry, "r") as source, target_path.open("wb") as destination:
                shutil.copyfileobj(source, destination)
        if mode and file_mode != stat.S_IFLNK:
            os.chmod(target_path, mode)
        entry_count += 1

run_path = output_root / "VOICEVOX/VOICEVOX.app/Contents/Resources/vv-engine/run"
if not run_path.exists():
    raise SystemExit("official vv-engine run が zip 内に見つかりません")
run_mode = run_path.stat().st_mode
os.chmod(run_path, run_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
print(entry_count)
PY
)

ports=(${(f)"$(python3 - <<'PY'
import socket
ports = []
for _ in range(2):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        ports.append(str(sock.getsockname()[1]))
print("\n".join(ports))
PY
)"} )
official_port=${ports[1]}
litevox_port=${ports[2]}

official_home=$(mktemp -d /tmp/litevox-official-home-verify.XXXXXX)
litevox_state=$(mktemp -d /tmp/litevox-state-verify.XXXXXX)
official_pid=""
litevox_pid=""

cleanup() {
  if [ -n "$litevox_pid" ] && kill -0 "$litevox_pid" 2>/dev/null; then
    kill "$litevox_pid" 2>/dev/null || true
    wait "$litevox_pid" 2>/dev/null || true
  fi
  if [ -n "$official_pid" ] && kill -0 "$official_pid" 2>/dev/null; then
    kill "$official_pid" 2>/dev/null || true
    wait "$official_pid" 2>/dev/null || true
  fi
  rm -rf "$official_home" "$litevox_state" "$official_engine_root"
}

trap cleanup EXIT INT TERM

{
  "$litevox_bin" model-dump "$voicevox_zip" --add-model "$onnxruntime_archive" --extract-runtime "$runtime_root"
  printf 'official_vv_engine_entries\t%s\n' "$official_engine_entry_count"
} > "${result_prefix_path}-extract.tsv"
"$litevox_bin" runtime_info --runtime "$runtime_root" > "${result_prefix_path}-runtime_info.json"

(
  cd "$(dirname "$official_run")"
  HOME="$official_home" "$official_run" --host 127.0.0.1 --port "$official_port" --output_log_utf8
) > "${result_prefix_path}-official.log" 2>&1 &
official_pid=$!

"$litevox_bin" server --runtime "$runtime_root" --state-dir "$litevox_state" --port "$litevox_port" > "${result_prefix_path}-litevox.log" 2>&1 &
litevox_pid=$!

wait_for_http() {
  python3 - "$1" "$2" <<'PY'
import sys
import time
import urllib.error
import urllib.request

base_url = sys.argv[1]
deadline = time.time() + float(sys.argv[2])
while time.time() < deadline:
    try:
        with urllib.request.urlopen(base_url + "/version", timeout=2) as response:
            if response.status == 200:
                sys.exit(0)
    except Exception:
        time.sleep(0.25)
        continue
sys.exit(1)
PY
}

wait_for_http "http://127.0.0.1:${official_port}" 120
wait_for_http "http://127.0.0.1:${litevox_port}" 120

python3 "$compare_short" "http://127.0.0.1:${official_port}" "http://127.0.0.1:${litevox_port}" > "${result_prefix_path}-45.jsonl"
python3 "$compare_full" "http://127.0.0.1:${official_port}" "http://127.0.0.1:${litevox_port}" > "${result_prefix_path}-65.jsonl"

tail -n 1 "${result_prefix_path}-45.jsonl"
tail -n 1 "${result_prefix_path}-65.jsonl"
