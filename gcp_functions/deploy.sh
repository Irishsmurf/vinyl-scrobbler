#!/usr/bin/env bash
#
# Deploy the Vinyl Scrobbler Cloud Functions (Gen 2).
#
# Usage:
#   ./deploy.sh                 # deploy both functions
#   ./deploy.sh scrobble        # deploy only ScrobbleAlbum
#   ./deploy.sh gateway         # deploy only web_nfc_gateway
#
# Config (override via env vars):
#   REGION   GCP region for both functions      (default: us-central1)
#   TOPIC    Pub/Sub topic connecting them      (default: vinyl-scrobble)
#   RUNTIME  Node.js runtime                     (default: nodejs22)
#
# Prerequisites:
#   - gcloud CLI authenticated on the correct project. This repo ships a
#     `vinyl-scrobbler` gcloud config profile (see README); `direnv` activates
#     it automatically. Verify with: gcloud config list
#   - For ScrobbleAlbum: copy scrobble_album/.env.yaml.example to
#     scrobble_album/.env.yaml and fill in your Last.fm credentials.

set -euo pipefail

REGION="${REGION:-us-central1}"
TOPIC="${TOPIC:-vinyl-scrobble}"
RUNTIME="${RUNTIME:-nodejs22}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ensure_topic() {
  if ! gcloud pubsub topics describe "$TOPIC" >/dev/null 2>&1; then
    echo "Creating Pub/Sub topic: $TOPIC"
    gcloud pubsub topics create "$TOPIC"
  fi
}

deploy_scrobble() {
  local env_file="$SCRIPT_DIR/scrobble_album/.env.yaml"
  if [[ ! -f "$env_file" ]]; then
    echo "ERROR: $env_file not found." >&2
    echo "Copy scrobble_album/.env.yaml.example to .env.yaml and add your Last.fm credentials." >&2
    exit 1
  fi
  ensure_topic
  echo "Deploying ScrobbleAlbum -> topic '$TOPIC' in $REGION ..."
  gcloud functions deploy ScrobbleAlbum \
    --gen2 \
    --region="$REGION" \
    --runtime="$RUNTIME" \
    --source="$SCRIPT_DIR/scrobble_album" \
    --entry-point=helloPubSub \
    --trigger-topic="$TOPIC" \
    --env-vars-file="$env_file"
}

deploy_gateway() {
  ensure_topic
  echo "Deploying web_nfc_gateway (HTTP) in $REGION ..."
  gcloud functions deploy web_nfc_gateway \
    --gen2 \
    --region="$REGION" \
    --runtime="$RUNTIME" \
    --source="$SCRIPT_DIR/web_nfc_gateway" \
    --entry-point=publishRfid \
    --trigger-http \
    --allow-unauthenticated
  echo
  echo "Gateway URL (put this in docs/index.html -> CLOUD_FUNCTION_URL):"
  gcloud functions describe web_nfc_gateway --region="$REGION" \
    --format="value(serviceConfig.uri)"
}

case "${1:-all}" in
  scrobble) deploy_scrobble ;;
  gateway)  deploy_gateway ;;
  all)      deploy_gateway; deploy_scrobble ;;
  *) echo "Usage: $0 [all|scrobble|gateway]" >&2; exit 1 ;;
esac

echo "Done."
