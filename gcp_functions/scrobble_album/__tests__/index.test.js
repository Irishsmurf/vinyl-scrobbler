const axios = require('axios');
const MockAdapter = require('axios-mock-adapter');
const { Firestore } = require('@google-cloud/firestore');
const {
    findAlbumByRfid,
    getAlbumTracks,
    scrobbleTracks,
    generateApiSignature
} = require('../index'); // Adjust the path to your index.js

// Mock Firestore
jest.mock('@google-cloud/firestore', () => {
    const mockGet = jest.fn();
    const mockWhere = jest.fn(() => ({
        limit: () => ({
            get: mockGet
        })
    }));
    const mockCollectionGroup = jest.fn(() => ({
        where: mockWhere
    }));

    return {
        Firestore: jest.fn(() => ({
            collectionGroup: mockCollectionGroup
        })),
        mockGet,
        mockWhere,
        mockCollectionGroup
    };
});


describe('Vinyl Scrobbler Cloud Function', () => {
    let mockAxios;
    let firestoreMocks;

    beforeEach(() => {
        mockAxios = new MockAdapter(axios);
        firestoreMocks = require('@google-cloud/firestore');
        firestoreMocks.mockGet.mockClear();
        firestoreMocks.mockWhere.mockClear();
        firestoreMocks.mockCollectionGroup.mockClear();
        // Set up environment variables
        process.env.LASTFM_API_KEY = 'test_api_key';
        process.env.LASTFM_API_SECRET = 'test_api_secret';
        process.env.LASTFM_SESSION_KEY = 'test_session_key';
    });

    afterEach(() => {
        mockAxios.restore();
    });

    describe('findAlbumByRfid', () => {
        it('should return album details when an RFID is found', async () => {
            const fakeRfid = '12345';
            const fakeAlbum = {
                artist: 'Test Artist',
                album: 'Test Album'
            };
            firestoreMocks.mockGet.mockResolvedValue({
                empty: false,
                docs: [{
                    id: 'doc1',
                    data: () => fakeAlbum
                }]
            });

            const result = await findAlbumByRfid(fakeRfid);

            expect(result).toEqual({
                id: 'doc1',
                ...fakeAlbum
            });
            expect(firestoreMocks.mockCollectionGroup).toHaveBeenCalledWith('albums');
            expect(firestoreMocks.mockWhere).toHaveBeenCalledWith('rfid', '==', fakeRfid);
        });

        it('should return null when no RFID is found', async () => {
            firestoreMocks.mockGet.mockResolvedValue({
                empty: true
            });

            const result = await findAlbumByRfid('unknown-rfid');
            expect(result).toBeNull();
        });

        it('should throw an error if Firestore query fails', async () => {
            const firestoreError = new Error('Firestore query failed');
            firestoreMocks.mockGet.mockRejectedValue(firestoreError);

            await expect(findAlbumByRfid('any-rfid')).rejects.toThrow('Firestore query failed');
        });

    });

    describe('getAlbumTracks', () => {
        it('should return a list of tracks for a valid album', async () => {
            const artist = 'The Cure';
            const album = 'Disintegration';
            const mockResponse = {
                album: {
                    tracks: {
                        track: [{
                            name: 'Plainsong'
                        }, {
                            name: 'Pictures of You'
                        }]
                    }
                }
            };
            mockAxios.onGet().reply(200, mockResponse);

            const tracks = await getAlbumTracks(artist, album);

            expect(tracks.length).toBe(2);
            expect(tracks[0].name).toBe('Plainsong');
        });


        it('should return an empty array if the API returns an error', async () => {
            mockAxios.onGet().reply(200, {
                error: 6,
                message: 'Album not found'
            });
            const tracks = await getAlbumTracks('Unknown', 'Album');
            expect(tracks).toEqual([]);
        });
    });

    describe('scrobbleTracks', () => {
        it('should make a successful POST request to scrobble tracks', async () => {
            const tracks = [{
                name: 'Track 1'
            }, {
                name: 'Track 2'
            }];
            const artist = 'Artist';
            const album = 'Album';

            const mockResponse = {
                scrobbles: {
                    '@attr': {
                        accepted: 2,
                        ignored: 0
                    },
                    scrobble: [{
                        track: {
                            '#text': 'Track 1'
                        },
                        artist: {
                            '#text': 'Artist'
                        },
                        album: {
                            '#text': 'Album'
                        }
                    }, {
                        track: {
                            '#text': 'Track 2'
                        },
                        artist: {
                            '#text': 'Artist'
                        },
                        album: {
                            '#text': 'Album'
                        }
                    }]
                }
            };

            mockAxios.onPost().reply(200, mockResponse);

            await scrobbleTracks(tracks, artist, album);

            expect(mockAxios.history.post.length).toBe(1);
            const postData = new URLSearchParams(mockAxios.history.post[0].data);
            expect(postData.get('track[0]')).toBe('Track 1');
            expect(postData.get('method')).toBe('track.scrobble');
        });

    });

    describe('generateApiSignature', () => {
        it('should generate a correct MD5 signature', () => {
            const params = {
                method: 'album.getinfo',
                artist: 'The Cure',
                album: 'Disintegration',
                api_key: 'test_api_key',
                sk: 'test_session_key'
            };

            const expectedSignature = '5195769657259070980b733794079e46';

            const signature = generateApiSignature(params);

            expect(signature).toBe(expectedSignature);
        });

    });

});
