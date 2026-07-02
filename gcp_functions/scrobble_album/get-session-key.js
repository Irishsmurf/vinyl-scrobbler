#!/usr/bin/env node
/**
 * One-off helper to obtain a Last.fm session key (`sk`) for scrobbling.
 *
 * Run it, open the printed URL to authorize the app for YOUR Last.fm account,
 * then press Enter. It prints the session key to paste into .env.yaml.
 *
 * Usage:
 *   cd gcp_functions/scrobble_album
 *   node get-session-key.js
 *
 * Reads the API key/secret from .env.yaml (or LASTFM_API_KEY / LASTFM_API_SECRET
 * env vars). The session key never expires — you only do this once.
 */

const fs = require('fs');
const path = require('path');
const readline = require('readline');
const axios = require('axios');
const md5 = require('md5');

const API_URL = 'https://ws.audioscrobbler.com/2.0/';

function loadCreds() {
  let key = process.env.LASTFM_API_KEY;
  let secret = process.env.LASTFM_API_SECRET;
  const envPath = path.join(__dirname, '.env.yaml');
  if ((!key || !secret) && fs.existsSync(envPath)) {
    const text = fs.readFileSync(envPath, 'utf8');
    const grab = (name) => (text.match(new RegExp(`^${name}:\\s*["']?([^"'\\s]+)`, 'm')) || [])[1];
    key = key || grab('LASTFM_API_KEY');
    secret = secret || grab('LASTFM_API_SECRET');
  }
  if (!key || !secret) {
    console.error('Missing LASTFM_API_KEY / LASTFM_API_SECRET (set env vars or fill .env.yaml).');
    process.exit(1);
  }
  return { key, secret };
}

// Last.fm signs requests with an md5 of the sorted params + shared secret.
function sign(params, secret) {
  const base = Object.keys(params).sort().map((k) => `${k}${params[k]}`).join('');
  return md5(base + secret);
}

async function call(method, params, secret) {
  const signed = { ...params, method, api_sig: sign({ ...params, method }, secret) };
  const { data } = await axios.get(API_URL, { params: { ...signed, format: 'json' } });
  return data;
}

function prompt(question) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  return new Promise((resolve) => rl.question(question, (a) => { rl.close(); resolve(a); }));
}

(async () => {
  const { key, secret } = loadCreds();

  // 1. Get an unauthorized request token.
  const tokenResp = await call('auth.getToken', { api_key: key }, secret);
  const token = tokenResp.token;
  if (!token) throw new Error(`auth.getToken failed: ${JSON.stringify(tokenResp)}`);

  // 2. User authorizes the app for their account.
  console.log('\n1. Open this URL in a browser and click "Yes, allow access":\n');
  console.log(`   https://www.last.fm/api/auth/?api_key=${key}&token=${token}\n`);
  await prompt('2. After authorizing, press Enter here to continue... ');

  // 3. Exchange the authorized token for a permanent session key.
  const sessionResp = await call('auth.getSession', { api_key: key, token }, secret);
  const sk = sessionResp.session && sessionResp.session.key;
  if (!sk) throw new Error(`auth.getSession failed: ${JSON.stringify(sessionResp)}`);

  console.log(`\n✅ Session key for user "${sessionResp.session.name}":\n`);
  console.log(`   ${sk}\n`);
  console.log('Paste it into .env.yaml as LASTFM_SESSION_KEY, then run ../deploy.sh (or ./deploy.sh from gcp_functions).');
})().catch((err) => {
  console.error('\nError:', err.message);
  process.exit(1);
});
