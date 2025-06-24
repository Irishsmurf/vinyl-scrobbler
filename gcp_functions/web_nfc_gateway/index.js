/*
 * Google Cloud Function - Web Scrobbler Gateway
 * 
 * Triggered by: HTTP Request
 * 
 * This function:
 * 1. Is triggered by a POST request from the Web NFC client.
 * 2. Expects a JSON body with an 'rfid' key, like { \"rfid\": \"AB CD 12 34\" }.
 * 3. Takes the RFID value and publishes it to the same Pub/Sub topic the ESP32 uses.
 * 4. This decouples the web client from the main scrobbling logic.
 */

const { PubSub } = require('@google-cloud/pubsub');

// Initialize Pub/Sub client
const pubSubClient = new PubSub();

// The name of the Pub/Sub topic your main function listens to
const topicName = process.env.PUBSUB_TOPIC || 'vinyl/scrobble'; // Made topic configurable
const expectedApiKey = process.env.WEB_NFC_GATEWAY_API_KEY; // API Key for this function
const webGatewayUserId = process.env.WEB_GATEWAY_USER_ID; // Predefined User ID for scrobbles from this gateway

/**
 * HTTP-triggered function that publishes a message to a Pub/Sub topic.
 * Requires X-API-Key header for authentication.
 * @param {object} req Cloud Function request object.
 * @param {object} res Cloud Function response object.
 */
exports.publishRfid = async (req, res) => {
    // IMPORTANT: In a production environment, restrict this to known origins.
    res.set('Access-Control-Allow-Origin', '*');
    res.set('Access-Control-Allow-Methods', 'POST, OPTIONS');
    res.set('Access-Control-Allow-Headers', 'Content-Type, X-API-Key');

    // Handle CORS preflight requests
    if (req.method === 'OPTIONS') {
        res.status(204).send('');
        return;
    }

    // We only accept POST requests
    if (req.method !== 'POST') {
        return res.status(405).send({ error: 'Method Not Allowed' });
    }

    // --- API Key Authentication ---
    const apiKey = req.get('X-API-Key');
    if (!expectedApiKey) {
        console.error('CRITICAL: API Key is not configured for the function. Denying all requests.');
        return res.status(500).send({ error: 'Internal Server Error - Configuration issue.' });
    }
    if (!apiKey || apiKey !== expectedApiKey) {
        console.warn('Invalid or missing API Key received.');
        return res.status(401).send({ error: 'Unauthorized' });
    }

    // --- Input Validation ---
    const rfid = req.body.rfid;
    if (!rfid || typeof rfid !== 'string' || rfid.trim() === '') {
        console.log('No or invalid RFID value received in request body.');
        return res.status(400).send({ error: 'Missing or invalid RFID value in request body.' });
    }
    const sanitizedRfid = rfid.trim();

    if (!webGatewayUserId) {
        console.error('CRITICAL: WEB_GATEWAY_USER_ID is not configured. Cannot publish message.');
        return res.status(500).send({ error: 'Internal Server Error - Configuration issue.'});
    }

    console.log(`Received RFID from web client: ${sanitizedRfid}`);

    // --- Prepare Pub/Sub Message ---
    const messagePayload = {
        rfid: sanitizedRfid,
        userId: webGatewayUserId  // Using the predefined User ID for this gateway
    };

    try {
        const dataBuffer = Buffer.from(JSON.stringify(messagePayload));
        const messageId = await pubSubClient.topic(topicName).publishMessage({ data: dataBuffer });
        console.log(`Message ${messageId} published to topic ${topicName}. Payload: ${JSON.stringify(messagePayload)}`);
        res.status(200).send({ success: true, message: `Scrobble request sent for ${sanitizedRfid}` });
    } catch (error) {
        console.error(`Error publishing message to Pub/Sub: ${error.message}`);
        res.status(500).send({ error: 'Failed to publish message.' });
    }
};