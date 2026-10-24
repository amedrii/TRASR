# TRASR

TRASR is a prototype race client for **Tomb Raider: Anniversary**. It lets two players find a match, start a run together, track each other's status, and automatically submit a verified finish.

> This is an early Windows-only prototype. Use it with a legitimate installed copy of Tomb Raider: Anniversary.

## What it currently does

- Sign in with Speedrun.com.
- Join a matchmaking queue for a chosen level and category.
- Show your opponent's current race status.
- Let either player forfeit an active match.
- Detect the level-completion screen and submit the finish automatically.
- Declare the first accepted finish as the winner and store the result on the server.
- Coordinate a five-second ready countdown before attempting to navigate the game menus.

## Current limitations

- Automatic menu navigation is currently implemented for **Final Conflict** only.
- The player must have Tomb Raider: Anniversary focused with **Play Game** selected before both racers are ready.
- Screen-based completion detection uses Tesseract OCR. Packaged releases will include its required runtime; source builds need it installed separately.
- The project is still under active development and should not yet be considered a ranked-racing client.

## Playing a match

1. Start TRASR and sign in with Speedrun.com.
2. Select the agreed level, category, and subcategory.
3. Join a queue and wait until a match is found.
4. Open Tomb Raider: Anniversary and leave it on the main menu with **Play Game** highlighted.
5. Both players press **Ready** in TRASR.
6. During the five-second countdown, focus the game window. TRASR will navigate to the selected level and start both players together.
7. Finish normally. TRASR watches for the game’s completion screen and submits the result.

If you no longer want to continue, use **Forfeit match**. This immediately awards the match to your opponent.

## Installation

When a release is available, download the Windows installer from the [Releases](../../releases) page, run it, and follow the installer prompts.

The installer is intended to include the client and screen-detection dependencies, so players should not need to install Python or Tesseract themselves.

## Building from source

### Client

Requirements:

- Windows
- Qt 6 with MinGW
- CMake
- Python 3.13 or later
- Tesseract OCR

Install the Python packages used by the completion detector:

```powershell
py -3.13 -m pip install -r tools\requirements-level-end-detector.txt
```

Open `CMakeLists.txt` in Qt Creator, configure a Release kit, and build the `TRASR` target.

For source builds, make sure Tesseract is installed at:

```text
C:\Program Files\Tesseract-OCR\tesseract.exe
```

## Backend deployment

The `worker` folder contains the Cloudflare Worker and D1 database migrations used for matchmaking, player status, match outcomes, and forfeits.

Before deploying an updated Worker, apply any new SQL files in `worker/migrations` to the production D1 database, in numeric order. Then deploy `worker/worker.js` through the Cloudflare dashboard or Wrangler.

## Project layout

```text
main.cpp                 Application entry point
trasr_window.*           Client interface, matchmaking, and race controls
run_monitor.*            Game monitoring and finish submission
screen_completion.*      Reads completion events from the detector
tra_process.*            Tomb Raider: Anniversary process/memory reader
tools/                   Completion detector and menu automation scripts
worker/                  Cloudflare Worker and D1 migrations
```

## Contributing

Please open an issue or pull request with a clear description of the problem or change. In particular, reports for additional level-end detection routes are very useful.

## Disclaimer

TRASR is an unofficial fan-made project. It is not affiliated with Crystal Dynamics, Eidos, Square Enix, or Speedrun.com.
