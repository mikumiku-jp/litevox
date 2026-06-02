#!/bin/sh
set -eu

if [ "${1:-}" = "" ]; then
  echo "usage: verify-cli-smoke.sh RUNTIME_ROOT [RESULT_PREFIX]" >&2
  exit 1
fi

runtime_root=$1
result_prefix=${2:-audio-compare/cli-smoke}
bin_path="$runtime_root/litevox"
tmp_dir=$(mktemp -d /tmp/litevox-cli-smoke.XXXXXX)
port=$((50880 + ($$ % 1000)))
server_pid=""

cleanup() {
  if [ "$server_pid" != "" ] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp_dir"
}

trap cleanup EXIT INT TERM

cat > "$tmp_dir/score.json" <<'EOF'
{"notes":[{"key":null,"frame_length":15,"lyric":""},{"key":60,"frame_length":45,"lyric":"ド"},{"key":62,"frame_length":45,"lyric":"レ"},{"key":64,"frame_length":45,"lyric":"ミ"},{"key":null,"frame_length":15,"lyric":""}]}
EOF

"$bin_path" help > "$result_prefix-help.txt"
"$bin_path" version --runtime "$runtime_root" > "$result_prefix-version.txt"
"$bin_path" deps --runtime "$runtime_root" --backend native > "$result_prefix-deps.txt"
"$bin_path" runtime_info --runtime "$runtime_root" > "$result_prefix-runtime_info.json"
"$bin_path" models --runtime "$runtime_root" > "$result_prefix-models.txt"
"$bin_path" styles --runtime "$runtime_root" > "$result_prefix-styles.txt"
"$bin_path" query --runtime "$runtime_root" --speaker 3 --text "ずんだもんなのだ" > "$result_prefix-query.json"
"$bin_path" tts --runtime "$runtime_root" --speaker 3 --text "ずんだもんなのだ" --out "$result_prefix-tts.wav"
"$bin_path" stream --runtime "$runtime_root" --speaker 3 --text "ずんだもんなのだ" --format pcm --chunk-samples 4096 > "$result_prefix-stream.pcm"
"$bin_path" sing-query --runtime "$runtime_root" --backend native --score "$tmp_dir/score.json" --speaker 6000 --out "$result_prefix-frame_audio_query.json"
"$bin_path" sing-f0 --runtime "$runtime_root" --backend native --score "$tmp_dir/score.json" --frame-audio-query "$result_prefix-frame_audio_query.json" --speaker 6000 > "$result_prefix-sing-f0.json"
"$bin_path" sing-volume --runtime "$runtime_root" --backend native --score "$tmp_dir/score.json" --frame-audio-query "$result_prefix-frame_audio_query.json" --speaker 6000 > "$result_prefix-sing-volume.json"
"$bin_path" frame-synthesis --runtime "$runtime_root" --backend native --frame-audio-query "$result_prefix-frame_audio_query.json" --speaker 3000 --out "$result_prefix-frame.wav"
"$bin_path" bench --runtime "$runtime_root" --speaker 3 --text "ずんだもんなのだ" --runs 1 --workers 1 --cpu-threads 1 > "$result_prefix-bench.tsv"
"$bin_path" bench-song --runtime "$runtime_root" --backend native --score "$tmp_dir/score.json" --speaker 6000 --runs 1 --workers 1 > "$result_prefix-bench-song.tsv"

"$bin_path" server --runtime "$runtime_root" --port "$port" > "$result_prefix-server.log" 2>&1 &
server_pid=$!

ready=0
attempt=0
while [ "$attempt" -lt 60 ]; do
  if curl -fsS "http://127.0.0.1:$port/version" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
  attempt=$((attempt + 1))
done

if [ "$ready" -ne 1 ]; then
  echo "server did not become ready" >&2
  exit 1
fi

printf 'ずんだもんなのだ\n' | "$bin_path" api-session --host 127.0.0.1 --port "$port" --speaker 3 --http-path /tts --out "$tmp_dir/api-session-tts" >/dev/null
"$bin_path" api-session --host 127.0.0.1 --port "$port" --speaker 6000 --http-path /sing_frame_audio_query --score "$tmp_dir/score.json" --out "$tmp_dir/api-session-song" >/dev/null

wc -c "$result_prefix-tts.wav" | awk '{print "tts_wav_bytes\t" $1}' > "$result_prefix-summary.tsv"
wc -c "$result_prefix-stream.pcm" | awk '{print "stream_pcm_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$result_prefix-frame.wav" | awk '{print "frame_wav_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$result_prefix-query.json" | awk '{print "query_json_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$result_prefix-frame_audio_query.json" | awk '{print "frame_audio_query_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$result_prefix-sing-f0.json" | awk '{print "sing_f0_json_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$result_prefix-sing-volume.json" | awk '{print "sing_volume_json_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -l "$result_prefix-models.txt" | awk '{print "models_lines\t" $1}' >> "$result_prefix-summary.tsv"
wc -l "$result_prefix-styles.txt" | awk '{print "styles_lines\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$tmp_dir/api-session-tts/0001.wav" | awk '{print "api_session_tts_wav_bytes\t" $1}' >> "$result_prefix-summary.tsv"
wc -c "$tmp_dir/api-session-song/0001.json" | awk '{print "api_session_song_json_bytes\t" $1}' >> "$result_prefix-summary.tsv"
