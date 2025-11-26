/**
 * @fileoverview This Google Cloud Function acts as an HTTP gateway for the Web NFC PWA.
 * It receives an RFID UID via a POST request and publishes it to a Google Cloud Pub/Sub
 * topic. This decouples the frontend from the core scrobbling logic.
 *
 * @author Your Name
 * @version 1.0.0
 */

const { PubSub } = require('@google-cloud/pubsub');

// Initialize the Google Cloud Pub/Sub client.
const pubSubClient = new PubSub();

// The target Pub/Sub topic that the main scrobbler function is subscribed to.
const topicName = 'vinyl/scrobble';

/**
 * An HTTP-triggered Cloud Function that receives an RFID UID and publishes it to Pub/Sub.
 * It handles CORS preflight requests and validates the incoming request.
 *
 * @param {object} req The Express-like request object from Google Cloud Functions.
 * @param {object} req.body The parsed JSON body of the request.
 * @param {string} req.body.rfid The RFID UID sent from the PWA.
 * @param {object} res The Express-like response object.
 * @returns {Promise<void>}
 */
exports.publishRfid = async (req, res) => {
    // Set CORS headers to allow requests from any origin.
    // For production, this could be restricted to a specific domain.
    res.set('Access-Control-Allow-Origin', '*');
    res.set('Access-Control-Allow-Methods', 'POST');
    res.set('Access-Control-Allow-Headers', 'Content-Type');

    // Respond to CORS preflight requests.
    if (req.method === 'OPTIONS') {
        res.status(204).send('');
        return;
    }

    // Ensure the request method is POST.
    if (req.method !== 'POST') {
        res.status(405).send({ error: 'Method Not Allowed' });
        return;
    }

    const { rfid } = req.body;

    if (!rfid) {
        console.log('Bad Request: No RFID value found in the request body.');
        res.status(400).send({ error: 'Missing RFID value in request body.' });
        return;
    }

    console.log(`Received RFID from web client: ${rfid}`);

    try {
        const dataBuffer = Buffer.from(rfid);
        const messageId = await pubSubClient.topic(topicName).publishMessage({ data: dataBuffer });
        console.log(`Message ${messageId} published to topic ${topicName}.`);
        res.status(200).send({ success: true, message: `Scrobble request sent for ${rfid}` });
    } catch (error) {
        console.error(`Fatal: Error publishing message to Pub/Sub: ${error.message}`, error);
        res.status(500).send({ error: 'Internal Server Error: Failed to publish message.' });
    }
};
