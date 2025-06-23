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
const topicName = 'vinyl/scrobble';

/**
 * HTTP-triggered function that publishes a message to a Pub/Sub topic.
 * 
 * @param {object} req Cloud Function request object.
 * @param {object} res Cloud Function response object.
 */
exports.publishRfid = async (req, res) => {
    // Allow CORS for requests from any origin (adjust in production if needed)
    res.set('Access-Control-Allow-Origin', '*');
    res.set('Access-Control-Allow-Methods', 'POST');
    res.set('Access-Control-Allow-Headers', 'Content-Type');

    // Handle CORS preflight requests
    if (req.method === 'OPTIONS') {
        res.status(204).send('');
        return;
    }

    // We only accept POST requests
    if (req.method !== 'POST') {
        return res.status(405).send({ error: 'Method Not Allowed' });
    }

    const rfid = req.body.rfid;

    if (!rfid) {
        console.log('No RFID value received in request body.');
        return res.status(400).send({ error: 'Missing RFID value in request body.' });
    }

    console.log(`Received RFID from web client: ${rfid}`);

    try {
        const dataBuffer = Buffer.from(rfid);
        const messageId = await pubSubClient.topic(topicName).publishMessage({ data: dataBuffer });
        console.log(`Message ${messageId} published to topic ${topicName}.`);
        res.status(200).send({ success: true, message: `Scrobble request sent for ${rfid}` });
    } catch (error) {
        console.error(`Error publishing message to Pub/Sub: ${error.message}`);
        res.status(500).send({ error: 'Failed to publish message.' });
    }
};