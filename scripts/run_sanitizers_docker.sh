#!/usr/bin/env bash
# Runs scripts/run_sanitizers.sh inside a clean Ubuntu container.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/src:ro ubuntu:24.04 bash -c '
  set -e
  apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ cmake make >/dev/null
  cp -r /src /work && cd /work && rm -rf build build-*
  scripts/run_sanitizers.sh'
