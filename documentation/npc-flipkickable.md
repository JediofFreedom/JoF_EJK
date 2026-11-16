# Flipkickable NPCs

Add `Flipkickable 1` inside an NPC definition in `ext_data/NPCs/*.npc` to
give that NPC the player rules for flipkicks, fall damage and standing on its head:

```text
martial_artist
{
    playerModel kyle
    class CLASS_JEDI
    health 100
    Flipkickable 1
}
```

The parameter name is case insensitive. `Flipkickable 0`, or omitting the
parameter, keeps the existing NPC behavior. Vehicles are excluded.

Flipkicks still require the server's `g_flipKick` setting and follow its existing
damage, knockdown, friendly-fire and duel rules. With this repository's server
game module, `g_flipKick` already lets players flipkick every NPC, so there the
parameter only adds the fall damage and head-standing rules below.

Enabled NPCs take normal fall damage and knockdown splat damage, using
`g_maxFallDmg` and `DF_NO_FALLING` in the same way as players. That includes
falls from their own movement: a Jedi NPC whose AI jumps down a drop too high
for its force jump level gets hurt, as a player would. Players can stand on
their heads without the NPC auto-bounce, and `g_slideOnPlayer` controls head
sliding for them too.

Use the updated server game module to read the parameter and the updated client
game module for matching movement prediction. On JA+ servers the client predicts
these rules only when the server sets the flag, which needs a matching JA+ server
change. The opt-in travels in entity flag bit 31 (`EF_NPC_FLIPKICKABLE`), the one
bit neither JA+ nor JoF uses; no engine protocol change is required.

## Regression checks

The standalone tests exercise the production parser dispatch, server/client
flipkick eligibility, NPC head bounce, and fall/splat event handler:

```sh
cmake -S tests/npc-flipkickable -B build/npc-flipkickable-tests
cmake --build build/npc-flipkickable-tests --config Release
ctest --test-dir build/npc-flipkickable-tests -C Release --output-on-failure
```
