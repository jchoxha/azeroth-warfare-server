#!/usr/bin/env bash
# Azeroth Warfare: make an account (run inside the mangosd container while it is up):
#   docker compose exec mangosd /opt/aw/scripts/create-account.sh NAME PASSWORD [gmlevel]
# Uses mangosd's own console command so the SRP6 verifier is made the server's way.
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: create-account.sh NAME PASSWORD [gmlevel 0-6]"; exit 2; }
echo "Type these at the mangosd console (docker compose attach mangosd, detach with Ctrl-P Ctrl-Q):"
echo "  account create $1 $2"
[ -n "${3:-}" ] && echo "  account set gmlevel $1 $3"
