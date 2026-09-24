'use strict';

const dgram = require('node:dgram');

const MAGIC = Buffer.from('JOFV');
const VERSION = 1;
const TYPE_JOIN = 1;
const TYPE_DATA = 2;
const HEADER_BYTES = 15;
const MAX_ROOM_BYTES = 96;
const MAX_VOICE_BYTES = 512;
const MAX_PACKET_BYTES = HEADER_BYTES + MAX_ROOM_BYTES + MAX_VOICE_BYTES;
const MAX_ROOMS = Number(process.env.VOICE_MAX_ROOMS || 1024);
const MAX_PEERS_PER_ROOM = Number(process.env.VOICE_MAX_PEERS || 128);
const PEER_TIMEOUT_MS = Number(process.env.VOICE_PEER_TIMEOUT_MS || 15000);
const RATE_PER_SECOND = Number(process.env.VOICE_RATE || 35);
const RATE_BURST = Number(process.env.VOICE_BURST || 20);
const host = process.env.VOICE_HOST || '0.0.0.0';
const port = Number(process.env.VOICE_PORT || 27970);

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  throw new Error('VOICE_PORT must be an integer from 1 to 65535');
}

const rooms = new Map();
const socket = dgram.createSocket(host.includes(':') ? 'udp6' : 'udp4');

function peerKey(remote) {
  return `${remote.family}|${remote.address}|${remote.port}`;
}

function peerAddress(peer) {
  const address = peer.family === 'IPv6' ? `[${peer.address}]` : peer.address;
  return `${address}:${peer.port}`;
}

function parsePacket(packet) {
  if (packet.length < HEADER_BYTES || packet.length > MAX_PACKET_BYTES ||
      !packet.subarray(0, 4).equals(MAGIC) || packet[4] !== VERSION ||
      (packet[5] !== TYPE_JOIN && packet[5] !== TYPE_DATA)) return null;

  const payloadLength = packet.readUInt16LE(12);
  const roomLength = packet[14];
  if (roomLength < 1 || roomLength > MAX_ROOM_BYTES || payloadLength > MAX_VOICE_BYTES ||
      HEADER_BYTES + roomLength + payloadLength !== packet.length ||
      (packet[5] === TYPE_JOIN && payloadLength !== 0) ||
      (packet[5] === TYPE_DATA && payloadLength === 0)) return null;

  const roomBytes = packet.subarray(HEADER_BYTES, HEADER_BYTES + roomLength);
  const room = roomBytes.toString('utf8');
  if (!room || !Buffer.from(room, 'utf8').equals(roomBytes)) return null;
  return { type: packet[5], sender: packet[6], room };
}

function allowVoice(peer, now) {
  const elapsed = Math.max(0, now - peer.rateUpdated);
  peer.tokens = Math.min(RATE_BURST, peer.tokens + elapsed * RATE_PER_SECOND / 1000);
  peer.rateUpdated = now;
  if (peer.tokens < 1) return false;
  peer.tokens -= 1;
  return true;
}

socket.on('message', (packet, remote) => {
  const parsed = parsePacket(packet);
  if (!parsed) return;

  let room = rooms.get(parsed.room);
  if (!room) {
    if (rooms.size >= MAX_ROOMS) return;
    room = new Map();
    rooms.set(parsed.room, room);
  }

  const key = peerKey(remote);
  const now = Date.now();
  let peer = room.get(key);
  if (!peer) {
    if (room.size >= MAX_PEERS_PER_ROOM) return;
    peer = { ...remote, sender: parsed.sender, lastSeen: now, tokens: RATE_BURST, rateUpdated: now };
    room.set(key, peer);
    console.log(`[join] room=${JSON.stringify(parsed.room)} client=${peer.sender} endpoint=${peerAddress(peer)} peers=${room.size}`);
  }
  peer.lastSeen = now;
  if (parsed.type === TYPE_JOIN && peer.sender !== parsed.sender) {
    const previousSender = peer.sender;
    peer.sender = parsed.sender;
    console.log(`[identity] room=${JSON.stringify(parsed.room)} client=${previousSender}->${peer.sender} endpoint=${peerAddress(peer)}`);
  }

  if (parsed.type !== TYPE_DATA || !allowVoice(peer, now)) return;

  // The sender id is fixed when this UDP endpoint joins, preventing mid-stream impersonation.
  packet[6] = peer.sender;
  for (const [otherKey, other] of room) {
    if (otherKey !== key) socket.send(packet, other.port, other.address);
  }
});

socket.on('error', error => {
  console.error(`voice relay socket error: ${error.stack || error.message}`);
});

const cleanup = setInterval(() => {
  const cutoff = Date.now() - PEER_TIMEOUT_MS;
  for (const [roomName, room] of rooms) {
    for (const [key, peer] of room) {
      if (peer.lastSeen < cutoff) {
        room.delete(key);
        console.log(`[leave] room=${JSON.stringify(roomName)} client=${peer.sender} endpoint=${peerAddress(peer)} peers=${room.size}`);
      }
    }
    if (room.size === 0) rooms.delete(roomName);
  }
}, Math.max(1000, Math.floor(PEER_TIMEOUT_MS / 3)));
cleanup.unref();

socket.bind(port, host, () => {
  const address = socket.address();
  console.log(`JoF voice relay listening on ${address.address}:${address.port}/${address.family}`);
});

function shutdown(signal) {
  console.log(`received ${signal}, shutting down`);
  socket.close(() => process.exit(0));
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
