# Force Destruction

A Legends / Jedi Knight-inspired dark-side power: a concentrated, non-homing
energy orb that bursts on contact. It damages and knocks back nearby targets,
including its caster at close range. This is a server-granted extra ability,
like the Repulse wheel integration, not a new purchasable force-power rank.

## Enable and use

Install the updated game module, client executable/cgame, and
`jofclient-assets.pk3`. On this repo's game server, set:

```text
seta g_forceDestruction 1
```

Living dark-side players receive the ability automatically. Select **Destruction**
in the force wheel and press Use Force, or bind it directly:

```text
bind x force_destruction
```

The wheel places it after Lightning (or at the end if Lightning is not owned).
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

Out-of-range tuning values are clamped when casting. The orb expires after three
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
entity type with a reserved `generic1` marker (213) and standard FX overrides.
The new assets reuse the game's Force texture and sound; no external art is required.

Run the focused regression checks (add `-A x64` or `-A Win32` when using Visual Studio):

```text
cmake -S tests/force-destruction -B build/force-destruction-check
cmake --build build/force-destruction-check --config Release
ctest --test-dir build/force-destruction-check -C Release --output-on-failure
```

These compile the real ability, Force restriction/Absorb helpers, wheel builder,
and input-routing code against mocked engine services; they are not an in-game test.

In-game checks before release:

- Enable/disable while connected; change sides, die/respawn, spectate and reconnect.
- Cycle both directions, let the wheel fade, hold/release Use Force, and use the direct bind.
- Try insufficient Force, rapid command spam, no-Force areas, vehicles and melee grabs.
- Test a direct hit, splash falloff, cover, nearby self-damage, NPCs and breakable props.
- Test Absorb 1/2/3, Protect, friendly fire off/on, separate duels and dimensions.
- Check the effect, sound and icon at different frame rates, then join an unmodified server.
