# Force Destruction

A Legends / Jedi Knight-inspired dark-side power: a concentrated, non-homing
energy orb that bursts on contact. It damages and knocks back nearby targets,
including its caster at close range. This is a server-granted extra ability,
like the Repulse wheel integration, not a new purchasable force-power rank.

## Enable and use

Install the updated game module on the server. For the wheel integration and
optional custom media support, install the updated client executable/cgame.
No Destruction-specific asset pack is required or bundled. On this repo's
game server, set:

```text
seta g_forceDestruction 1
```

Living dark-side players receive the ability automatically. Select **Destruction**
in the force wheel and press Use Force, or bind it directly:

```text
bind x force_destruction
```

The wheel places it after Repulse and before Lightning. Missing powers are
skipped without moving Destruction to the end of the wheel.
Dash stays before Speed's position, and Stasis/Repulse after Sense's position,
even when those real powers are not owned.
Holding Use Force does not repeat the cast. No Lightning rank is required.
Turning the server setting off or changing to the light side removes the grant.
Unsupported servers do not get the wheel entry or client-generated command.

## Server settings

| Setting | Default | Effective range |
| --- | ---: | ---: |
| `g_forceDestruction` | 0 (disabled) | 0 / 1 |
| `g_forceDestructionCost` | 50 Force | 1–100 |
| `g_forceDestructionDamage` | 90 | 1–500 |
| `g_forceDestructionRadius` | 160 units | 16–512 |
| `g_forceDestructionSpeed` | 900 units/second | 100–3000 |
| `g_forceDestructionCooldown` | 4000 ms | 500–30000 |

Out-of-range tuning values are clamped when casting. The orb expires after 15
seconds, does not home, and costs no ammunition. Casting has 650 ms of recovery
and delays Force regeneration for one second. Successful casts end spawn protection.
Damage/radius are captured at launch, so tuning changes cannot alter an in-flight blast.

Direct hits use the full configured damage. Splash falls off with distance from
the target's bounds and requires line of sight; the direct target is not hit twice.
Armor, Protect, normal damage attribution and friendly-fire scaling still apply.
Absorb counters the blast as level-three Force energy: levels 1/2 reduce it, and
level 3 cancels it while converting some spent energy to Force. Ysalamiri blocks
its damage. Teammates with friendly fire disabled, protected duel bystanders,
and players in other dimensions receive neither damage nor knockback.

Spectators, dead players, racers and non-Jedi cannot cast. The usual offensive
Force restrictions apply, including Ysalamiri, vehicles, restricted duels,
broken arms, saber restrictions, grabs and saber locks. Casting is blocked while
paused, during intermission, or before a Siege round starts. Disabling all normal
Force powers also prevents the grant; bit 21 of `g_forcePowerDisable` (or its FFA
variant) can disable only Destruction.

## Compatibility and checks

The grant occupies bit 21 of `forcePowersKnown`; wheel slot 21 is display-only.
The client also requires the server's `g_forceDestruction` advertisement, so an
unrelated mod using the same spare bit does not accidentally expose this ability.
`NUM_FORCE_POWERS`, networked force arrays, force configuration strings, and real
`forcesel` values stay unchanged. The dedicated `force_destruction` command is
validated by the server on every request. The missile uses the existing concussion
entity type with a reserved `generic1` marker (213). Its networked FX overrides
reference only stock `concussion/shot` and `concussion/explosion` assets, including
on saber-only maps. Vanilla and older clients therefore see a normal concussion
projectile and explosion without downloading custom assets. Damage and knockback
remain server-authoritative; vanilla clients do not gain the new wheel entry.

On JoF JA+ V123, the client keeps a server-granted Destruction entry and command
available during jetpack flight, floating and noclip. Dead players, spectators
and followers remain excluded. The server still decides whether a cast is allowed;
keeping the noclip entry visible does not authorize a noclip cast.

V123's hand charge uses `PW_DISINT_4` and bit 21 of `forcePowersActive`
(`DESTRUCTION_HAND_FLAG`, distinct from the grant in `forcePowersKnown`). It takes
precedence over the remote caster's vanilla Grip compatibility bit. The optional
hand EFX plays once per hand per frame, independently of the projectile EFX;
the hand file controls its appearance. The custom travelling orb emits three
layers per frame. Melee casts emit from both hands. The hand effect is hidden in
the caster's first-person view and when mind-tricked. The server keeps the hand
flags on during its 250 ms charge, then
clears them in the snapshot that introduces the orb during the 1000 ms cast pose.
There is no client timer: emission stops as soon as the flags clear, including an
interrupted charge, and existing EFX particles fade according to their own lifetime.
Without the custom hand EFX, two passes of short crimson sprites drift forward
from the left hand (both hands at 1.15x size for Melee) and expire within 150 ms.

Updated clients recognize the marker and use stock concussion effects and their
vanilla impact audio by default, with Force Push cast audio and Lightning's wheel
icon. Custom Destruction effects, sounds and icons are not included in this repo
or `jofclient-assets.pk3`. Optional locally installed media can override these
defaults independently at the following paths:

- Trail: `effects/forcedestruction/destruction.efx`.
- Hand charge: `effects/forcedestruction/destruction_hand.efx`.
- Impact: `effects/forcedestruction/destruction_explode_enhanced2.efx`, falling
  back to `destruction_explode.efx` if the enhanced effect is unavailable.
- Wheel icon: `gfx/forcedestruction/force_destruction.tga`.
- Cast sound: `sound/forcedestruction/destruction.mp3`.
- Impact sounds: `sound/forcedestruction/forcedestruct01.wav` and
  `forcedestruct02.wav`, alternating by missile entity number.

Optional custom impact EFX must not contain their own `Sound` blocks: cgame
selects and plays impact audio independently, preventing doubled sounds and
allowing audio fallback. Custom WAV sound effects must be mono for engine
compatibility.

Only the custom projectile EFX is stacked; stock concussion shots and all impacts
play once. `DESTRUCTION_EFX_LAYERS` in `cg_local.h` tunes only orb density.
Missing trail/impact effects use stock concussion visuals. A missing icon uses
Lightning's icon; missing cast audio uses Force Push; a missing impact sample uses the other
sample, or stock mine-impact audio if neither is available. Stock concussion
effects retain their own audio. The server advertises only stock FX and a tagged
stock Force-push sound event, so vanilla clients do not need custom downloads.
Normal weapon visuals and sounds are unchanged. Gameplay/damage does not depend
on which assets a client has installed.

Run the focused regression checks (add `-A x64` or `-A Win32` when using Visual Studio):

```text
cmake -S tests/force-destruction -B build/force-destruction-check
cmake --build build/force-destruction-check --config Release
ctest --test-dir build/force-destruction-check -C Release --output-on-failure
```

These compile the real ability, Force restriction/Absorb helpers, wheel builder,
input routing and FX selection/playback code against mocked engine services;
they are not an in-game test. Media checks cover all 256 combinations of
missing/present trail, hand effect, both impact effects, icon and three sound
files, as well as unchanged ordinary weapon FX/sounds. The asset check ensures that custom
Destruction media and its retired packaged credit are not bundled.
Hand checks cover single custom EFX playback independently of the projectile,
two sprite fallback passes, lifetimes and forward movement, normal/Super sizes,
both hands, matrix reuse, first-person/mind-trick
hiding, stopping emission when flags clear, and unchanged Push/Grip routing.
Wheel checks include all 128 combinations of extra grants and their anchor powers,
plus flying/noclip eligibility and protected spectator/dead input states.

In-game checks before release:

- Enable/disable while connected; change sides, die/respawn, spectate and reconnect.
- Cycle both directions, let the wheel fade, hold/release Use Force, and use the direct bind.
- Try insufficient Force, rapid command spam, no-Force areas, vehicles and melee grabs.
- Test a direct hit, splash falloff, cover, nearby self-damage, NPCs and breakable props.
- Test Absorb 1/2/3, Protect, friendly fire off/on, separate duels and dimensions.
- Check the effect, sound and icon at different frame rates, then join an unmodified server.
- Join with a vanilla client and no custom assets; verify projectile and explosion
  visibility on a saber-only map. On an updated client, test a missing asset pack,
  each missing EFX, icon and sound file separately; then restore the pack
  and confirm custom visuals. Check normal concussion shots alongside Destruction.
