/*
 * Google Cloud Function - Vinyl Scrobbler
 * * Triggered by: Google Cloud Pub/Sub
 * * This function:
 * 1. Is triggered when a message (containing an RFID UID) is published to a Pub/Sub topic.
 * 2. Decodes the RFID UID from the message payload.
 * 3. Connects to Firestore using Admin credentials.
 * 4. Queries all user collections to find the album corresponding to the scanned RFID UID.
 * 5. If found, it fetches the album's tracklist from the Last.fm API.
 * 6. Scrobbles each track of the album to Last.fm using the `track.scrobble` API method.
 * * SETUP & DEPLOYMENT:
 * 1. Place this `index.js` and the `package.json` in a new directory.
 * 2. Run `npm install` in that directory.
 * 3. You MUST obtain a Last.fm API Key, Shared Secret, and a user Session Key.
 * - Get API Key/Secret here: https://www.last.fm/api/account/create
 * - Get a Session Key by completing the auth flow: https://www.last.fm/api/desktopauth
 * 4. Add your secrets to the Cloud Function's environment variables during deployment for security.
 * 5. Deploy the function using the gcloud CLI or the Google Cloud Console UI.
 */

const { Firestore } = require('@google-cloud/firestore');
const axios = require('axios');
const md5 = require('md5');

// Initialize Firestore client
const firestore = new Firestore();

// --- Last.fm Configuration --- 
// IMPORTANT: Do NOT hardcode these values. Use environment variables in your Cloud Function.
const LASTFM_API_KEY = process.env.LASTFM_API_KEY; // Your Last.fm API Key
const LASTFM_API_SECRET = process.env.LASTFM_API_SECRET; // Your Last.fm Shared Secret
const LASTFM_SESSION_KEY = process.env.LASTFM_SESSION_KEY; // Your user session key
const LASTFM_API_URL = 'https://ws.audioscrobbler.com/2.0/';

// --- Application Specific Configuration ---
const APP_ID = process.env.APP_ID || 'vinyl-scrobbler-dev'; // Used in Firestore path

/**
 * Main function triggered by a Pub/Sub message.
 * Expects a JSON payload with "rfid" and "userId".
 * @param {object} message The Pub/Sub message.
 * @param {object} context The event metadata.
 */
exports.scrobbleAlbum = async (message, context) => {
    let rfidUid, userId;

    // 1. Decode and parse the message data
    try {
        const messageDataString = message.data
            ? Buffer.from(message.data, 'base64').toString()
            : null;

        if (!messageDataString) {
            console.log('No data in Pub/Sub message.');
            return;
        }

        const messagePayload = JSON.parse(messageDataString);
        rfidUid = messagePayload.rfid;
        userId = messagePayload.userId;

        if (!rfidUid || typeof rfidUid !== 'string' || rfidUid.trim() === '') {
            console.log('Missing or invalid RFID UID in message payload.');
            return;
        }
        if (!userId || typeof userId !== 'string' || userId.trim() === '') {
            console.log('Missing or invalid userId in message payload.');
            return;
        }
        rfidUid = rfidUid.trim();
        userId = userId.trim();

        console.log(`Received RFID UID: ${rfidUid} for User ID: ${userId}`);

    } catch (error) {
        console.error('Failed to parse Pub/Sub message:', error);
        return; // Stop processing if message is malformed
    }

    try {
        // 2. Find the album in Firestore for the specific user
        const albumDetails = await findAlbumByRfidForUser(rfidUid, userId);

        if (!albumDetails) {
            console.log(`No album found in Firestore for RFID: ${rfidUid} and User ID: ${userId}`);
            return;
        }
        console.log(`Found album: ${albumDetails.artist} - ${albumDetails.album}`);

        // 3. Get the album's tracklist from Last.fm
        const tracks = await getAlbumTracks(albumDetails.artist, albumDetails.album);

        if (!tracks || tracks.length === 0) {
            console.log(`Could not find a tracklist for ${albumDetails.artist} - ${albumDetails.album}`);
            return;
        }
        console.log(`Found ${tracks.length} tracks. Preparing to scrobble.`);

        // 4. Scrobble each track to Last.fm
        // Note: The Last.fm session key (LASTFM_SESSION_KEY) determines which Last.fm user the tracks are scrobbled to.
        // This function currently uses a single, globally configured session key.
        // If scrobbles need to be per-Firebase-user, the session key management would need to be more complex (e.g., stored per user).
        await scrobbleTracks(tracks, albumDetails.artist, albumDetails.album);

        console.log('Successfully scrobbled all tracks.');

    } catch (error) {
        console.error(`An error occurred during the scrobbling process for RFID ${rfidUid}, User ${userId}:`, error);
    }
};

/**
 * Searches for a matching RFID tag in a specific user's 'albums' collection.
 * @param {string} rfidUid The RFID UID to search for.
 * @param {string} userId The Firebase User ID.
 * @returns {Promise<object|null>} The album data or null if not found.
 */
async function findAlbumByRfidForUser(rfidUid, userId) {
    const collectionPath = `artifacts/${APP_ID}/users/${userId}/albums`;
    console.log(`Searching for RFID ${rfidUid} in Firestore collection: ${collectionPath}`);

    const userAlbumsCollection = firestore.collection(collectionPath);
    const snapshot = await userAlbumsCollection.where('rfid', '==', rfidUid).limit(1).get();

    if (snapshot.empty) {
        return null;
    }

    const doc = snapshot.docs[0];
    return { id: doc.id, ...doc.data() };
}

/**
 * Fetches the full tracklist for an album from the Last.fm API.
 * @param {string} artist The artist name.
 * @param {string} album The album name.
 * @returns {Promise<Array<object>>} A list of track objects.
 */
async function getAlbumTracks(artist, album) {
    try {
        const response = await axios.get(LASTFM_API_URL, {
            params: {
                method: 'album.getinfo',
                api_key: LASTFM_API_KEY,
                artist: artist,
                album: album,
                format: 'json'
            }
        });

        if (response.data.error || !response.data.album || !response.data.album.tracks) {
            console.error('Last.fm API error while getting tracklist:', response.data.message);
            return [];
        }

        return response.data.album.tracks.track;
    } catch (error) {
        console.error('Error calling Last.fm album.getinfo:', error.message);
        return [];
    }
}

/**
 * Scrobbles an array of tracks to Last.fm.
 * @param {Array<object>} tracks Array of track objects from album.getinfo.
 * @param {string} artist The artist name.
 * @param {string} album The album name.
 */
async function scrobbleTracks(tracks, artist, album) {
    const timestamps = [];
    const now = Math.floor(Date.now() / 1000);
    // Create timestamps going backwards in time from now, one for each track
    for (let i = 0; i < tracks.length; i++) {
        // Assuming average track length of 3 minutes (180s) for timestamp separation
        timestamps.push(now - (tracks.length - i - 1) * 180);
    }

    const params = {
        method: 'track.scrobble',
        sk: LASTFM_SESSION_KEY,
        api_key: LASTFM_API_KEY
    };

    // Last.fm API requires parameters for each track indexed with []
    tracks.forEach((track, i) => {
        params[`artist[${i}]`] = artist;
        params[`album[${i}]`] = album;
        params[`track[${i}]`] = track.name;
        params[`timestamp[${i}]`] = timestamps[i];
    });

    // Generate the API signature
    params.api_sig = generateApiSignature(params);
    params.format = 'json';

    try {
        const response = await axios.post(LASTFM_API_URL, new URLSearchParams(params).toString(), {
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        });

        if (response.data.error) {
            console.error('Last.fm scrobble API error:', response.data.message);
            throw new Error(response.data.message);
        }

        console.log('Scrobble successful:', response.data.scrobbles);
    } catch (error) {
        console.error('Error calling Last.fm track.scrobble:', error.message);
        throw error;
    }
}

/**
 * Generates the MD5 signature required for authenticated Last.fm API calls.
 * @param {object} params The parameters for the API call.
 * @returns {string} The MD5 hash signature.
 */
function generateApiSignature(params) {
    // Sort keys alphabetically
    const sortedKeys = Object.keys(params).sort();

    let signatureString = '';
    sortedKeys.forEach(key => {
        signatureString += key + params[key];
    });

    signatureString += LASTFM_API_SECRET;

    return md5(signatureString);
}
