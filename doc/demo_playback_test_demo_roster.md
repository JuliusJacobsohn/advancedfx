# Demo Playback Test Demo Roster

Source: `mirv_deathmsg help players` in `match730_003816038387630997543_1135542072_274.dem`, captured through the HLAE CS2 demo launch workflow on 2026-05-01.

Demo path:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\replays\match730_003816038387630997543_1135542072_274.dem
```

Known players:

| Name | User ID | XUID | Spectator key |
| --- | ---: | --- | --- |
| hayzinvor | 0 | x76561198723801816 | k6 |
| Godfather | 1 | x76561198772930198 | k7 |
| lou.mir | 2 | x76561199863511079 | k8 |
| im2mango | 3 | x76561199633642758 | k9 |
| Shadowwuwu:* | 4 | x76561198725839489 | k0 |
| macan enjoyer | 5 | x76561199098694372 | k1 |
| J0naszeK | 6 | x76561198721306201 | k2 |
| Donald J. Trump | 7 | x76561198057189628 | k3 |
| Wladimir Putin | 8 | x76561197960680616 | k4 |
| Benjamin Netanjahu | 9 | x76561197962023477 | k5 |

Useful test target:

```text
mirv_chat_insert byXuid x76561197962023477 team=CT alive=1 visibility=team location=A_Ramp message rotate now
```

The `byUserId` equivalent for this player is:

```text
mirv_chat_insert byUserId 9 team=CT alive=1 visibility=team location=A_Ramp message rotate now
```
