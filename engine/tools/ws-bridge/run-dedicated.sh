#!/usr/bin/env bash
# Dedicated lobby server for the ws-bridge demo (fixture data, no GL needed).
# Command bursts from tools/ws-bridge/wc3.cmds1 (lobby open + config) and
# wc3.cmds2 (slots + config re-assert). NOTE: the engine's console does not
# reliably apply multi-line command batches from a single write - commands are
# therefore re-emitted ONE PER WRITE with a small delay (wc3.cmds2 loop).
# startmatch fires on a timer so a joining client can take a slot first.
# Run from the repo root, --network=host, CI image:
#   docker run -d --name wc3-dedicated --network=host \
#     -v "$PWD":/src -w /src/open-realm-em \
#     ghcr.io/corepunch/open-realm-ci:ubuntu-24.04 \
#     bash /src/open-realm-em/tools/ws-bridge/run-dedicated.sh
{
  sleep 3
  cat /src/open-realm-em/tools/ws-bridge/wc3.cmds1
  sleep 2
  while IFS= read -r wc3line; do
    echo "$wc3line"
    sleep 0.5
  done < /src/open-realm-em/tools/ws-bridge/wc3.cmds2
  sleep 86
  echo 'startmatch Maps\Transport.w3m'
  sleep 86400
} | exec ./build/bin/openwarcraft3 -data /src/webdata +dedicated 1 +game_port 27960 +map 'Maps\Transport.w3m'
