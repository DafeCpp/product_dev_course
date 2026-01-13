#!/usr/bin/env bash
set -euo pipefail

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "❌ Required command not found: $1" >&2
    exit 1
  }
}

wait_url() {
  local url="$1"
  local name="$2"
  local attempts="${3:-60}"
  for _ in $(seq 1 "$attempts"); do
    if curl -sf "$url" >/dev/null 2>&1; then
      echo "✅ $name ok"
      return 0
    fi
    sleep 1
  done
  echo "❌ $name not ready: $url" >&2
  return 1
}

require_cmd curl
require_cmd python3
require_cmd docker-compose

echo "== wait services =="
wait_url "http://localhost:8001/health" "auth-service"
wait_url "http://localhost:8002/health" "experiment-service"
wait_url "http://localhost:8003/health" "telemetry-ingest-service"
wait_url "http://localhost:8080/health" "auth-proxy"

TS="$(date +%s)"
USERNAME="demo${TS}"
EMAIL="demo${TS}@example.com"
PASSWORD="demo12345"

echo "== auth: register ($USERNAME) =="
curl -sf -X POST http://localhost:8001/auth/register \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"${USERNAME}\",\"email\":\"${EMAIL}\",\"password\":\"${PASSWORD}\"}" >/dev/null

echo "== auth: login =="
LOGIN_JSON="$(curl -sf -X POST http://localhost:8001/auth/login \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}")"
ACCESS_TOKEN="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["access_token"])' <<<"$LOGIN_JSON")"

echo "== auth: me =="
ME_JSON="$(curl -sf -H "Authorization: Bearer ${ACCESS_TOKEN}" http://localhost:8001/auth/me)"
USER_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["id"])' <<<"$ME_JSON")"
echo "user_id=${USER_ID}"

echo "== auth: create project =="
PROJECT_JSON="$(curl -sf -X POST http://localhost:8001/projects \
  -H "Authorization: Bearer ${ACCESS_TOKEN}" \
  -H 'Content-Type: application/json' \
  -d "{\"name\":\"mvp-demo-${TS}\",\"description\":\"MVP demo project\"}")"
PROJECT_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["id"])' <<<"$PROJECT_JSON")"
echo "project_id=${PROJECT_ID}"

HDR_USER=(-H "X-User-Id: ${USER_ID}" -H "X-Project-Id: ${PROJECT_ID}" -H "X-Project-Role: owner")

echo "== experiment-service: create sensor =="
SENSOR_JSON="$(curl -sf -X POST http://localhost:8002/api/v1/sensors \
  "${HDR_USER[@]}" \
  -H 'Content-Type: application/json' \
  -H "Idempotency-Key: mvp-sensor-${TS}" \
  -d "{\"project_id\":\"${PROJECT_ID}\",\"name\":\"temperature_raw\",\"type\":\"thermocouple\",\"input_unit\":\"mV\",\"display_unit\":\"C\"}")"
SENSOR_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["sensor"]["id"])' <<<"$SENSOR_JSON")"
SENSOR_TOKEN="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["token"])' <<<"$SENSOR_JSON")"
echo "sensor_id=${SENSOR_ID}"

echo "== experiment-service: create experiment =="
EXP_JSON="$(curl -sf -X POST http://localhost:8002/api/v1/experiments \
  "${HDR_USER[@]}" \
  -H 'Content-Type: application/json' \
  -H "Idempotency-Key: mvp-exp-${TS}" \
  -d "{\"project_id\":\"${PROJECT_ID}\",\"owner_id\":\"${USER_ID}\",\"name\":\"MVP Demo Experiment ${TS}\",\"description\":\"demo\",\"experiment_type\":\"demo\",\"tags\":[\"mvp\"],\"metadata\":{\"demo\":true}}")"
EXPERIMENT_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["id"])' <<<"$EXP_JSON")"
echo "experiment_id=${EXPERIMENT_ID}"

echo "== experiment-service: create run =="
RUN_JSON="$(curl -sf -X POST "http://localhost:8002/api/v1/experiments/${EXPERIMENT_ID}/runs" \
  "${HDR_USER[@]}" \
  -H 'Content-Type: application/json' \
  -d "{\"experiment_id\":\"${EXPERIMENT_ID}\",\"project_id\":\"${PROJECT_ID}\",\"created_by\":\"${USER_ID}\",\"name\":\"run-1\",\"params\":{\"lr\":0.1},\"metadata\":{\"source\":\"mvp-demo-check\"}}")"
RUN_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["id"])' <<<"$RUN_JSON")"
echo "run_id=${RUN_ID}"

echo "== experiment-service: create capture session =="
CS_JSON="$(curl -sf -X POST "http://localhost:8002/api/v1/runs/${RUN_ID}/capture-sessions?project_id=${PROJECT_ID}" \
  "${HDR_USER[@]}" \
  -H 'Content-Type: application/json' \
  -d "{\"project_id\":\"${PROJECT_ID}\",\"run_id\":\"${RUN_ID}\",\"ordinal_number\":1,\"status\":\"running\",\"initiated_by\":\"${USER_ID}\"}")"
CAPTURE_SESSION_ID="$(python3 -c 'import sys,json; print(json.loads(sys.stdin.read())["id"])' <<<"$CS_JSON")"
echo "capture_session_id=${CAPTURE_SESSION_ID}"

echo "== telemetry-ingest-service: ingest telemetry =="
INGEST_JSON="$(curl -sf -X POST http://localhost:8003/api/v1/telemetry \
  -H "Authorization: Bearer ${SENSOR_TOKEN}" \
  -H 'Content-Type: application/json' \
  -d "{\"sensor_id\":\"${SENSOR_ID}\",\"run_id\":\"${RUN_ID}\",\"capture_session_id\":\"${CAPTURE_SESSION_ID}\",\"meta\":{\"source\":\"mvp-demo-check\"},\"readings\":[{\"timestamp\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"raw_value\":1.23,\"meta\":{\"step\":1}}]}")"
python3 -c 'import sys,json; print(json.loads(sys.stdin.read()))' <<<"$INGEST_JSON"

echo "== verify: sensor heartbeat updated =="
SENSOR_GET="$(curl -sf "http://localhost:8002/api/v1/sensors/${SENSOR_ID}?project_id=${PROJECT_ID}" "${HDR_USER[@]}")"
LAST_HEARTBEAT="$(python3 -c 'import sys,json; s=json.loads(sys.stdin.read()); print(s.get("last_heartbeat") or s.get("sensor",{}).get("last_heartbeat") or "")' <<<"$SENSOR_GET")"
if [[ -z "$LAST_HEARTBEAT" ]]; then
  echo "❌ last_heartbeat is empty" >&2
  exit 1
fi
echo "last_heartbeat=${LAST_HEARTBEAT}"

echo "== verify: telemetry_records rows in DB =="
SQL="select count(*) from telemetry_records where sensor_id='${SENSOR_ID}';"
ROWS="$(docker-compose exec -T postgres psql -U postgres -d experiment_db -t -c "$SQL" | tr -d ' ' | sed '/^$/d')"
echo "telemetry_records.count=${ROWS}"
python3 -c 'import sys; n=int(sys.argv[1]); raise SystemExit(0 if n>=1 else 1)' "$ROWS"

echo "✅ MVP demo acceptance check passed"
