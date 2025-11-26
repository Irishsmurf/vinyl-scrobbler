/**
 * @fileoverview This Google Cloud Function is the core of the Vinyl Scrobbler backend.
 * It is triggered by a message on a Google Cloud Pub/Sub topic, which contains an
 * RFID UID. The function then finds the corresponding album in Firestore, fetches its
 * tracklist from the Last.fm API, and scrobbles each track to the user's Last.fm profile.
 *
 * @author Your Name
 * @version 1.0.0
 */

const { Firestore } = require('@google-cloud/firestore');
const functions = require('@google-cloud/functions-framework');
const axios = require('axios');
const md5 = require('md5');

// Initialize Firestore client for database interactions.
const firestore = new Firestore();

// --- Last.fm API Configuration ---
// These credentials should be set as environment variables in the Google Cloud Function settings.
const LASTFM_API_KEY = process.env.LASTFM_API_KEY;
const LASTFM_API_SECRET = process.env.LASTFM_API_SECRET;
const LASTFM_SESSION_KEY = process.env.LASTFM_SESSION_KEY;
const LASTFM_API_URL = 'https://ws.audioscrobbler.com/2.0/';

/**
 * The main entry point for the Cloud Function, triggered by a Pub/Sub message.
 * It orchestrates the process of decoding the UID, finding the album, fetching tracks, and scrobbling.
 * @param {object} cloudEvent The event payload from Pub/Sub.
 * @param {object} cloudEvent.data The data object containing the message.
 * @param {object} cloudEvent.data.message The message object.
 * @param {string} cloudEvent.data.message.data The base64-encoded RFID UID.
 * @returns {Promise<void>}
 * @throws {Error} Throws an error if any step in the process fails, ensuring message retries.
 */
functions.cloudEvent('helloPubSub', async (cloudEvent) => {
    const base64Uid = cloudEvent.data.message.data;
    if (!base64Uid) {
        console.log('Received an empty message. Aborting.');
        return;
    }

    const rfidUid = Buffer.from(base64Uid, 'base64').toString().trim();
    if (!rfidUid) {
        console.log('Decoded RFID UID is empty. Aborting.');
        return;
    }
    console.log(`Received and decoded RFID UID: ${rfidUid}`);

    try {
        const albumDetails = await findAlbumByRfid(rfidUid);
        if (!albumDetails) {
            console.log(`No album found in Firestore for RFID: ${rfidUid}`);
            return;
        }
        console.log(`Found album: ${albumDetails.artist} - ${albumDetails.album}`);

        const tracks = await getAlbumTracks(albumDetails.artist, albumDetails.album);
        if (!tracks || tracks.length === 0) {
            console.log(`Could not find a tracklist for ${albumDetails.artist} - ${albumDetails.album}`);
            return;
        }
        console.log(`Found ${tracks.length} tracks. Preparing to scrobble.`);

        await scrobbleTracks(tracks, albumDetails.artist, albumDetails.album);

    } catch (error) {
        console.error('An error occurred during the scrobbling process:', error);
        throw error; // Re-throw to trigger Pub/Sub retries.
    }
});

/**
 * Searches across all 'albums' collection groups in Firestore for a document
 * with a matching 'rfid' field.
 * @param {string} rfidUid The RFID UID to search for.
 * @returns {Promise<object|null>} A promise that resolves to the album data object
 *   (including its ID) or null if no match is found.
 * @throws {Error} Throws an error if the Firestore query fails, especially if a required
 *   composite index is missing.
 */
async function findAlbumByRfid(rfidUid) {
    console.log(`Searching for RFID '${rfidUid}' in Firestore...`);
    const albumsCollectionGroup = firestore.collectionGroup('albums');
    
    try {
        const snapshot = await albumsCollectionGroup.where('rfid', '==', rfidUid).limit(1).get();

        if (snapshot.empty) {
            return null;
        }

        const doc = snapshot.docs[0];
        return { id: doc.id, ...doc.data() };
    } catch (error) {
        if (error.code === 9) { // FAILED_PRECONDITION
            const helpfulMessage = `A Firestore index is required for this query. Please create a composite index for the 'albums' collection group on the 'rfid' field. Original error: "${error.details}"`;
            console.error(helpfulMessage);
            throw new Error(helpfulMessage, { cause: error });
        }
        throw error;
    }
}

/**
 * Fetches the full tracklist for a given album from the Last.fm API.
 * @param {string} artist The name of the artist.
 * @param {string} album The title of the album.
 * @returns {Promise<Array<object>>} A promise that resolves to an array of track objects
 *   from the Last.fm API. Returns an empty array if not found or in case of an error.
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

        const tracks = response.data?.album?.tracks?.track;
        if (!tracks) {
            console.error('Last.fm API error: No track data found in response.', response.data);
            return [];
        }

        return Array.isArray(tracks) ? tracks : [tracks]; // Handle single-track albums
    } catch (error) {
        console.error(`Error calling Last.fm album.getinfo for ${artist} - ${album}:`, error.message);
        if (error.response) console.error('API Response Data:', error.response.data);
        return [];
    }
}

/**
 * Scrobbles an array of tracks to a user's Last.fm profile. It calculates timestamps
 * for each track to ensure they appear in the correct order.
 * @param {Array<object>} tracks An array of track objects from the Last.fm API.
 * @param {string} artist The name of the artist for all tracks.
 * @param {string} album The title of the album for all tracks.
 * @returns {Promise<void>}
 * @throws {Error} Throws an error if the Last.fm API call fails.
 */
async function scrobbleTracks(tracks, artist, album) {
    const now = Math.floor(Date.now() / 1000);
    const timestamps = tracks.map((_, i) => now - (tracks.length - i - 1) * 180); // ~3 min per track

    const params = {
        method: 'track.scrobble',
        sk: LASTFM_SESSION_KEY,
        api_key: LASTFM_API_KEY
    };

    tracks.forEach((track, i) => {
        params[`artist[${i}]`] = artist;
        params[`album[${i}]`] = album;
        params[`track[${i}]`] = track.name;
        params[`timestamp[${i}]`] = timestamps[i];
    });

    params.api_sig = generateApiSignature(params);
    params.format = 'json';

    try {
        const response = await axios.post(LASTFM_API_URL, new URLSearchParams(params).toString(), {
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        });

        if (response.data.error) {
            throw new Error(`Last.fm scrobble API error (${response.data.error}): ${response.data.message}`);
        }
        
        const scrobbles = response.data.scrobbles.scrobble;
        const scrobblesArray = Array.isArray(scrobbles) ? scrobbles : [scrobbles];
        
        console.log(`Scrobble successful. Logged ${scrobblesArray.length} tracks:`);
        scrobblesArray.forEach(scrobble => {
            const trackName = scrobble.track['#text'];
            const artistName = scrobble.artist['#text'];
            console.log(`  - "${trackName}" by ${artistName}`);
        });

    } catch (error) {
        console.error('Error calling Last.fm track.scrobble:', error.message);
        throw error;
    }
}

/**
 * Generates the MD5 API signature required for all authenticated Last.fm write calls.
 * The signature is a hash of the API parameters, sorted alphabetically, with the API secret appended.
 * @param {object} params The API call parameters (excluding 'format').
 * @returns {string} The calculated MD5 signature hash.
 */
function generateApiSignature(params) {
    const paramsForSig = { ...params };
    delete paramsForSig.format;

    const sortedKeys = Object.keys(paramsForSig).sort();
    let signatureString = '';
    sortedKeys.forEach(key => {
        signatureString += key + paramsForSig[key];
    });
    signatureString += LASTFM_API_SECRET;

    return md5(signatureString);
}
