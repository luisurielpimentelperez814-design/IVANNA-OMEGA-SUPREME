#!/bin/bash
set -e

mkdir -p telemetry/history

if [ -f ivanna_oem_metrics.json ]; then

    cp ivanna_oem_metrics.json \
    telemetry/history/${GITHUB_SHA:-local}.json

fi

