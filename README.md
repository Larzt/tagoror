<h1 align="center">Codex</h1>

<p align="center">
  A sticky-notes widget for the Linux desktop, written in C++20 with Qt 6.<br>
  Frameless, translucent, and made to sit <em>on</em> the desktop rather than get in
  the way — text notes, checklists, reminders that actually ring (with a month
  view to go with them), and voice notes with a waveform.
</p>

<p align="center">
  <img src="docs/brand.png" alt="Codex: the app icon next to the notes panel" width="720">
</p>

> The interface is in Spanish, where the app is called **Códice**.

## Features

<p align="center">
  <img src="docs/panel.png" alt="The panel with a checklist, a reminder, a voice note and a text note" width="380">
</p>

### Four kinds of note

| | |
|---|---|
| **Text** | A plain note whose editor grows with the content. |
| **Checklist** | Items you can tick off (done ones get struck through), a progress bar, and an add row that reads as a pending task rather than a form. |
| **Reminder** | A real date, not just a label: when the time comes it rings until you stop it. |
| **Voice** | Records from your microphone and draws the waveform of what you said. |

<p align="center">
  <img src="docs/new-note.png" alt="The new-note selector" width="260">
</p>

### Reminders that actually go off

- Presets (in 5 minutes, in an hour, 18:00, tomorrow at 9:00), a date typed by
  hand, or a day picked on the calendar.
- Click the date chip **or** right-click the note to change it.
- Colour tells you the state at a glance: a muted clock while it is still ahead,
  a red bell once the time has passed — whether or not it has already rung.
- When one fires it plays a looping tone until you stop it, from the button on
  the note, the note's menu, or simply by opening the panel.
- Folded away, the dock itself turns into a red bell, so an alarm is visible
  even when the panel is not.

### A month view for them

<p align="center">
  <img src="docs/calendar.png" alt="The calendar page, with a day's reminders listed below the month" width="380">
</p>

- The calendar button in the header swaps the note list for a month grid; the
  same button swaps back.
- Every reminder shows up as a dot on its day, coloured one by one: accent for
  what is still ahead, red for what has passed.
- Pick a day to list its reminders by time, click one to jump to its note, or
  add a new one straight from the day — a preset hour or a time you type.
- Reminders with a free-text date have no instant to place, so they stay in the
  list and out of the grid.
- The month moves with the arrows, the mouse wheel, or **Hoy** to come back.

### Voice notes

- Records 16-bit PCM WAV straight from the microphone.
- The waveform is drawn **while you record**, so a muted or wrongly-routed
  microphone is visible immediately instead of leaving you with a silent file.
- If a take comes out with no audible signal, the note says so.
- Click anywhere on the waveform to seek; the played part fills with the accent
  colour.
- The input device is selectable, and falls back to the system default if the
  one you picked is gone.

### The window

- **Lives on the desktop** by default, below other windows. "Always on top" is
  an option, not the default.
- Drag it by its header, resize it from the bottom-right corner, or fold it into
  a dock you can also drag around.
- Frameless and translucent, with adjustable opacity.

<p align="center">
  <img src="docs/dock.png" alt="The collapsed dock" width="100">
  &nbsp;&nbsp;&nbsp;
  <img src="docs/settings.png" alt="The settings menu" width="260">
</p>

### Look and feel

- Accent colour from a swatch or any hex value you type, plus an opacity slider.
- Every menu is drawn by the app itself — no native `QMenu` — so right-clicking
  a note gives you the note's own options instead of a cut/copy/paste menu.
- No image assets at all: the icons are drawn with `QPainter` and the alarm tone
  is synthesised on first use.

### Other

- Filter notes as you type.
- An empty panel offers a button to create the first note.
- Everything is saved automatically, a moment after you stop typing.

## Building

Requires **Qt 6** (Widgets and Multimedia), **CMake ≥ 3.25**, **Ninja**, and a
C++20 compiler. Developed against Qt 6.11.

```sh
# Arch / CachyOS
sudo pacman -S qt6-base qt6-multimedia qt6-multimedia-ffmpeg cmake ninja

# Debian / Ubuntu
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build
```

Then:

```sh
make          # configure + build
make run      # build and launch
make clean
```

Or with CMake directly:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/codex
```

Playback goes through Qt Multimedia's FFmpeg backend, so that plugin has to be
installed — on Arch it ships separately as `qt6-multimedia-ffmpeg`; on Debian it
comes with `qt6-multimedia-dev`.

## Installing as a desktop app

To get it in your application menu instead of running it from a terminal:

```sh
make install          # into ~/.local — no root needed
```

That builds an optimised binary and installs the executable, a `codex.desktop`
entry, and the icon at every size. Look for **Códice** in your application menu;
the launcher may take a few seconds to notice it the first time, or a logout to
be safe.

```sh
make install PREFIX=/usr/local   # system-wide instead (needs sudo)
make autostart                   # also open it when you log in
make autostart-off               # stop doing that
make uninstall                   # remove it (your notes are kept)
```

The `.desktop` entry records the full path of the installed binary, so it works
whether or not the prefix is on your `PATH`.

Only one copy runs at a time: launching it again while it is already open just
brings the existing panel to the front, so two windows can never fight over the
same `notes.json`.

Notes kept by earlier versions under `Stride/Abyss` or `Stride/NotasWidget` are
moved across automatically the first time you run it, and so is the data folder
you had chosen by hand — a rename must never strand anyone's notes.

## Where your notes live

```
~/.local/share/Stride/Codex/
├── notes.json     # notes, accent, opacity, window size, preferences
├── alarm.wav      # the generated alarm tone
└── audio/         # one WAV per voice note
```

The folder is configurable from settings. Changing it copies the voice
attachments across and leaves the originals where they were, so nothing is lost
if the copy fails.

## Layout

The code is split into three layers, and headers mirror the sources:

```
include/core/    note, paths, store        the data and where it is kept
include/ui/      panel, notecard, calendar, popup, theme, waveform, dragwidgets
include/audio/   recorder, alarm, wave     microphone, alarm tone, WAV
src/             the matching implementations, same folders
packaging/       .desktop template and the rasterised icons
codex.svg        app icon
codex-small.svg  simplified variant, used below 32px
```

`core` knows nothing about the interface, `ui` never touches the disk — it goes
through `Store`, which owns the notes and the JSON file — and `audio` only deals
with the microphone and WAV files.

The icon ships twice on purpose: the detailed five-ring mark as the scalable
SVG, and the simplified three-ring one rasterised into the small sizes, where
the thin rings would otherwise blur together. `packaging/render-icons.sh`
regenerates the PNGs when either SVG changes.

`wave.hpp` is a small header-only RIFF/WAVE reader and writer — enough for the
16-bit PCM files the app records, and it declines anything else rather than
guessing.

## Known limitations

- Moving, resizing, and window stacking are delegated to the compositor
  (`startSystemMove` / `startSystemResize`), which is what makes them work on
  Wayland. In exchange, on Wayland "always on top" and "on the desktop" are
  requests the compositor may decline.
- Reminders are checked every 5 seconds, so an alarm can be up to that late.
- It is a utility window by design, so it stays out of the taskbar and the
  alt-tab list. Fold it into the dock instead of minimising it.
- There is no automated test suite yet.
