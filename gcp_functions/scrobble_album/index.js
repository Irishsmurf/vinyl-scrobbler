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
 * - The provided "lastfm-sk-generator" script can generate the Session Key.
 * 4. Add your secrets to the Cloud Function's environment variables during deployment for security.
 * 5. Deploy the function using the gcloud CLI or the Google Cloud Console UI.
 */

const { Firestore } = require('@google-cloud/firestore');
const functions = require('@google-cloud/functions-framework');
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

/**
 * Main function triggered by a Pub/Sub message.
 * This function is now resilient to both legacy and Eventarc Pub/Sub trigger formats.
 * @param {object} cloudEvent The event metadata and data payload.
 */
// CORRECTED: Made the function handler `async` to allow for `await`.
functions.cloudEvent('helloPubSub', async (cloudEvent) => {
    // The Pub/Sub message is passed as the CloudEvent's data payload.
    const base64Uid = cloudEvent.data.message.data;
    console.log(`Received CloudEvent: ${base64Uid}`);

    const rfidUid = base64Uid
        ? Buffer.from(base64Uid, 'base64').toString().trim()
        : null;

    if (!rfidUid) {
        console.log('Decoded RFID UID is empty. Aborting.');
        return;
    }
    console.log(`Received and decoded RFID UID: ${rfidUid}`);

    try {
        // 2. Find the album in Firestore
        // CORRECTED: Used `await` to ensure the function waits for the Firestore query to complete.
        const albumDetails = await findAlbumByRfid(rfidUid);

        if (!albumDetails) {
            console.log(`No album found in Firestore for RFID: ${rfidUid}`);
            return;
        }
        console.log(`Found album: ${albumDetails.artist} - ${albumDetails.album}`);

        // 3. Get the album's tracklist from Last.fm
        // CORRECTED: Used `await` to wait for the Last.fm API call.
        const tracks = await getAlbumTracks(albumDetails.artist, albumDetails.album);

        if (!tracks || tracks.length === 0) {
            console.log(`Could not find a tracklist for ${albumDetails.artist} - ${albumDetails.album}`);
            return;
        }
        console.log(`Found ${tracks.length} tracks. Preparing to scrobble.`);

        // 4. Scrobble each track to Last.fm
        // CORRECTED: Used `await` to ensure the scrobbling process finishes before the function exits.
        await scrobbleTracks(tracks, albumDetails.artist, albumDetails.album);

        console.log('Successfully scrobbled all tracks.');

    } catch (error) {
        console.error('An error occurred during the scrobbling process:', error);
        // Re-throw the error to ensure the message is nacked and retried.
        throw error;
    }
});

/**
 * Searches across all user-specific 'albums' collections for a matching RFID tag.
 * @param {string} rfidUid The RFID UID to search for.
 * @returns {Promise<object|null>} The album data or null if not found.
 */
async function findAlbumByRfid(rfidUid) {
    console.log(`Searching for RFID '${rfidUid}' in Firestore...`);
    // Use a collectionGroup query to search across all subcollections named 'albums'.
    const albumsCollectionGroup = firestore.collectionGroup('albums');
    const snapshot = await albumsCollectionGroup.where('rfid', '==', rfidUid).limit(1).get();

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

        if (response.data.error || !response.data.album || !response.data.album.tracks || !response.data.album.tracks.track) {
            console.error('Last.fm API error while getting tracklist:', response.data.message || 'No track data found.');
            return [];
        }

        // Ensure tracks.track is always an array, even for single-track albums
        if (!Array.isArray(response.data.album.tracks.track)) {
            return [response.data.album.tracks.track];
        }
        return response.data.album.tracks.track;
    } catch (error) {
        console.error(`Error calling Last.fm album.getinfo for ${artist} - ${album}:`, error.message);
        // If the error is from Axios and contains response data, log it.
        if (error.response) {
            console.error('API Response Data:', error.response.data);
        }
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

    // Last.fm API requires parameters for each track to be indexed with []
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
        // Use URLSearchParams to correctly format the body for application/x-www-form-urlencoded
        const response = await axios.post(LASTFM_API_URL, new URLSearchParams(params).toString(), {
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        });

        if (response.data.error) {
            console.error('Last.fm scrobble API error:', response.data.message);
            throw new Error(`Last.fm scrobble API error (${response.data.error}): ${response.data.message}`);
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
    // Exclude 'format' from signature generation, as per some interpretations of the docs
    const paramsForSig = { ...params };
    delete paramsForSig.format;

    // Sort keys alphabetically
    const sortedKeys = Object.keys(paramsForSig).sort();

    let signatureString = '';
    sortedKeys.forEach(key => {
        signatureString += key + paramsForSig[key];
    });

    signatureString += LASTFM_API_SECRET;

    return md5(signatureString);
}
