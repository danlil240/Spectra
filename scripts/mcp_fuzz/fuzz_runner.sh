#!/usr/bin/env bash
# Spectra MCP fuzz runner — bootstrap, warm-up, 200 fuzz steps, bursts, issue JSONL.
# Usage: fuzz_runner.sh [BINARY] [SESSION_TAG]
#   BINARY      default: $SPECTRA_ROOT/build/spectra
#   SESSION_TAG default: spectra (log prefix /tmp/spectra_fuzz_${TAG}.*)
#
# Env: SPECTRA_ROOT, DISPLAY (prefer :1), SEED (default 42), FUZZ_STEPS (default 200),
#      SPECTRA_MCP_URL (default http://127.0.0.1:8765/mcp)
#      SPECTRA_NO_NATIVE_DIALOGS=1 is set automatically on launch.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="${SPECTRA_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BINARY="${1:-$WORKSPACE/build/spectra}"
BINARY_NAME="$(basename "$BINARY")"
SESSION_TAG="${2:-spectra}"
LOG="${SPECTRA_FUZZ_LOG:-/tmp/spectra_fuzz_${SESSION_TAG}.jsonl}"
ISSUES="${SPECTRA_FUZZ_ISSUES:-/tmp/spectra_fuzz_${SESSION_TAG}_issues.jsonl}"
STDERR_LOG="${SPECTRA_FUZZ_STDERR:-/tmp/spectra_fuzz_${SESSION_TAG}_stderr.log}"
SEED="${SEED:-42}"
FUZZ_STEPS="${FUZZ_STEPS:-200}"
MCP="${SPECTRA_MCP_URL:-http://127.0.0.1:8765/mcp}"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR"
export DISPLAY="${DISPLAY:-:1}"
if [[ ! -S "/tmp/.X11-unix/X${DISPLAY#:}" ]] && [[ -S /tmp/.X11-unix/X1 ]]; then
  export DISPLAY=:1
fi
if [[ ! -S "/tmp/.X11-unix/X${DISPLAY#:}" ]]; then
  if ! pgrep -x Xvfb >/dev/null; then
    Xvfb :99 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/tmp/xvfb_fuzz.log 2>&1 &
    sleep 2
  fi
  export DISPLAY=:99
fi
if [[ "$DISPLAY" == ":99" ]]; then
  export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$(find /usr/share/vulkan/icd.d -name '*lvp*' 2>/dev/null | head -1)}"
  export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
else
  unset LIBGL_ALWAYS_SOFTWARE
fi

export SPECTRA_NO_NATIVE_DIALOGS=1

SKIP_CMDS='app.quit|help.show|file.export_png|file.export_svg|accessibility.sonify_series|data.export_html_table|file.copy_to_clipboard'

: > "$LOG"
: > "$ISSUES"

log_issue() {
  local sev="$1" title="$2" detail="$3"
  echo "{\"severity\":\"$sev\",\"title\":\"$title\",\"detail\":$(python3 -c "import json; print(json.dumps('''$detail'''))")}" >> "$ISSUES"
  echo "[ISSUE $sev] $title: $detail"
}

mcp_call() {
  local tool="$1"
  local args="${2:-{}}"
  local timeout_s="${3:-30}"
  curl -s --max-time "$timeout_s" -X POST "$MCP" \
    -H "Content-Type: application/json" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool\",\"arguments\":$args}}"
}

mcp_ok() {
  python3 -c "
import sys, json
try:
    d=json.load(sys.stdin)
    if 'error' in d: sys.exit(1)
    r=d.get('result')
    if r is None: sys.exit(1)
    if isinstance(r, dict) and r.get('isError'): sys.exit(1)
    sys.exit(0)
except Exception:
    sys.exit(1)
" 2>/dev/null
}

extract_text() {
  python3 -c "
import sys, json
d=json.load(sys.stdin)
r=d.get('result',{})
if isinstance(r,dict):
    sc=r.get('structuredContent')
    if sc:
        import json as j; print(j.dumps(sc)); raise SystemExit
    for c in r.get('content',[]):
        if c.get('type')=='text':
            print(c.get('text',''))
            break
    else:
        print(json.dumps(r))
else:
    print(json.dumps(d))
" 2>/dev/null <<< "$1"
}

extract_pump() {
  python3 -c "
import sys, json, re
d=json.load(sys.stdin)
r=d.get('result',{})
sc=r.get('structuredContent') if isinstance(r,dict) else None
if sc and 'pump_frames' in sc:
    print(sc['pump_frames']); raise SystemExit
text=''
if isinstance(r,dict):
    for c in r.get('content',[]):
        if c.get('type')=='text': text=c.get('text','')
try:
    j=json.loads(text) if text.strip().startswith('{') else {}
    print(j.get('pump_frames', j.get('frames', 2)))
except Exception:
    m=re.search(r'pump_frames[\"'\'']?\s*[:=]\s*(\d+)', text)
    print(m.group(1) if m else 2)
" 2>/dev/null <<< "$1"
}

json_field() {
  local json_text="$1" field="$2" default="${3:-0}"
  python3 -c "
import sys, json, re
t=sys.argv[1]; field=sys.argv[2]; default=sys.argv[3]
try:
    j=json.loads(t)
    print(j.get(field, default))
except Exception:
    m=re.search(rf'{field}[\"'\'']?\s*[:=]\s*(\d+)', t)
    print(m.group(1) if m else default)
" "$json_text" "$field" "$default" 2>/dev/null
}

ping_alive() {
  mcp_ok "$(mcp_call ping '{}' 5)"
}

kill_spectra() {
  pkill -x spectra 2>/dev/null || true
  pkill -x spectra-ros 2>/dev/null || true
  sleep 0.8
}

health_ok() {
  curl -s --max-time 2 http://127.0.0.1:8765/ 2>/dev/null | grep -q jsonrpc
}

launch() {
  kill_spectra
  cd "$WORKSPACE"
  if [[ "$BINARY_NAME" == "spectra-ros" ]]; then
    bash -lc "source /opt/ros/jazzy/setup.bash 2>/dev/null; exec \"$BINARY\" --no-native-dialogs" > "$STDERR_LOG" 2>&1 &
  else
    "$BINARY" --no-native-dialogs > "$STDERR_LOG" 2>&1 &
  fi
  local pid=$!
  for _ in $(seq 1 40); do
    if health_ok; then
      sleep 1
      echo "Launched $BINARY_NAME pid=$pid"
      return 0
    fi
    sleep 0.25
  done
  log_issue CRITICAL "MCP failed to start" "Binary $BINARY did not expose MCP within 10s. stderr: $(tail -5 "$STDERR_LOG")"
  return 1
}

check_png_not_blank() {
  local path="$1"
  [[ -f "$path" ]] || { log_issue ERROR "Screenshot missing" "$path"; return 1; }
  local size
  size=$(stat -c%s "$path" 2>/dev/null || echo 0)
  if [[ "$size" -lt 500 ]]; then
    log_issue ERROR "Tiny screenshot" "$path only ${size} bytes"
    return 1
  fi
  return 0
}

log_json() {
  python3 -c "
import json, sys
entry={'phase':sys.argv[1],'step':sys.argv[2],'tool':sys.argv[3]}
try:
    entry['response']=json.loads(sys.argv[4])
except Exception:
    entry['response_raw']=sys.argv[4][:2000]
print(json.dumps(entry))
" "$1" "$2" "$3" "$4" >> "$LOG"
}

echo "=== Fuzz session: $BINARY_NAME tag=$SESSION_TAG ==="

launch || exit 1

for tool in ping get_state get_window_size list_commands list_fuzz_actions; do
  r=$(mcp_call "$tool" '{}')
  log_json bootstrap 0 "$tool" "$r"
  mcp_ok "$r" || log_issue WARNING "Bootstrap tool failed" "$tool: $(extract_text <<<"$r" | head -c 200)"
done

r=$(mcp_call fuzz_reset "{\"seed\":$SEED}")
log_json bootstrap 0 fuzz_reset "$r"

ws=$(mcp_call get_window_size '{}' 5)
ww=$(json_field "$(extract_text <<<"$ws")" width 0)
if [[ "${ww:-0}" -eq 0 ]]; then
  log_issue ERROR "GLFW window not created" "get_window_size returned 0x0; prefer DISPLAY=:1. stderr: $(tail -3 "$STDERR_LOG")"
fi

st=$(mcp_call get_state '{}' 5)
afid=$(json_field "$(extract_text <<<"$st")" active_figure_id 0)
if [[ "${afid:-0}" == "18446744073709551615" ]]; then
  log_issue WARNING "Invalid active_figure_id sentinel" "get_state reports UINT64_MAX when no figures exist"
fi

r=$(mcp_call create_figure '{}')
log_json warmup 1 create_figure "$r"
FIG_ID=$(json_field "$(extract_text <<<"$r")" figure_id 1)

r=$(mcp_call add_series "{\"figure_id\":${FIG_ID:-1},\"n_points\":200}")
log_json warmup 2 add_series "$r"
mcp_call wait_frames '{"count":15}' >/dev/null

for cmd in view.toggle_grid view.toggle_legend view.toggle_crosshair view.autofit view.zoom_in figure.new edit.undo edit.redo series.cycle_selection; do
  r=$(mcp_call execute_command "{\"command_id\":\"$cmd\"}")
  log_json warmup cmd "$cmd" "$r"
  mcp_call pump_frames '{"count":2}' >/dev/null
  mcp_ok "$r" || log_issue WARNING "Warmup command failed" "$cmd: $(extract_text <<<"$r" | head -c 150)"
done

BASELINE="/tmp/spectra_fuzz_baseline_${SESSION_TAG}.png"
mcp_call wait_frames '{"count":10}' >/dev/null
r=$(mcp_call capture_window "{\"path\":\"$BASELINE\"}")
log_json warmup cap baseline "$r"
check_png_not_blank "$BASELINE"

echo "Starting $FUZZ_STEPS fuzz steps..."
for step in $(seq 1 "$FUZZ_STEPS"); do
  r=$(mcp_call fuzz_step '{}' 30)
  log_json fuzz "$step" fuzz_step "$r"
  if ! ping_alive; then
    log_issue CRITICAL "Crash during fuzz loop" "step=$step seed=$SEED last=$(extract_text <<<"$r" | head -c 300)"
    break
  fi
  pump=$(extract_pump <<<"$r")
  mcp_call pump_frames "{\"count\":${pump:-2}}" >/dev/null

  if (( step % 25 == 0 )); then
    sr=$(mcp_call get_state '{}')
    log_json fuzz "$step" get_state "$sr"
    fc=$(json_field "$(extract_text <<<"$sr")" figure_count 0)
    uc=$(json_field "$(extract_text <<<"$sr")" undo_count 0)
    [[ "${fc:-0}" -gt 25 ]] && log_issue WARNING "Runaway figure_count" "step=$step figure_count=$fc"
    [[ "${uc:-0}" -gt 500 ]] && log_issue WARNING "Runaway undo_count" "step=$step undo_count=$uc"
  fi

  if (( step % 50 == 0 )); then
    cap="/tmp/spectra_fuzz_${SESSION_TAG}_step_${step}.png"
    mcp_call wait_frames '{"count":5}' >/dev/null
    mcp_call capture_window "{\"path\":\"$cap\"}" >/dev/null
    check_png_not_blank "$cap"
  fi

  if (( step % 100 == 0 )); then
    for sz in '320 240' '1920 600' '640 480'; do
      set -- $sz
      mcp_call resize_window "{\"width\":$1,\"height\":$2}" >/dev/null
      mcp_call pump_frames '{"count":3}' >/dev/null
    done
  fi
done

if ping_alive; then
  echo "Command exhaustion..."
  cmds=$(mcp_call list_commands '{}' | extract_text | python3 -c "
import sys,json,re
t=sys.stdin.read()
try:
    j=json.loads(t)
    cmds=j if isinstance(j,list) else j.get('commands',[])
except Exception:
    cmds=re.findall(r'\"([a-z][a-z0-9_.]+)\"', t)
skip=set('''$SKIP_CMDS'''.split('|'))
for c in cmds:
    cid=c if isinstance(c,str) else c.get('id',c.get('command_id',''))
    if cid and cid not in skip and not any(x in cid for x in ['quit','save','load']):
        print(cid)
" 2>/dev/null)

  cmd_n=0
  while IFS= read -r cmd; do
    [[ -z "$cmd" ]] && continue
    ((cmd_n++)) || true
    r=$(mcp_call execute_command "{\"command_id\":\"$cmd\"}" 15)
    log_json exhaustion "$cmd_n" "$cmd" "$r"
    if ! ping_alive; then
      log_issue CRITICAL "Crash during command exhaustion" "command=$cmd step=$cmd_n seed=$SEED"
      break
    fi
    mcp_ok "$r" || log_issue WARNING "Command returned error" "$cmd: $(extract_text <<<"$r" | head -c 200)"
    mcp_call pump_frames '{"count":2}' >/dev/null
  done <<< "$cmds"

  if ping_alive; then
    echo "Resize marathon..."
    for i in $(seq 1 20); do
      w=$((200 + RANDOM % 1720))
      h=$((200 + RANDOM % 880))
      mcp_call resize_window "{\"width\":$w,\"height\":$h}" >/dev/null
      mcp_call pump_frames '{"count":1}' >/dev/null
      log_json resize_marathon "$i" resize_window "{\"w\":$w,\"h\":$h}"
    done
  fi

  if ping_alive; then
    echo "Figure lifecycle + TabDetach..."
    for _ in $(seq 1 10); do
      mcp_call create_figure '{}' >/dev/null
      mcp_call add_series '{"n_points":50}' >/dev/null
      mcp_call pump_frames '{"count":2}' >/dev/null
    done
    for i in $(seq 1 5); do
      r=$(mcp_call fuzz_step '{"action":"CloseFigure"}')
      mcp_call pump_frames '{"count":2}' >/dev/null
      log_json lifecycle "$i" CloseFigure "$r"
    done
    for i in $(seq 1 8); do
      r=$(mcp_call fuzz_step '{"action":"TabDetach"}')
      mcp_call pump_frames '{"count":3}' >/dev/null
      log_json tab_detach "$i" TabDetach "$r"
      ping_alive || { log_issue ERROR "Crash during TabDetach" "iteration=$i"; break; }
    done
  fi

  if [[ "$BINARY_NAME" == "spectra-ros" ]] && ping_alive; then
    echo "ROS panel exploration..."
    for xy in "40 200" "40 350" "120 150" "200 400" "350 100" "500 300" "80 50"; do
      set -- $xy
      mcp_call mouse_click "{\"x\":$1,\"y\":$2}" >/dev/null
      mcp_call pump_frames '{"count":3}' >/dev/null
    done
    mcp_call capture_window "{\"path\":\"/tmp/spectra_fuzz_ros_${SESSION_TAG}.png\"}" >/dev/null
  fi
fi

if ping_alive; then
  echo "Isolated crash-prone command probes..."
  for cmd in file.copy_to_clipboard file.export_png file.export_svg help.show accessibility.sonify_series data.export_html_table; do
    ping_alive || break
    r=$(mcp_call execute_command "{\"command_id\":\"$cmd\"}" 15)
    log_json isolated "$cmd" "$cmd" "$r"
    mcp_call pump_frames '{"count":3}' >/dev/null
    if ! ping_alive; then
      log_issue CRITICAL "Crash on isolated command" "$cmd"
    elif ! mcp_ok "$r"; then
      log_issue WARNING "Isolated command error" "$cmd: $(extract_text <<<"$r" | head -c 200)"
    fi
  done
fi

FINAL_ALIVE=false
ping_alive && FINAL_ALIVE=true
log_json final 0 get_state "$(mcp_call get_state '{}' 5 2>/dev/null || echo '{}')"

if [[ -f "$STDERR_LOG" ]]; then
  grep -iE 'error|segfault|assert|fatal|xclip.*not found' "$STDERR_LOG" 2>/dev/null | head -10 | while IFS= read -r line; do
    log_issue WARNING "stderr log" "$line"
  done
fi

echo "SESSION_DONE binary=$BINARY_NAME alive=$FINAL_ALIVE issues=$(wc -l < "$ISSUES") log=$LOG issues_file=$ISSUES"
