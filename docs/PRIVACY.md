# Privacy

Eternal Launcher has no analytics, no telemetry and no ads. It talks to the network only to
do what a launcher for this group's server needs, as listed below. Everything else stays on
the computer, in `%APPDATA%\.eternal-launcher`.

## What is sent, and to whom

| Where | When | What |
| --- | --- | --- |
| The group's file server (`eternal.ryanbarreto.dev`) | On start and on Jogar | Requests for the mod list, the mods, the news and launcher updates. |
| The same server, play time | While playing the group's modpack | The player's name when a session starts, then a heartbeat every minute. The server keeps the total time, the days played and the last session, which make up the ranking and the achievements. |
| The same server, ranking and achievements | On Home | The player's name, to read their position, level and achievements. |
| The same server, gallery | Only when the player shares | The screenshot or skin they chose, with their name. Reports send nothing but the picture's id. |
| The group's account server (`accounts-eternal.ryanbarreto.dev`) | Signing in, creating an account, first access, changing skins | The name and password typed, over HTTPS, in exchange for a session token. The password is never stored; the token is stored encrypted for the Windows user (DPAPI). |
| Mojang and Microsoft | Installing and playing; looking up a skin by name | Requests for the official game files, and the names typed in the skin search. Microsoft sign-in only in Conta original mode. |
| optifine.net | The first time Minecraft 1.8.9 is played | The download of OptiFine. |
| Eclipse Adoptium (`api.adoptium.net`) | When Java is missing | The download of a portable Java runtime. |
| authlib-injector (`authlib-injector.yushi.moe`, GitHub) | When it is missing | The download of the pinned authlib-injector release. |

Like any web request, these servers see the computer's IP address. The group's servers keep
it only to limit abuse (uploads per day, sessions per address).

## Removing your data

Ask an admin of the group to remove your play time, your account or anything you shared.
Deleting `%APPDATA%\.eternal-launcher` removes everything the launcher kept on the computer.
