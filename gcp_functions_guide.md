# Vinyl Scrobbler Cloud Functions — Deploy & Test Guide

The backend is two **Gen 2** Google Cloud Functions that talk to each other over a
single Pub/Sub topic:

| Function          | Trigger        | Entry point   | Purpose                                                        |
| ----------------- | -------------- | ------------- | ------------------------------------------------------------- |
| `web_nfc_gateway` | HTTP           | `publishRfid` | Receives an RFID UID from the PWA, publishes it to Pub/Sub.   |
| `ScrobbleAlbum`   | Pub/Sub topic  | `helloPubSub` | Looks the UID up in Firestore and scrobbles it to Last.fm.    |

**Shared settings** (keep these consistent everywhere):

| Setting | Value           |
| ------- | --------------- |
| Project | `rfid-album-scrobblr` |
| Region  | `us-central1`   |
| Runtime | `nodejs20`      |
| Topic   | `vinyl-scrobble`|

> **Note:** the topic name has no slashes — `vinyl-scrobble`, not `vinyl/scrobble`.
> Pub/Sub topic IDs may only contain letters, numbers, dashes, dots and underscores.

---

## 🚀 Deploy (the easy way)

A single script deploys both functions with the correct settings.

1. **Authenticate.** This repo ships a dedicated `gcloud` config profile named
   `vinyl-scrobbler`; [`direnv`](https://direnv.net/) activates it automatically via
   `.envrc`. Confirm you're on the right project:
   ```bash
   gcloud config list        # project should be rfid-album-scrobblr
   ```

2. **Provide the Last.fm credentials** for `ScrobbleAlbum`:
   ```bash
   cd gcp_functions/scrobble_album
   cp .env.yaml.example .env.yaml   # .env.yaml is git-ignored
   # edit .env.yaml and fill in the three values
   cd ../..
   ```

3. **Deploy:**
   ```bash
   cd gcp_functions
   ./deploy.sh            # both functions (creates the topic if needed)
   # or individually:
   ./deploy.sh gateway    # just web_nfc_gateway (prints its HTTPS URL)
   ./deploy.sh scrobble   # just ScrobbleAlbum
   ```
   Override defaults with env vars if ever needed, e.g. `REGION=europe-west2 ./deploy.sh`.

4. **Wire up the PWA.** After deploying the gateway, copy the printed HTTPS URL into
   the `CLOUD_FUNCTION_URL` constant in
   [docs/index.html](file:///home/paddez/dev/vinyl-scrobbler/docs/index.html), then
   commit and push.

That's it. To update a function later, just edit its code and re-run `./deploy.sh`.

---

## 🧪 Test locally before deploying

The functions use the `@google-cloud/functions-framework`, so they run as local HTTP
servers.

**Credentials for local runs:** the SDKs authenticate via Application Default
Credentials. Log in once:
```bash
gcloud auth application-default login
```
For `ScrobbleAlbum`, also export the Last.fm variables in your shell (or `source`
them from your `.env.yaml`):
```bash
export LASTFM_API_KEY="..."
export LASTFM_API_SECRET="..."
export LASTFM_SESSION_KEY="..."
```

### `web_nfc_gateway` (HTTP)
```bash
cd gcp_functions/web_nfc_gateway
npm install
npm start        # http://localhost:8080
```
In another terminal:
```bash
curl -X POST http://localhost:8080 \
  -H "Content-Type: application/json" \
  -d '{"rfid": "12 34 56 78"}'
# -> {"success":true,"message":"Scrobble request sent for 12 34 56 78"}
```

### `ScrobbleAlbum` (Pub/Sub / CloudEvent)
```bash
cd gcp_functions/scrobble_album
npm install
npm start        # http://localhost:8083 (see the "start" script in package.json)
```
It expects a CloudEvent whose `data.message.data` is the **Base64-encoded** UID.
`"12 34 56 78"` in Base64 is `MTIgMzQgNTYgNzg=`:
```bash
curl -X POST http://localhost:8083 \
  -H "Content-Type: application/json" \
  -d '{
    "specversion": "1.0",
    "type": "google.cloud.pubsub.topic.v1.messagePublished",
    "source": "//pubsub.googleapis.com/projects/rfid-album-scrobblr/topics/vinyl-scrobble",
    "id": "123456",
    "time": "2026-07-02T18:48:00Z",
    "datacontenttype": "application/json",
    "data": { "message": { "data": "MTIgMzQgNTYgNzg=", "messageId": "1" } }
  }'
```

---

## 🔧 Manual deploy (reference)

The script wraps these commands — reach for them only if you need to deploy by hand.

```bash
# ScrobbleAlbum (Pub/Sub trigger)
gcloud functions deploy ScrobbleAlbum \
  --gen2 --region=us-central1 --runtime=nodejs20 \
  --source=gcp_functions/scrobble_album \
  --entry-point=helloPubSub \
  --trigger-topic=vinyl-scrobble \
  --env-vars-file=gcp_functions/scrobble_album/.env.yaml

# web_nfc_gateway (HTTP trigger)
gcloud functions deploy web_nfc_gateway \
  --gen2 --region=us-central1 --runtime=nodejs20 \
  --source=gcp_functions/web_nfc_gateway \
  --entry-point=publishRfid \
  --trigger-http --allow-unauthenticated
```

---

## 🔒 IAM & Firestore

- The Gen 2 runtime service account (by default the Compute service account,
  `PROJECT_NUMBER-compute@developer.gserviceaccount.com`) needs the
  **Cloud Datastore User** role so `ScrobbleAlbum` can query Firestore.
- The first scrobble may fail with `FAILED_PRECONDITION` if the collection-group
  composite index doesn't exist yet — the error in the logs contains a direct link
  to create it.

## 🧹 Housekeeping

Old/failed deployments linger and cause confusion. List what actually exists and
remove stragglers:
```bash
gcloud functions list --gen2
# delete a stale one (note --region):
gcloud functions delete ScrobbleAlbum --gen2 --region=europe-west2
```
