# Vinyl Scrobbler Cloud Functions Deployment & Testing Guide

This guide explains how to test the Google Cloud Functions locally using the Google Cloud Functions Framework and how to deploy them to Google Cloud Platform (GCP).

---

## 🛠️ Testing Locally Before Deployment

You can run and test both functions locally on your machine. We have configured `package.json` scripts to run them via the `@google-cloud/functions-framework`.

### 1. Prerequisites for Local Testing

Because these functions interact with Google Cloud services (Firestore and Pub/Sub), your local environment needs credentials to authenticate with GCP.

1. **Authenticate using Application Default Credentials (ADC):**
   ```bash
   gcloud auth application-default login
   ```
   This command opens a browser to log in and saves credentials locally that the Google Cloud SDKs (`@google-cloud/firestore` and `@google-cloud/pubsub`) will automatically detect.

2. **Ensure correct environment variables:**
   The `scrobble_album` function requires Last.fm credentials. Export them in your terminal before running:
   ```bash
   export LASTFM_API_KEY="your_lastfm_api_key"
   export LASTFM_API_SECRET="your_lastfm_secret"
   export LASTFM_SESSION_KEY="your_lastfm_session_key"
   ```

---

### 2. Testing the `web_nfc_gateway` Function

This is an **HTTP-triggered function** that receives an RFID UID and publishes it to a Pub/Sub topic.

#### Step A: Run the function locally
1. Navigate to the directory:
   ```bash
   cd gcp_functions/web_nfc_gateway
   ```
2. Install dependencies:
   ```bash
   npm install
   ```
3. Start the function:
   ```bash
   npm start
   ```
   This will start the server on `http://localhost:8080`.

#### Step B: Test with a mock HTTP request
Open a separate terminal and send a POST request with `curl`:
```bash
curl -X POST http://localhost:8080 \
  -H "Content-Type: application/json" \
  -d '{"rfid": "12 34 56 78"}'
```

**Expected Response:**
```json
{"success":true,"message":"Scrobble request sent for 12 34 56 78"}
```

---

### 3. Testing the `scrobble_album` Function

This is a **Pub/Sub-triggered function** (CloudEvent signature).

#### Step A: Run the function locally
1. Navigate to the directory:
   ```bash
   cd gcp_functions/scrobble_album
   ```
2. Install dependencies:
   ```bash
   npm install
   ```
3. Start the function:
   ```bash
   npm start
   ```
   This will start the server on `http://localhost:8080` (or the configured port).

#### Step B: Test with a mock CloudEvent payload
Because it's a Pub/Sub event, the local server expects a CloudEvent POST request. Send a POST request representing the Pub/Sub message.
> [!NOTE]
> The `data.message.data` field must be a Base64-encoded representation of the RFID UID.
> For example, `"12 34 56 78"` in Base64 is `MTIgMzQgNTYgNzg=`.

```bash
curl -X POST http://localhost:8080 \
  -H "Content-Type: application/json" \
  -d '{
    "specversion": "1.0",
    "type": "google.cloud.pubsub.topic.v1.messagePublished",
    "source": "//pubsub.googleapis.com/projects/mock-project/topics/vinyl-scrobble",
    "id": "123456",
    "time": "2026-07-02T18:48:00Z",
    "datacontenttype": "application/json",
    "data": {
      "message": {
        "data": "MTIgMzQgNTYgNzg=",
        "messageId": "1"
      }
    }
  }'
```

---

## 🚀 Deploying to Google Cloud Platform

Deploy the functions using the `gcloud` CLI. Make sure you have selected the correct GCP project:
```bash
gcloud config set project your-gcp-project-id
```

### 1. Create the Pub/Sub Topic
Before deploying the functions, ensure the target Pub/Sub topic exists:
```bash
gcloud pubsub topics create vinyl/scrobble
```

### 2. Deploy `web_nfc_gateway` (HTTP Trigger)
This function acts as the public entry point for your mobile PWA.

1. Navigate to the function folder:
   ```bash
   cd gcp_functions/web_nfc_gateway
   ```
2. Deploy the function:
   ```bash
   gcloud functions deploy web_nfc_gateway \
     --gen2 \
     --runtime=nodejs18 \
     --region=us-central1 \
     --trigger-http \
     --allow-unauthenticated \
     --entry-point=publishRfid
   ```
3. **Capture the URL:** After deployment completes, the CLI will output an `httpsTrigger` URL (e.g., `https://web-nfc-gateway-xxxxx-uc.a.run.app`). Copy this URL and update the `CLOUD_FUNCTION_URL` constant inside [docs/index.html](file:///home/paddez/dev/vinyl-scrobbler/docs/index.html).

### 3. Deploy `scrobble_album` (Pub/Sub Trigger)
This function performs the lookup in Firestore and scrobbles to Last.fm.

1. Navigate to the function folder:
   ```bash
   cd gcp_functions/scrobble_album
   ```
2. Deploy the function:
   ```bash
   gcloud functions deploy scrobble_album \
     --gen2 \
     --runtime=nodejs18 \
     --region=us-central1 \
     --trigger-topic=vinyl/scrobble \
     --entry-point=helloPubSub \
     --set-env-vars LASTFM_API_KEY="your_api_key",LASTFM_API_SECRET="your_shared_secret",LASTFM_SESSION_KEY="your_session_key"
   ```

---

## 🔒 Firestore Permissions Check
Ensure the service account assigned to the `scrobble_album` function (usually the Default Compute Service Account `PROJECT_NUMBER-compute@developer.gserviceaccount.com` for Gen 2 functions) has the **Cloud Datastore User** or **Owner** IAM role, allowing it to query your Firestore collections.
