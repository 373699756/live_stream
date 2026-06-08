#!/usr/bin/env bash

set -u
set -o pipefail

BASE_URL="http://127.0.0.1:8080"
STREAM="main"
DURATION_SEC=120
INTERVAL_SEC=2
HLS_CLIENTS=0
FLV_CLIENTS=0
MJPEG_CLIENTS=0
WEBRTC_CLIENTS=0
USER_NAME=""
PASSWORD=""
TOKEN="${LIVE_STREAM_TOKEN:-}"
PID=""
OUTPUT_DIR=""

Usage() {
    cat <<'EOF'
Usage: scripts/board_hot_path_probe.sh [options]

Collect board-side hot-path metrics while optional preview clients are running.

Options:
  --base-url URL          HTTP base URL, default http://127.0.0.1:8080
  --stream main|sub       Stream to load, default main
  --duration SEC          Sampling duration, default 120
  --interval SEC          Sampling interval, default 2
  --hls-clients N         Number of HLS clients to start
  --flv-clients N         Number of HTTP-FLV clients to start
  --mjpeg-clients N       Number of MJPEG clients to start
  --webrtc-clients N      Number of WebRTC client commands to start
  --user NAME             Login user for cookie auth
  --password PASSWORD     Login password for cookie auth
  --token TOKEN           Bearer token; also read from LIVE_STREAM_TOKEN
  --pid PID               live_stream PID; auto-detected when omitted
  --output-dir DIR        Output directory, default /tmp/live_stream_hot_path_TIMESTAMP
  -h, --help              Show this help

WebRTC load:
  Set WEBRTC_CLIENT_CMD to a shell command if --webrtc-clients is non-zero.
  The command receives BASE_URL, STREAM, WHEP_URL, COOKIE_JAR, AUTH_HEADER,
  and CLIENT_INDEX in the environment.

Outputs:
  metrics.csv             Sampled CPU/RSS/API/media diagnostics
  raw/*.json              Raw JSON responses per sample
  clients.log             Started clients and skipped client notes
  run.env                 Probe configuration
EOF
}

Die() {
    printf 'board-hot-path-probe: %s\n' "$*" >&2
    exit 1
}

HaveTool() {
    command -v "$1" >/dev/null 2>&1
}

NowMs() {
    local value
    value="$(date +%s%3N 2>/dev/null || true)"
    case "${value}" in
        *N*|"")
            printf '%s000\n' "$(date +%s)"
            ;;
        *)
            printf '%s\n' "${value}"
            ;;
    esac
}

NormalizeBaseUrl() {
    printf '%s\n' "${BASE_URL%/}"
}

ParseArgs() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --base-url)
                BASE_URL="$2"
                shift 2
                ;;
            --stream)
                STREAM="$2"
                shift 2
                ;;
            --duration)
                DURATION_SEC="$2"
                shift 2
                ;;
            --interval)
                INTERVAL_SEC="$2"
                shift 2
                ;;
            --hls-clients)
                HLS_CLIENTS="$2"
                shift 2
                ;;
            --flv-clients)
                FLV_CLIENTS="$2"
                shift 2
                ;;
            --mjpeg-clients)
                MJPEG_CLIENTS="$2"
                shift 2
                ;;
            --webrtc-clients)
                WEBRTC_CLIENTS="$2"
                shift 2
                ;;
            --user)
                USER_NAME="$2"
                shift 2
                ;;
            --password)
                PASSWORD="$2"
                shift 2
                ;;
            --token)
                TOKEN="$2"
                shift 2
                ;;
            --pid)
                PID="$2"
                shift 2
                ;;
            --output-dir)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            -h|--help)
                Usage
                exit 0
                ;;
            *)
                Die "unknown option: $1"
                ;;
        esac
    done
}

ValidateNumber() {
    local name="$1"
    local value="$2"
    case "${value}" in
        ''|*[!0-9]*)
            Die "${name} must be a non-negative integer"
            ;;
    esac
}

AutoDetectPid() {
    if [[ -n "${PID}" ]]; then
        return 0
    fi
    if HaveTool pgrep; then
        PID="$(pgrep -n live_stream 2>/dev/null || true)"
    fi
    if [[ -z "${PID}" ]] && HaveTool pidof; then
        PID="$(pidof live_stream 2>/dev/null | awk '{print $1}' || true)"
    fi
    if [[ -z "${PID}" ]]; then
        PID=0
    fi
}

CurlAuthArgs() {
    if [[ -n "${TOKEN}" ]]; then
        printf '%s\n' "-H"
        printf '%s\n' "Authorization: Bearer ${TOKEN}"
    fi
    if [[ -n "${COOKIE_JAR:-}" ]]; then
        printf '%s\n' "-b"
        printf '%s\n' "${COOKIE_JAR}"
    fi
}

LoginIfNeeded() {
    COOKIE_JAR="${OUTPUT_DIR}/cookies.txt"
    : >"${COOKIE_JAR}"
    if [[ -n "${TOKEN}" || -z "${USER_NAME}" ]]; then
        return 0
    fi
    local login_body="${OUTPUT_DIR}/login.json"
    local status
    status="$(
        curl -sS -o "${login_body}" -w '%{http_code}' \
            -c "${COOKIE_JAR}" \
            -H 'Content-Type: application/json' \
            -X POST \
            -d "{\"user_name\":\"${USER_NAME}\",\"password\":\"${PASSWORD}\"}" \
            "${BASE_URL}/api/auth/login" 2>/dev/null || true
    )"
    if [[ "${status}" != "200" ]]; then
        Die "login failed with HTTP ${status}; see ${login_body}"
    fi
}

CurlToFile() {
    local path="$1"
    local output_file="$2"
    shift 2
    mkdir -p "$(dirname "${output_file}")"
    local time_file="${output_file}.time"
    local status_file="${output_file}.status"
    local args=()
    while IFS= read -r arg; do
        args+=("${arg}")
    done < <(CurlAuthArgs)

    local status
    status="$(
        curl -sS "${args[@]}" "$@" -o "${output_file}" \
            -w '%{http_code} %{time_total}' \
            "${BASE_URL}${path}" 2>/dev/null || true
    )"
    printf '%s\n' "${status%% *}" >"${status_file}"
    printf '%s\n' "${status#* }" >"${time_file}"
    if [[ ! -f "${output_file}" ]]; then
        : >"${output_file}"
    fi
}

TimeMsFromFile() {
    local time_file="$1"
    awk '{printf "%.0f", ($1 + 0) * 1000}' "${time_file}" 2>/dev/null || printf '0'
}

JsonStreamField() {
    local json_file="$1"
    local stream="$2"
    local field="$3"
    if [[ ! -f "${json_file}" ]]; then
        return 0
    fi
    tr '{}' '\n' <"${json_file}" \
        | sed -n "/\"stream\":\"${stream}\"/s/.*\"${field}\":\\([0-9][0-9]*\\).*/\\1/p" \
        | head -n 1
}

JsonStringStreamField() {
    local json_file="$1"
    local stream="$2"
    local field="$3"
    if [[ ! -f "${json_file}" ]]; then
        return 0
    fi
    tr '{}' '\n' <"${json_file}" \
        | sed -n "/\"stream\":\"${stream}\"/s/.*\"${field}\":\"\\([^\"]*\\)\".*/\\1/p" \
        | head -n 1
}

JsonNumberField() {
    local json_file="$1"
    local field="$2"
    if [[ ! -f "${json_file}" ]]; then
        return 0
    fi
    sed -n "s/.*\"${field}\":\\([0-9][0-9]*\\).*/\\1/p" "${json_file}" | head -n 1
}

JsonSumField() {
    local json_file="$1"
    local field="$2"
    if [[ ! -f "${json_file}" ]]; then
        printf '0'
        return 0
    fi
    tr '{}' '\n' <"${json_file}" \
        | sed -n "s/.*\"${field}\":\\([0-9][0-9]*\\).*/\\1/p" \
        | awk '{sum += $1} END {print sum + 0}'
}

JsonCountField() {
    local json_file="$1"
    local field="$2"
    if [[ ! -f "${json_file}" ]]; then
        printf '0'
        return 0
    fi
    tr '{}' '\n' <"${json_file}" \
        | sed -n "s/.*\"${field}\":\\([0-9][0-9]*\\).*/x/p" \
        | wc -l \
        | awk '{print $1 + 0}'
}

DefaultZero() {
    if [[ -z "$1" ]]; then
        printf '0'
    else
        printf '%s' "$1"
    fi
}

CsvString() {
    local value="$1"
    value="${value//\"/\"\"}"
    printf '"%s"' "${value}"
}

ReadProcTicks() {
    local pid="$1"
    if [[ "${pid}" == "0" || ! -r "/proc/${pid}/stat" ]]; then
        printf '0'
        return
    fi
    awk '{print ($14 + $15)}' "/proc/${pid}/stat"
}

ReadCpuTotalIdle() {
    awk '/^cpu / {
        idle = $5 + $6
        total = 0
        for (i = 2; i <= NF; ++i) total += $i
        print total, idle
    }' /proc/stat
}

ReadVmKb() {
    local pid="$1"
    local name="$2"
    if [[ "${pid}" == "0" || ! -r "/proc/${pid}/status" ]]; then
        printf '0'
        return
    fi
    awk -v name="${name}" '$1 == name ":" {print $2 + 0}' "/proc/${pid}/status"
}

CpuCount() {
    local count
    count="$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || true)"
    if [[ -z "${count}" || "${count}" == "0" ]]; then
        count=1
    fi
    printf '%s\n' "${count}"
}

CalcProcCpuPct() {
    local prev_proc="$1"
    local curr_proc="$2"
    local prev_total="$3"
    local curr_total="$4"
    local cpu_count="$5"
    awk -v pp="${prev_proc}" -v cp="${curr_proc}" \
        -v pt="${prev_total}" -v ct="${curr_total}" \
        -v ncpu="${cpu_count}" \
        'BEGIN {
            dt = ct - pt
            dp = cp - pp
            if (dt <= 0 || dp < 0) {
                printf "0.00"
            } else {
                printf "%.2f", (dp / dt) * ncpu * 100.0
            }
        }'
}

CalcSystemCpuPct() {
    local prev_total="$1"
    local curr_total="$2"
    local prev_idle="$3"
    local curr_idle="$4"
    awk -v pt="${prev_total}" -v ct="${curr_total}" \
        -v pi="${prev_idle}" -v ci="${curr_idle}" \
        'BEGIN {
            dt = ct - pt
            di = ci - pi
            if (dt <= 0 || di < 0) {
                printf "0.00"
            } else {
                printf "%.2f", ((dt - di) / dt) * 100.0
            }
        }'
}

StartLoopClient() {
    local mode="$1"
    local index="$2"
    local url="$3"
    local log_file="${OUTPUT_DIR}/${mode}_${index}.log"
    local args=()
    while IFS= read -r arg; do
        args+=("${arg}")
    done < <(CurlAuthArgs)

    case "${mode}" in
        hls)
            if HaveTool ffmpeg; then
                (ffmpeg -nostdin -loglevel error -i "${url}" -f null - >"${log_file}" 2>&1) &
            else
                (
                    while :; do
                        curl -fsS "${args[@]}" -o /dev/null "${url}" >>"${log_file}" 2>&1
                        sleep 1
                    done
                ) &
            fi
            ;;
        flv|mjpeg)
            (curl -fsS -N "${args[@]}" -o /dev/null "${url}" >"${log_file}" 2>&1) &
            ;;
    esac
    CLIENT_PIDS+=("$!")
    printf '%s client %s pid=%s url=%s\n' "${mode}" "${index}" "$!" "${url}" >>"${OUTPUT_DIR}/clients.log"
}

StartWebrtcClient() {
    local index="$1"
    local url="$2"
    if [[ -z "${WEBRTC_CLIENT_CMD:-}" ]]; then
        printf 'webrtc client %s skipped: WEBRTC_CLIENT_CMD is not set\n' "${index}" >>"${OUTPUT_DIR}/clients.log"
        return
    fi
    (
        BASE_URL="${BASE_URL}" \
        STREAM="${STREAM}" \
        WHEP_URL="${url}" \
        COOKIE_JAR="${COOKIE_JAR}" \
        AUTH_HEADER="${TOKEN:+Authorization: Bearer ${TOKEN}}" \
        CLIENT_INDEX="${index}" \
        sh -c "${WEBRTC_CLIENT_CMD}"
    ) >>"${OUTPUT_DIR}/webrtc_${index}.log" 2>&1 &
    CLIENT_PIDS+=("$!")
    printf 'webrtc client %s pid=%s command=%s\n' "${index}" "$!" "${WEBRTC_CLIENT_CMD}" >>"${OUTPUT_DIR}/clients.log"
}

StopClients() {
    local pid
    for pid in "${CLIENT_PIDS[@]:-}"; do
        if kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
        fi
    done
}

WriteRunEnv() {
    {
        printf 'BASE_URL=%s\n' "${BASE_URL}"
        printf 'STREAM=%s\n' "${STREAM}"
        printf 'DURATION_SEC=%s\n' "${DURATION_SEC}"
        printf 'INTERVAL_SEC=%s\n' "${INTERVAL_SEC}"
        printf 'HLS_CLIENTS=%s\n' "${HLS_CLIENTS}"
        printf 'FLV_CLIENTS=%s\n' "${FLV_CLIENTS}"
        printf 'MJPEG_CLIENTS=%s\n' "${MJPEG_CLIENTS}"
        printf 'WEBRTC_CLIENTS=%s\n' "${WEBRTC_CLIENTS}"
        printf 'PID=%s\n' "${PID}"
        printf 'OUTPUT_DIR=%s\n' "${OUTPUT_DIR}"
        printf 'HAS_FFMPEG=%s\n' "$(HaveTool ffmpeg && printf yes || printf no)"
        printf 'WEBRTC_CLIENT_CMD=%s\n' "${WEBRTC_CLIENT_CMD:-}"
    } >"${OUTPUT_DIR}/run.env"
}

SampleOnce() {
    local sample_index="$1"
    local elapsed="$2"
    local raw_dir="${OUTPUT_DIR}/raw"
    local streams_file="${raw_dir}/streams_${sample_index}.json"
    local sessions_file="${raw_dir}/sessions_${sample_index}.json"
    local status_file="${raw_dir}/system_${sample_index}.json"
    local playlist_file="${raw_dir}/hls_${sample_index}.txt"
    local snapshot_file="${raw_dir}/snapshot_${sample_index}.jpg"

    CurlToFile "/api/media/streams" "${streams_file}"
    CurlToFile "/api/media/sessions" "${sessions_file}"
    CurlToFile "/api/system/status" "${status_file}"
    CurlToFile "/live/${STREAM}/hls/index.m3u8" "${playlist_file}"
    CurlToFile "/snapshot/${STREAM}.jpg" "${snapshot_file}"

    local api_streams_ms api_sessions_ms hls_ms snapshot_ms
    api_streams_ms="$(TimeMsFromFile "${streams_file}.time")"
    api_sessions_ms="$(TimeMsFromFile "${sessions_file}.time")"
    hls_ms="$(TimeMsFromFile "${playlist_file}.time")"
    snapshot_ms="$(TimeMsFromFile "${snapshot_file}.time")"

    local curr_proc curr_cpu curr_total curr_idle
    curr_proc="$(ReadProcTicks "${PID}")"
    read -r curr_total curr_idle < <(ReadCpuTotalIdle)
    local proc_cpu_pct system_cpu_pct
    proc_cpu_pct="$(CalcProcCpuPct "${PREV_PROC_TICKS}" "${curr_proc}" "${PREV_CPU_TOTAL}" "${curr_total}" "${CPU_COUNT}")"
    system_cpu_pct="$(CalcSystemCpuPct "${PREV_CPU_TOTAL}" "${curr_total}" "${PREV_CPU_IDLE}" "${curr_idle}")"
    PREV_PROC_TICKS="${curr_proc}"
    PREV_CPU_TOTAL="${curr_total}"
    PREV_CPU_IDLE="${curr_idle}"

    local rss_kb vmhwm_kb
    rss_kb="$(ReadVmKb "${PID}" "VmRSS")"
    vmhwm_kb="$(ReadVmKb "${PID}" "VmHWM")"

    local main_cached_bytes sub_cached_bytes main_hls_bytes sub_hls_bytes
    local main_cached_frames sub_cached_frames main_reader_count sub_reader_count
    local main_client_count sub_client_count main_reset sub_reset
    main_cached_bytes="$(DefaultZero "$(JsonStreamField "${streams_file}" main cached_bytes)")"
    sub_cached_bytes="$(DefaultZero "$(JsonStreamField "${streams_file}" sub cached_bytes)")"
    main_hls_bytes="$(DefaultZero "$(JsonStreamField "${streams_file}" main hls_bytes)")"
    sub_hls_bytes="$(DefaultZero "$(JsonStreamField "${streams_file}" sub hls_bytes)")"
    main_cached_frames="$(DefaultZero "$(JsonStreamField "${streams_file}" main cached_frames)")"
    sub_cached_frames="$(DefaultZero "$(JsonStreamField "${streams_file}" sub cached_frames)")"
    main_reader_count="$(DefaultZero "$(JsonStreamField "${streams_file}" main reader_count)")"
    sub_reader_count="$(DefaultZero "$(JsonStreamField "${streams_file}" sub reader_count)")"
    main_client_count="$(DefaultZero "$(JsonStreamField "${streams_file}" main client_count)")"
    sub_client_count="$(DefaultZero "$(JsonStreamField "${streams_file}" sub client_count)")"
    main_reset="$(JsonStringStreamField "${streams_file}" main last_reset_reason)"
    sub_reset="$(JsonStringStreamField "${streams_file}" sub last_reset_reason)"

    local sessions_pending sessions_count
    sessions_pending="$(JsonSumField "${sessions_file}" pending_bytes)"
    sessions_count="$(JsonCountField "${sessions_file}" session_id)"

    local webrtc_active http_flv_active mjpeg_active rtsp_active
    local rtsp_dropped webrtc_dropped webrtc_dropped_rtp ai_dropped
    webrtc_active="$(DefaultZero "$(JsonNumberField "${sessions_file}" webrtc_active_peers)")"
    http_flv_active="$(DefaultZero "$(JsonNumberField "${sessions_file}" http_flv_active_clients)")"
    mjpeg_active="$(DefaultZero "$(JsonNumberField "${sessions_file}" mjpeg_active_clients)")"
    rtsp_active="$(DefaultZero "$(JsonNumberField "${sessions_file}" rtsp_active_sessions)")"
    rtsp_dropped="$(DefaultZero "$(JsonNumberField "${sessions_file}" rtsp_dropped_frames)")"
    webrtc_dropped="$(DefaultZero "$(JsonNumberField "${sessions_file}" webrtc_dropped_frames)")"
    webrtc_dropped_rtp="$(DefaultZero "$(JsonNumberField "${sessions_file}" webrtc_dropped_rtp_packets)")"
    ai_dropped=0

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$(NowMs)" "${elapsed}" "${PID}" "${rss_kb}" "${vmhwm_kb}" \
        "${proc_cpu_pct}" "${system_cpu_pct}" \
        "${api_streams_ms}" "${api_sessions_ms}" "${hls_ms}" "${snapshot_ms}" \
        "${main_cached_bytes}" "${sub_cached_bytes}" \
        "${main_hls_bytes}" "${sub_hls_bytes}" \
        "${main_cached_frames}" "${sub_cached_frames}" \
        "${main_reader_count}" "${sub_reader_count}" \
        "${main_client_count}" "${sub_client_count}" \
        "${sessions_pending}" "${sessions_count}" \
        "${webrtc_active}" "${http_flv_active}" "${mjpeg_active}" "${rtsp_active}" \
        "${rtsp_dropped}" "${webrtc_dropped}" "${webrtc_dropped_rtp}" \
        "${ai_dropped}" "$(CsvString "${main_reset}")" "$(CsvString "${sub_reset}")" \
        >>"${OUTPUT_DIR}/metrics.csv"
}

Main() {
    ParseArgs "$@"
    BASE_URL="$(NormalizeBaseUrl)"
    [[ "${STREAM}" == "main" || "${STREAM}" == "sub" ]] || Die "--stream must be main or sub"
    ValidateNumber "--duration" "${DURATION_SEC}"
    ValidateNumber "--interval" "${INTERVAL_SEC}"
    ValidateNumber "--hls-clients" "${HLS_CLIENTS}"
    ValidateNumber "--flv-clients" "${FLV_CLIENTS}"
    ValidateNumber "--mjpeg-clients" "${MJPEG_CLIENTS}"
    ValidateNumber "--webrtc-clients" "${WEBRTC_CLIENTS}"
    [[ "${INTERVAL_SEC}" != "0" ]] || Die "--interval must be greater than zero"
    HaveTool curl || Die "missing curl"

    if [[ -z "${OUTPUT_DIR}" ]]; then
        OUTPUT_DIR="/tmp/live_stream_hot_path_$(date +%Y%m%d-%H%M%S)"
    fi
    mkdir -p "${OUTPUT_DIR}/raw" || Die "cannot create ${OUTPUT_DIR}"
    : >"${OUTPUT_DIR}/clients.log"

    AutoDetectPid
    LoginIfNeeded
    WriteRunEnv

    CLIENT_PIDS=()
    trap StopClients EXIT INT TERM

    local i
    for ((i = 1; i <= HLS_CLIENTS; ++i)); do
        StartLoopClient hls "${i}" "${BASE_URL}/live/${STREAM}/hls/index.m3u8"
    done
    for ((i = 1; i <= FLV_CLIENTS; ++i)); do
        StartLoopClient flv "${i}" "${BASE_URL}/live/${STREAM}.live.flv"
    done
    for ((i = 1; i <= MJPEG_CLIENTS; ++i)); do
        StartLoopClient mjpeg "${i}" "${BASE_URL}/live/${STREAM}.mjpg"
    done
    for ((i = 1; i <= WEBRTC_CLIENTS; ++i)); do
        StartWebrtcClient "${i}" "${BASE_URL}/live/${STREAM}/whep"
    done

    CPU_COUNT="$(CpuCount)"
    PREV_PROC_TICKS="$(ReadProcTicks "${PID}")"
    read -r PREV_CPU_TOTAL PREV_CPU_IDLE < <(ReadCpuTotalIdle)

    printf '%s\n' \
        'timestamp_ms,elapsed_s,pid,rss_kb,vmhwm_kb,proc_cpu_pct,system_cpu_pct,api_streams_latency_ms,api_sessions_latency_ms,hls_playlist_latency_ms,snapshot_latency_ms,main_cached_bytes,sub_cached_bytes,main_hls_bytes,sub_hls_bytes,main_cached_frames,sub_cached_frames,main_reader_count,sub_reader_count,main_client_count,sub_client_count,sessions_pending_bytes,sessions_count,webrtc_active_peers,http_flv_active_clients,mjpeg_active_clients,rtsp_active_sessions,rtsp_dropped_frames,webrtc_dropped_frames,webrtc_dropped_rtp_packets,ai_dropped_tasks,main_last_reset_reason,sub_last_reset_reason' \
        >"${OUTPUT_DIR}/metrics.csv"

    local start_ms now_ms elapsed sample
    start_ms="$(NowMs)"
    sample=0
    while :; do
        now_ms="$(NowMs)"
        elapsed=$(( (now_ms - start_ms) / 1000 ))
        if (( elapsed > DURATION_SEC )); then
            break
        fi
        SampleOnce "${sample}" "${elapsed}"
        sample=$((sample + 1))
        sleep "${INTERVAL_SEC}"
    done

    StopClients
    printf 'board-hot-path-probe: wrote %s\n' "${OUTPUT_DIR}"
}

Main "$@"
