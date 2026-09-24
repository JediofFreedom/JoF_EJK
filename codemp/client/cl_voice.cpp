/*
===========================================================================
Copyright (C) 2026 JoF contributors

SDL2 microphone capture and low-bandwidth IMA ADPCM voice chat.
===========================================================================
*/

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "client.h"
#include "snd_public.h"
#include <SDL.h>
#define VOICE_RATE 16000
#define VOICE_FRAME_SAMPLES 640 // 40 ms
#define VOICE_EXTERNAL_HEADER 15
#define VOICE_EXTERNAL_ROOM_MAX 96
#define VOICE_EXTERNAL_PACKET_MAX (VOICE_EXTERNAL_HEADER + VOICE_EXTERNAL_ROOM_MAX + VOICE_MAX_PACKET_BYTES)
#define VOICE_EXTERNAL_VERSION 1
#define VOICE_EXTERNAL_JOIN 1
#define VOICE_EXTERNAL_DATA 2

#ifdef _WIN32
typedef SOCKET voiceSocket_t;
typedef int voiceSocklen_t;
#define VOICE_INVALID_SOCKET INVALID_SOCKET
#define VoiceCloseSocket closesocket
#else
typedef int voiceSocket_t;
typedef socklen_t voiceSocklen_t;
#define VOICE_INVALID_SOCKET (-1)
#define VoiceCloseSocket close
#endif

static SDL_AudioDeviceID voiceCaptureDevice;
static qboolean voiceButtonDown;
static qboolean voiceCapturing;
static qboolean voiceServerSupported;
static byte voiceOutgoing[VOICE_MAX_PACKET_BYTES];
static int voiceOutgoingLength;
static int voiceOutgoingSequence;
static byte voiceGeneration;
static int voiceIncomingSequence[MAX_CLIENTS];
static byte voiceIncomingGeneration[MAX_CLIENTS];
static qboolean voiceIncomingValid[MAX_CLIENTS];
static qboolean voiceMuted[MAX_CLIENTS];
static qboolean voiceMuteAll;
static voiceSocket_t voiceSocket = VOICE_INVALID_SOCKET;
static struct sockaddr_storage voiceRelayAddress;
static voiceSocklen_t voiceRelayAddressLength;
static char voiceAdvertisedServer[MAX_STRING_CHARS];
static char voiceAdvertisedRoom[VOICE_EXTERNAL_ROOM_MAX + 1];
static char voiceActiveServer[MAX_STRING_CHARS];
static char voiceActiveRoom[VOICE_EXTERNAL_ROOM_MAX + 1];
static int voiceLastHeartbeat;
#ifdef _WIN32
static qboolean voiceWinsockStarted;
#endif

static cvar_t *cl_voice;
static cvar_t *cl_voiceVolume;
static cvar_t *cl_voiceCaptureGain;
static cvar_t *cl_voiceInputDevice;
static cvar_t *cl_voiceServer;
static cvar_t *cl_voiceRoom;

static const int imaIndexTable[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static const int imaStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
	143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
	494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
	1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
	4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
	27086, 29794, 32767
};

static int VoiceClampSample(int value)
{
	return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
}

static int VoiceEncode(const short *samples, int count, byte *out, int capacity)
{
	const int needed = 6 + ((count - 1 + 1) / 2);
	if (count < 1 || needed > capacity) {
		return 0;
	}

	int predictor = samples[0];
	int index = 0;
	out[0] = (byte)(predictor & 0xff);
	out[1] = (byte)((predictor >> 8) & 0xff);
	out[2] = (byte)index;
	out[3] = 0;
	out[4] = (byte)(count & 0xff);
	out[5] = (byte)((count >> 8) & 0xff);
	Com_Memset(out + 6, 0, needed - 6);

	for (int i = 1; i < count; ++i) {
		const int step = imaStepTable[index];
		int diff = samples[i] - predictor;
		int code = 0;
		if (diff < 0) { code = 8; diff = -diff; }
		int delta = step >> 3;
		if (diff >= step) { code |= 4; diff -= step; delta += step; }
		if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; delta += step >> 1; }
		if (diff >= (step >> 2)) { code |= 1; delta += step >> 2; }
		predictor = VoiceClampSample(predictor + ((code & 8) ? -delta : delta));
		index = Com_Clampi(0, 88, index + imaIndexTable[code]);
		const int nibble = i - 1;
		out[6 + nibble / 2] |= (byte)((code & 15) << ((nibble & 1) ? 4 : 0));
	}
	return needed;
}

static int VoiceDecode(const byte *data, int length, short *samples, int capacity)
{
	if (length < 7) return 0;
	int predictor = (short)(data[0] | (data[1] << 8));
	int index = data[2];
	const int count = data[4] | (data[5] << 8);
	if (index > 88 || count < 1 || count > capacity || 6 + ((count - 1 + 1) / 2) > length) return 0;
	samples[0] = (short)predictor;
	for (int i = 1; i < count; ++i) {
		const int nibble = i - 1;
		const int code = (data[6 + nibble / 2] >> ((nibble & 1) ? 4 : 0)) & 15;
		const int step = imaStepTable[index];
		int delta = step >> 3;
		if (code & 4) delta += step;
		if (code & 2) delta += step >> 1;
		if (code & 1) delta += step >> 2;
		predictor = VoiceClampSample(predictor + ((code & 8) ? -delta : delta));
		index = Com_Clampi(0, 88, index + imaIndexTable[code]);
		samples[i] = (short)predictor;
	}
	return count;
}

static void VoicePlayEncoded(int sender, int generation, int sequence, const byte *encoded, int length);

static void VoiceCloseRelay(void)
{
	if (voiceSocket != VOICE_INVALID_SOCKET) {
		VoiceCloseSocket(voiceSocket);
		voiceSocket = VOICE_INVALID_SOCKET;
	}
	voiceRelayAddressLength = 0;
	voiceLastHeartbeat = 0;
}

static qboolean VoiceSplitAddress(const char *address, char *host, int hostSize, char *port, int portSize)
{
	const char *portStart;
	int hostLength;
	if (!address || !address[0]) return qfalse;
	if (address[0] == '[') {
		const char *end = strchr(address + 1, ']');
		if (!end || end[1] != ':' || !end[2]) return qfalse;
		hostLength = (int)(end - address - 1);
		portStart = end + 2;
		address++;
	} else {
		portStart = strrchr(address, ':');
		if (!portStart || portStart == address || !portStart[1]) return qfalse;
		hostLength = (int)(portStart - address);
		portStart++;
	}
	if (hostLength <= 0 || hostLength >= hostSize || strlen(portStart) >= (size_t)portSize) return qfalse;
	Com_Memcpy(host, address, hostLength);
	host[hostLength] = '\0';
	Q_strncpyz(port, portStart, portSize);
	return qtrue;
}

static qboolean VoiceOpenRelay(const char *address)
{
	char host[256], port[16];
	struct addrinfo hints, *addresses = NULL, *candidate;
	if (!VoiceSplitAddress(address, host, sizeof(host), port, sizeof(port))) {
		Com_Printf(S_COLOR_RED "Voice: invalid relay address '%s' (expected host:port)\n", address);
		return qfalse;
	}
	Com_Memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	if (getaddrinfo(host, port, &hints, &addresses) != 0) {
		Com_Printf(S_COLOR_RED "Voice: could not resolve relay '%s'\n", address);
		return qfalse;
	}
	for (candidate = addresses; candidate; candidate = candidate->ai_next) {
		voiceSocket = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
		if (voiceSocket == VOICE_INVALID_SOCKET) continue;
#ifdef _WIN32
		u_long nonBlocking = 1;
		if (ioctlsocket(voiceSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
#else
		if (fcntl(voiceSocket, F_SETFL, fcntl(voiceSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
#endif
			VoiceCloseSocket(voiceSocket);
			voiceSocket = VOICE_INVALID_SOCKET;
			continue;
		}
		Com_Memcpy(&voiceRelayAddress, candidate->ai_addr, candidate->ai_addrlen);
		voiceRelayAddressLength = (voiceSocklen_t)candidate->ai_addrlen;
		break;
	}
	freeaddrinfo(addresses);
	if (voiceSocket == VOICE_INVALID_SOCKET) {
		Com_Printf(S_COLOR_RED "Voice: could not open UDP relay '%s'\n", address);
		return qfalse;
	}
	voiceLastHeartbeat = cls.realtime - 5000;
	Com_Printf("Voice: using external relay %s, room '%s'\n", address, voiceActiveRoom);
	return qtrue;
}

static void VoiceWriteLongLE(byte *out, int value)
{
	out[0] = (byte)value; out[1] = (byte)(value >> 8);
	out[2] = (byte)(value >> 16); out[3] = (byte)(value >> 24);
}

static int VoiceReadLongLE(const byte *in)
{
	return (int)((unsigned)in[0] | ((unsigned)in[1] << 8) | ((unsigned)in[2] << 16) | ((unsigned)in[3] << 24));
}

static qboolean VoiceRelaySenderMatches(const struct sockaddr_storage *from, voiceSocklen_t fromLength)
{
	if (fromLength != voiceRelayAddressLength || from->ss_family != voiceRelayAddress.ss_family) return qfalse;
	if (from->ss_family == AF_INET) {
		const struct sockaddr_in *a = (const struct sockaddr_in *)from;
		const struct sockaddr_in *b = (const struct sockaddr_in *)&voiceRelayAddress;
		return (qboolean)(a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr);
	}
	if (from->ss_family == AF_INET6) {
		const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)from;
		const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)&voiceRelayAddress;
		return (qboolean)(a->sin6_port == b->sin6_port && !memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)));
	}
	return qfalse;
}

static void VoiceSendExternal(int type, const byte *payload, int payloadLength, int sequence)
{
	byte packet[VOICE_EXTERNAL_PACKET_MAX];
	const int roomLength = (int)strlen(voiceActiveRoom);
	if (voiceSocket == VOICE_INVALID_SOCKET || roomLength <= 0 || roomLength > VOICE_EXTERNAL_ROOM_MAX ||
		payloadLength < 0 || payloadLength > VOICE_MAX_PACKET_BYTES) return;
	packet[0] = 'J'; packet[1] = 'O'; packet[2] = 'F'; packet[3] = 'V';
	packet[4] = VOICE_EXTERNAL_VERSION;
	packet[5] = (byte)type;
	packet[6] = (byte)clc.clientNum;
	packet[7] = voiceGeneration;
	VoiceWriteLongLE(packet + 8, sequence);
	packet[12] = (byte)payloadLength;
	packet[13] = (byte)(payloadLength >> 8);
	packet[14] = (byte)roomLength;
	Com_Memcpy(packet + VOICE_EXTERNAL_HEADER, voiceActiveRoom, roomLength);
	if (payloadLength) Com_Memcpy(packet + VOICE_EXTERNAL_HEADER + roomLength, payload, payloadLength);
	sendto(voiceSocket, (const char *)packet, VOICE_EXTERNAL_HEADER + roomLength + payloadLength, 0,
		(const struct sockaddr *)&voiceRelayAddress, voiceRelayAddressLength);
}

static void VoiceUpdateRelay(void)
{
	const char *server = voiceAdvertisedServer[0] ? voiceAdvertisedServer : cl_voiceServer->string;
	char room[VOICE_EXTERNAL_ROOM_MAX + 1];
	if (voiceAdvertisedServer[0] && voiceAdvertisedRoom[0]) Q_strncpyz(room, voiceAdvertisedRoom, sizeof(room));
	else if (!voiceAdvertisedServer[0] && cl_voiceRoom->string[0]) Q_strncpyz(room, cl_voiceRoom->string, sizeof(room));
	else Q_strncpyz(room, NET_AdrToString(clc.serverAddress), sizeof(room));
	if (!Q_stricmp(server, voiceActiveServer) && !Q_stricmp(room, voiceActiveRoom)) return;
	VoiceCloseRelay();
	Q_strncpyz(voiceActiveServer, server, sizeof(voiceActiveServer));
	Q_strncpyz(voiceActiveRoom, room, sizeof(voiceActiveRoom));
	if (voiceActiveServer[0] && voiceActiveRoom[0]) VoiceOpenRelay(voiceActiveServer);
}

static void VoicePollRelay(void)
{
	byte packet[VOICE_EXTERNAL_PACKET_MAX];
	if (voiceSocket == VOICE_INVALID_SOCKET) return;
	for (int packets = 0; packets < 128; ++packets) {
		struct sockaddr_storage from;
		voiceSocklen_t fromLength = sizeof(from);
		const int length = recvfrom(voiceSocket, (char *)packet, sizeof(packet), 0, (struct sockaddr *)&from, &fromLength);
		if (length < 0) break;
		if (!VoiceRelaySenderMatches(&from, fromLength) || length < VOICE_EXTERNAL_HEADER ||
			packet[0] != 'J' || packet[1] != 'O' || packet[2] != 'F' || packet[3] != 'V' ||
			packet[4] != VOICE_EXTERNAL_VERSION || packet[5] != VOICE_EXTERNAL_DATA) continue;
		const int payloadLength = packet[12] | (packet[13] << 8);
		const int roomLength = packet[14];
		if (roomLength <= 0 || roomLength > VOICE_EXTERNAL_ROOM_MAX || payloadLength < 0 ||
			payloadLength > VOICE_MAX_PACKET_BYTES || VOICE_EXTERNAL_HEADER + roomLength + payloadLength != length ||
			(int)strlen(voiceActiveRoom) != roomLength || memcmp(packet + VOICE_EXTERNAL_HEADER, voiceActiveRoom, roomLength)) continue;
		VoicePlayEncoded(packet[6], packet[7], VoiceReadLongLE(packet + 8),
			packet + VOICE_EXTERNAL_HEADER + roomLength, payloadLength);
	}
}

static qboolean VoiceOpenCapture(void)
{
	if (voiceCaptureDevice) return qtrue;
	if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		Com_Printf(S_COLOR_RED "Voice: SDL audio initialization failed: %s\n", SDL_GetError());
		return qfalse;
	}
	SDL_AudioSpec desired;
	SDL_zero(desired);
	desired.freq = VOICE_RATE;
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = VOICE_FRAME_SAMPLES;
	const char *deviceName = (cl_voiceInputDevice->string[0] ? cl_voiceInputDevice->string : NULL);
	voiceCaptureDevice = SDL_OpenAudioDevice(deviceName, SDL_TRUE, &desired, NULL, 0);
	if (!voiceCaptureDevice) {
		Com_Printf(S_COLOR_RED "Voice: could not open capture device: %s\n", SDL_GetError());
		return qfalse;
	}
	Com_Printf("Voice: capture device opened%s%s\n", deviceName ? ": " : "", deviceName ? deviceName : "");
	return qtrue;
}

static void VoiceStart_f(void) { voiceButtonDown = qtrue; Cvar_Set("cl_voiceTalking", "1"); }
static void VoiceStop_f(void) { voiceButtonDown = qfalse; Cvar_Set("cl_voiceTalking", "0"); }

static void VoiceDevices_f(void)
{
	const int count = SDL_GetNumAudioDevices(SDL_TRUE);
	Com_Printf("Capture devices (%d):\n", count);
	for (int i = 0; i < count; ++i) Com_Printf("  %d: %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
}

void CL_VoiceRestartCapture(void)
{
	if (voiceCaptureDevice) SDL_CloseAudioDevice(voiceCaptureDevice);
	voiceCaptureDevice = 0;
	voiceCapturing = qfalse;
	voiceOutgoingLength = 0;
	Com_Printf("Voice capture will reopen on the next transmission.\n");
}

static void VoiceRestart_f(void) { CL_VoiceRestartCapture(); }

static void VoiceReconnect_f(void)
{
	VoiceCloseRelay();
	voiceActiveServer[0] = '\0';
	voiceActiveRoom[0] = '\0';
	VoiceUpdateRelay();
}

static void VoiceMute_f(void)
{
	if (Cmd_Argc() != 2) { Com_Printf("usage: voice_mute <client number>\n"); return; }
	const int clientNum = atoi(Cmd_Argv(1));
	if (clientNum < 0 || clientNum >= MAX_CLIENTS) { Com_Printf("Invalid client number.\n"); return; }
	voiceMuted[clientNum] = (qboolean)!voiceMuted[clientNum];
	Com_Printf("Voice from client %d %s.\n", clientNum, voiceMuted[clientNum] ? "muted" : "unmuted");
}

static void VoiceMuteAll_f(void)
{
	voiceMuteAll = (qboolean)!voiceMuteAll;
	Com_Printf("Voice chat %s.\n", voiceMuteAll ? "muted" : "unmuted");
}

void CL_VoiceInit(void)
{
	#ifdef _WIN32
	WSADATA winsockData;
	voiceWinsockStarted = (qboolean)(WSAStartup(MAKEWORD(2, 2), &winsockData) == 0);
	#endif
	cl_voice = Cvar_Get("cl_voice", "1", CVAR_ARCHIVE, "Enable voice chat");
	cl_voiceVolume = Cvar_Get("cl_voiceVolume", "1.0", CVAR_ARCHIVE, "Voice playback volume");
	cl_voiceCaptureGain = Cvar_Get("cl_voiceCaptureGain", "1.0", CVAR_ARCHIVE, "Microphone gain");
	cl_voiceInputDevice = Cvar_Get("cl_voiceInputDevice", "", CVAR_ARCHIVE, "SDL capture device name");
	cl_voiceServer = Cvar_Get("cl_voiceServer", "", CVAR_ARCHIVE, "Fallback external voice relay (host:port)");
	cl_voiceRoom = Cvar_Get("cl_voiceRoom", "", CVAR_ARCHIVE, "Fallback external voice room");
	Cvar_Get("cl_voiceProtocol", VOICE_PROTOCOL_NAME, CVAR_USERINFO | CVAR_ROM, "Voice protocol capability");
	Cvar_Get("cl_voiceTalking", "0", CVAR_ROM, "Whether push-to-talk is active");
	Cmd_AddCommand("+voice", VoiceStart_f, "Start push-to-talk voice chat");
	Cmd_AddCommand("-voice", VoiceStop_f, "Stop push-to-talk voice chat");
	Cmd_AddCommand("voice_devices", VoiceDevices_f, "List microphone capture devices");
	Cmd_AddCommand("voice_restart", VoiceRestart_f, "Reopen the voice capture device");
	Cmd_AddCommand("voice_reconnect", VoiceReconnect_f, "Reconnect to the external voice relay");
	Cmd_AddCommand("voice_mute", VoiceMute_f, "Toggle mute for a client number");
	Cmd_AddCommand("voice_muteall", VoiceMuteAll_f, "Toggle all voice playback");
}

void CL_VoiceShutdown(void)
{
	if (voiceCaptureDevice) SDL_CloseAudioDevice(voiceCaptureDevice);
	voiceCaptureDevice = 0;
	VoiceCloseRelay();
	#ifdef _WIN32
	if (voiceWinsockStarted) WSACleanup();
	voiceWinsockStarted = qfalse;
	#endif
	Cmd_RemoveCommand("+voice"); Cmd_RemoveCommand("-voice");
	Cmd_RemoveCommand("voice_devices"); Cmd_RemoveCommand("voice_restart");
	Cmd_RemoveCommand("voice_reconnect");
	Cmd_RemoveCommand("voice_mute"); Cmd_RemoveCommand("voice_muteall");
}

void CL_VoiceSystemInfo(const char *systemInfo)
{
	voiceServerSupported = (qboolean)!Q_stricmp(Info_ValueForKey(systemInfo, "sv_voiceProtocol"), VOICE_PROTOCOL_NAME);
	Q_strncpyz(voiceAdvertisedServer, Info_ValueForKey(systemInfo, "sv_voiceServer"), sizeof(voiceAdvertisedServer));
	Q_strncpyz(voiceAdvertisedRoom, Info_ValueForKey(systemInfo, "sv_voiceRoom"), sizeof(voiceAdvertisedRoom));
	VoiceCloseRelay();
	voiceActiveServer[0] = '\0';
	voiceActiveRoom[0] = '\0';
}

void CL_VoiceFrame(void)
{
	if (cls.state >= CA_ACTIVE) {
		VoiceUpdateRelay();
		VoicePollRelay();
		if (voiceSocket != VOICE_INVALID_SOCKET && cls.realtime - voiceLastHeartbeat >= 5000) {
			VoiceSendExternal(VOICE_EXTERNAL_JOIN, NULL, 0, 0);
			voiceLastHeartbeat = cls.realtime;
		}
	} else if (voiceSocket != VOICE_INVALID_SOCKET || voiceActiveServer[0]) {
		VoiceCloseRelay();
		voiceActiveServer[0] = '\0';
		voiceActiveRoom[0] = '\0';
	}
	const qboolean shouldCapture = (qboolean)(voiceButtonDown && cl_voice->integer &&
		(voiceActiveServer[0] || voiceServerSupported) && cls.state >= CA_ACTIVE);
	if (!shouldCapture) {
		if (voiceCapturing && voiceCaptureDevice) SDL_PauseAudioDevice(voiceCaptureDevice, 1);
		voiceCapturing = qfalse;
		return;
	}
	if (!VoiceOpenCapture()) return;
	if (!voiceCapturing) {
		SDL_ClearQueuedAudio(voiceCaptureDevice);
		SDL_PauseAudioDevice(voiceCaptureDevice, 0);
		voiceCapturing = qtrue;
		voiceGeneration++;
		voiceOutgoingSequence = 0;
	}
	if (voiceOutgoingLength) return;
	const Uint32 frameBytes = VOICE_FRAME_SAMPLES * sizeof(short);
	const Uint32 queued = SDL_GetQueuedAudioSize(voiceCaptureDevice);
	if (queued > frameBytes * 5) SDL_ClearQueuedAudio(voiceCaptureDevice);
	if (SDL_GetQueuedAudioSize(voiceCaptureDevice) < frameBytes) return;
	short samples[VOICE_FRAME_SAMPLES];
	if (SDL_DequeueAudio(voiceCaptureDevice, samples, frameBytes) != frameBytes) return;
	const float gain = Com_Clamp(0.0f, 8.0f, cl_voiceCaptureGain->value);
	if (gain != 1.0f) for (int i = 0; i < VOICE_FRAME_SAMPLES; ++i) samples[i] = (short)VoiceClampSample((int)(samples[i] * gain));
	voiceOutgoingLength = VoiceEncode(samples, VOICE_FRAME_SAMPLES, voiceOutgoing, sizeof(voiceOutgoing));
}

qboolean CL_VoiceWritePacket(msg_t *msg)
{
	if (!voiceOutgoingLength) return qfalse;
	if (voiceActiveServer[0]) {
		VoiceSendExternal(VOICE_EXTERNAL_DATA, voiceOutgoing, voiceOutgoingLength, voiceOutgoingSequence++);
		voiceOutgoingLength = 0;
		return qtrue;
	}
	if (msg->cursize + voiceOutgoingLength + 9 >= msg->maxsize) return qfalse;
	MSG_WriteByte(msg, clc_voice);
	MSG_WriteByte(msg, voiceGeneration);
	MSG_WriteLong(msg, voiceOutgoingSequence++);
	MSG_WriteShort(msg, voiceOutgoingLength);
	MSG_WriteData(msg, voiceOutgoing, voiceOutgoingLength);
	voiceOutgoingLength = 0;
	return qtrue;
}

static void VoicePlayEncoded(int sender, int generation, int sequence, const byte *encoded, int length)
{
	if (sender < 0 || sender >= MAX_CLIENTS || length < 0 || length > VOICE_MAX_PACKET_BYTES ||
		!cl_voice->integer || voiceMuteAll || voiceMuted[sender]) return;
	if (voiceIncomingValid[sender] && voiceIncomingGeneration[sender] == generation && sequence <= voiceIncomingSequence[sender]) return;
	short samples[VOICE_FRAME_SAMPLES];
	const int count = VoiceDecode(encoded, length, samples, ARRAY_LEN(samples));
	if (!count) return;
	voiceIncomingValid[sender] = qtrue;
	voiceIncomingGeneration[sender] = (byte)generation;
	voiceIncomingSequence[sender] = sequence;
	S_RawSamples(count, VOICE_RATE, 2, 1, (const byte *)samples, Com_Clamp(0.0f, 2.0f, cl_voiceVolume->value), 1);
}

void CL_ParseVoice(msg_t *msg)
{
	const int sender = MSG_ReadByte(msg);
	const int generation = MSG_ReadByte(msg);
	const int sequence = MSG_ReadLong(msg);
	const int length = MSG_ReadShort(msg);
	byte encoded[VOICE_MAX_PACKET_BYTES];
	if (sender < 0 || sender >= MAX_CLIENTS || length < 0 || length > VOICE_MAX_PACKET_BYTES || msg->readcount + length > msg->cursize) {
		msg->readcount = msg->cursize + 1;
		return;
	}
	MSG_ReadData(msg, encoded, length);
	VoicePlayEncoded(sender, generation, sequence, encoded, length);
}
