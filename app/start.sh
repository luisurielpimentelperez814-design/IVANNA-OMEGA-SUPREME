#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
# Treat unset variables as an error.
set -Eeuo pipefail

# Ensure cleanup runs only once
CLEANUP_DONE=0

# Track background process IDs so we can terminate them cleanly
CONTROL_PLANE_PID=""
NGINX_PID=""
TAIL_PID=""

# This function is trapped and executed on SIGINT/SIGTERM to ensure clean shutdown.
cleanup() {
    if [[ "${CLEANUP_DONE:-0}" -eq 1 ]]; then
        return
    fi
    CLEANUP_DONE=1

    # Prevent recursive invocation if EXIT is also trapped
    trap - SIGINT SIGTERM EXIT

    echo "Caught signal or process exited, shutting down all child processes..."

    # Gracefully stop known background processes if they are still running
    if [[ -n "${NGINX_PID:-}" ]] && kill -0 "${NGINX_PID}" 2>/dev/null; then
        kill -TERM "${NGINX_PID}" 2>/dev/null || true
    fi

    if [[ -n "${CONTROL_PLANE_PID:-}" ]] && kill -0 "${CONTROL_PLANE_PID}" 2>/dev/null; then
        kill -TERM "${CONTROL_PLANE_PID}" 2>/dev/null || true
    fi

    if [[ -n "${TAIL_PID:-}" ]] && kill -0 "${TAIL_PID}" 2>/dev/null; then
        kill -TERM "${TAIL_PID}" 2>/dev/null || true
    fi

    echo "Waiting for all processes to terminate..."
    wait 2>/dev/null || true
    echo "Shutdown complete."
}

# Register the 'cleanup' function to be called on termination signals and on EXIT.
# Using EXIT ensures cleanup runs no matter how the script terminates.
trap 'cleanup' SIGINT SIGTERM

# Utility to wait for a service to become available over HTTP.
wait_for_http() {
    local url="$1"
    local timeout_seconds="${2:-120}"
    local interval_seconds="${3:-2}"
    local elapsed=0
    echo "Waiting for $url to become ready..."
    until curl -fsS "$url" > /dev/null 2>&1; do
        if [[ "$elapsed" -ge "$timeout_seconds" ]]; then
            echo "Timeout waiting for $url after ${timeout_seconds}s"
            return 1
        fi
        sleep "$interval_seconds"
        elapsed=$((elapsed + interval_seconds))
    done
    echo "$url is ready."
}

# --- Startup Sequence ---

# 1. Define environment variables.
: "${CONTROL_PLANE_PORT:=8000}"
: "${DEFAULT_APP_PORT:=3000}"
: "${APP_DIR:=/app/applet}"
: "${NGINX_PORT:=8080}"
: "${CSP_HEADER_VALUE:=}"

# Configure X-Forwarded-Host header for Nginx proxying based on framework type:
# - For Flutter Web, setting PROXY_FORWARDED_HOST_HEADER='$http_host' preserves the client's host and port
#   (e.g., 'localhost:8080') so DWDS constructs accurate WebSocket/SSE debug URLs inside client.js.
# - For other frameworks, we default to standard Nginx '$host', while keeping 'Host: localhost:${DEFAULT_APP_PORT}'
#   in nginx.conf to bypass host header validation checks in Node/Vite dev servers.
if [[ "${FLUTTER_FRAMEWORK:-false}" == "true" ]]; then
  PROXY_FORWARDED_HOST_HEADER='$http_host'
else
  PROXY_FORWARDED_HOST_HEADER='$host'
fi

sed -e "s~\${NGINX_PORT}~$NGINX_PORT~g" \
    -e "s~\${CONTROL_PLANE_PORT}~$CONTROL_PLANE_PORT~g" \
    -e "s~\${DEFAULT_APP_PORT}~$DEFAULT_APP_PORT~g" \
    -e "s~\${CSP_HEADER_VALUE}~$CSP_HEADER_VALUE~g" \
    -e "s~\${PROXY_FORWARDED_HOST_HEADER}~$PROXY_FORWARDED_HOST_HEADER~g" \
    /etc/nginx/nginx.conf.template > /etc/nginx/nginx.conf



mkdir -p /var/log/nginx

# 3. Start nginx in the background.
echo "Starting nginx..."
nginx -g 'daemon off;' &
NGINX_PID=$!

# 4. Start Go control-plane API in the background.
echo "Starting Control Plane API service..."
/app/control-plane-api/control-plane-api \
  --listen-addr=:${CONTROL_PLANE_PORT} \
  --app-dir=${APP_DIR} \
  --default-app-port=${DEFAULT_APP_PORT} &
CONTROL_PLANE_PID=$!

# 5. Wait for the control plane to become healthy.
wait_for_http "http://localhost:${CONTROL_PLANE_PORT}/health" 120 1 || { echo "Control plane failed to start"; cleanup; exit 1; }

# 6. Wait indefinitely. The trap handler will manage the shutdown.
echo "Control plane API started. Waiting for signal to shut down..."

# This child process will be killed by the cleanup trap.
tail -f /dev/null &
TAIL_PID=$!
wait "${TAIL_PID}"
