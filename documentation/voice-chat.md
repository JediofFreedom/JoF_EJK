# Voice chat

Voice chat is enabled by default on new clients and servers. It uses 16 kHz
mono IMA ADPCM at roughly 8 KB/s per active speaker and is negotiated through
the `jof-adpcm1` capability, so unmodified clients and servers remain
compatible.

Bind a key to push-to-talk:

```
bind v +voice
```

Useful commands and settings:

- `voice_devices` lists available microphones.
- `cl_voiceInputDevice "device name"` selects one; run `voice_restart` after
  changing it.
- `cl_voiceCaptureGain 1.0` controls microphone gain (0 to 8).
- `cl_voiceVolume 1.0` controls received voice volume (0 to 2).
- `voice_mute <client number>` toggles one player.
- `voice_muteall` toggles all received voice.
- `cl_voice 0` disables voice on a client.
- `sv_voice 0` disables voice relay after a server restart.

The server relays voice as lossy traffic. Dropped voice frames are not resent,
which prevents old speech from building up behind gameplay snapshots.

## External relay

An external UDP relay is included in `voice-relay`. It uses only Node.js built-in
modules and requires Node.js 18 or newer:

```
cd voice-relay
npm start
```

It listens on UDP port 27970 by default. `VOICE_HOST` and `VOICE_PORT` select the
listen address and port. Make that UDP port reachable through the host firewall
and NAT, then advertise it from the game server:

```
seta sv_voiceServer "voice.example.com:27970"
seta sv_voiceRoom "my-public-server"
```

Both values are included in server info and system info. `sv_voiceRoom` keeps
multiple game servers on one relay separate and should be unique. If it is left
empty, clients derive a room from the game server address; setting it explicitly
also makes listen-server and localhost clients join the same room.

If a server does not advertise `sv_voiceServer`, a player can configure the same
values manually:

```
seta cl_voiceServer "voice.example.com:27970"
seta cl_voiceRoom "my-public-server"
```

The advertised server and room take precedence over the client settings. Clear
both server and client relay addresses to use the built-in relay. Run
`voice_reconnect` after a DNS or networking failure to retry immediately.

The relay validates packet sizes, isolates rooms, expires inactive peers, and
rate-limits voice datagrams. It does not encrypt or authenticate audio; deploy it
like other public game UDP services and do not treat room names as secrets.
Valid client heartbeats produce `[join]` messages in the relay console. A
`[leave]` message appears after a client stops sending heartbeats for 15 seconds;
each message includes the room, client number, endpoint, and room population.
