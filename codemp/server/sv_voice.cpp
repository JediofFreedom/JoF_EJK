/*
===========================================================================
Copyright (C) 2026 JoF contributors

Engine-level voice relay. The server treats codec payloads as opaque and
only forwards them to clients that advertised the matching protocol.
===========================================================================
*/

#include "server.h"

cvar_t *sv_voice;

void SV_UserVoice( client_t *sender, msg_t *msg )
{
	const int generation = MSG_ReadByte(msg);
	const int sequence = MSG_ReadLong(msg);
	const int length = MSG_ReadShort(msg);
	byte encoded[VOICE_MAX_PACKET_BYTES];

	if (length < 0 || length > VOICE_MAX_PACKET_BYTES || msg->readcount + length > msg->cursize) {
		// Consume nothing further for malformed data; the enclosing packet is bad.
		msg->readcount = msg->cursize + 1;
		return;
	}

	MSG_ReadData(msg, encoded, length);
	if (!sv_voice || !sv_voice->integer || !sender->hasVoice ||
		sender->state != CS_ACTIVE || *sender->downloadName || length < 7) {
		return;
	}
	// Expected traffic is one 40 ms frame. Cap abusive clients before the
	// broadcast fan-out can amplify their traffic to every recipient.
	if (sender->lastVoicePacketTime && svs.time - sender->lastVoicePacketTime < 20) {
		return;
	}
	sender->lastVoicePacketTime = svs.time;

	const int senderNum = (int)(sender - svs.clients);
	for (int i = 0; i < sv_maxclients->integer; ++i) {
		client_t *recipient = &svs.clients[i];
		if (recipient == sender || recipient->state != CS_ACTIVE ||
			!recipient->hasVoice || *recipient->downloadName) {
			continue;
		}

		if (recipient->voiceQueueCount == (int)ARRAY_LEN(recipient->voicePackets)) {
			// Live audio is more useful than stale audio: discard the oldest frame.
			recipient->voiceQueueStart = (recipient->voiceQueueStart + 1) % ARRAY_LEN(recipient->voicePackets);
			recipient->voiceQueueCount--;
		}

		const int slot = (recipient->voiceQueueStart + recipient->voiceQueueCount) % ARRAY_LEN(recipient->voicePackets);
		recipient->voicePackets[slot].sender = (byte)senderNum;
		recipient->voicePackets[slot].generation = (byte)generation;
		recipient->voicePackets[slot].sequence = sequence;
		recipient->voicePackets[slot].length = (unsigned short)length;
		Com_Memcpy(recipient->voicePackets[slot].data, encoded, length);
		recipient->voiceQueueCount++;
	}
}

void SV_WriteVoiceToClient( client_t *client, msg_t *msg )
{
	while (client->voiceQueueCount > 0) {
		const int slot = client->voiceQueueStart;
		const int length = client->voicePackets[slot].length;
		const int overhead = 1 + 1 + 1 + 4 + 2;

		if (msg->cursize + overhead + length + 1 >= msg->maxsize) {
			break;
		}

		MSG_WriteByte(msg, svc_voice);
		MSG_WriteByte(msg, client->voicePackets[slot].sender);
		MSG_WriteByte(msg, client->voicePackets[slot].generation);
		MSG_WriteLong(msg, client->voicePackets[slot].sequence);
		MSG_WriteShort(msg, length);
		MSG_WriteData(msg, client->voicePackets[slot].data, length);

		client->voiceQueueStart = (client->voiceQueueStart + 1) % ARRAY_LEN(client->voicePackets);
		client->voiceQueueCount--;
	}
}
