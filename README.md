# Dead Island - Discord Rich Presence (RPC)

Shows what you're doing in Dead Island on Discord.

## What it does

- Shows current location/map area
- Displays active quest name
- Shows your character (Xian Mei, Sam B, Purna, Logan, or Ryder)
- Displays player level and XP
- Shows story completion percentage
- Tracks session playtime

## Installation

1. Download `DeadIslandRPC.asi` from releases
2. Copy it to your game's directory (where the .exe is)
3. Make sure you have an ASI loader installed
4. Launch the game

## Building

Requirements:
- Visual Studio 2022 with C++ Desktop Development
- Windows SDK

## How it works

Uses memory scanning and function hooking to read game state from the engine and game DLLs. Communicates with Discord via named pipe IPC.

## Technical Details

- Hooks into `engine_x86_rwdi.dll` and `game_x86_rwdi.dll`
- Reads level names via exported IGame/ILevel functions
- Detects character selection and player stats through memory offsets
- Reads active quest from the quest manager
- Communicates with Discord via named pipe IPC

## Supported Version

Original 2011 Dead Island only. Definitive Edition is not supported.

## Troubleshooting

Check `DeadIslandRPC.log` in your game folder for diagnostic information.

## License

See LICENSE.txt
