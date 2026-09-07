# Changelog

All notable changes to Tagoror are recorded here. Versions follow
[semantic versioning](https://semver.org), and the format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.5.0] — 2026-09-07

This release exists because Tagoror lost someone's notes. A storage folder on a
USB stick that was not mounted yet made the app believe it was a fresh install,
and the demo notes it seeded were then written over the real file the moment the
stick appeared. Everything below follows from that: one rule for the bug, and a
backup system so that a bug of that shape can never again be the end of the
story.

### Fixed

- **The app no longer conjures a storage folder that is not there.**
  `appDataDir()` used to `mkpath()` a hand-picked folder, which on an unmounted
  volume silently built the whole path on top of the empty mount point. A
  chosen folder is now never created on startup — it was created when it was
  chosen, and if it is missing, it is missing. `audioDir()`/`imageDir()` carry
  the same guard.
- **A file that cannot be read is never overwritten.** `Store::load()` used to
  treat "cannot open `notes.json`" as "fresh install", seed the demo notes, and
  let the next save truncate the real file. It now distinguishes three
  outcomes — no file, a file that reads, and a file that should be there and
  cannot be reached — and in the third it goes read-only: nothing is seeded and
  `save()` writes nothing at all.
- **Saving is atomic.** `notes.json` is written to a temporary file and renamed
  over the old one, so a write interrupted halfway — the drive pulled, the
  machine cut — can no longer leave a JSON that does not parse. A `notes.json`
  that does not parse is every note, not the last few.
- With the storage folder missing, the interface follows the system language
  instead of always falling back to Spanish. There is no saved preference to
  read in that state, and the warning is the only thing on screen.

### Added

- **Backups.** A copy of `notes.json` is set aside in `<data>/backups/` before
  it is overwritten. The ten most recent are kept and older ones are pruned.
  Backups live inside the storage folder, so they travel to the USB stick with
  the notes, and changing the folder carries the history across.
- **A menu of their own**, at *Settings → Backups…*:
  - *Make a copy now*, for a copy on demand.
  - *How often* — never, or every 1, 3, 7, 15 or 30 days.
  - *At what time* — every three hours as a preset, or any `HH:mm` you type.
  - The next scheduled run, spelled out, so the schedule can be checked.
  - *Go back to a copy* — the list of what exists, with date and note count.
    Restoring sets the current state aside as a copy first, so restoring the
    wrong one is not the mistake there is no coming back from.
- A scheduled copy is made even if the app was closed at that hour: it catches
  up on the next launch, over the file as just read.
- **A warning under the header** while the storage folder is unavailable, saying
  plainly that nothing is being saved. A panel that opens empty and silent is
  how notes get lost without anyone noticing.
- The folder is **picked up on its own** when it appears — a USB stick mounted
  ten minutes after login is noticed within five seconds, and anything typed in
  the meantime is kept, not discarded.
- **Choosing a storage folder that already holds notes now asks** which you
  meant: open the notes already there, or move these ones over. Assuming the
  second is what overwrote the file in the first place.

### Changed

- The settings row for the storage folder shows *Unavailable* when it is, and
  the backups row shows the schedule.
- `Popup` gained `addChoice()`: chips where one is marked as the current
  setting, as opposed to `addChips()`, which is a list of actions.

### Infrastructure

- **Releases are published automatically.** A push to `main` carrying a version
  that has no tag yet is the release: `.github/workflows/release.yml` tags it,
  builds the AppImage, the double-click installer and both Windows packages,
  and attaches them with the notes taken from this file. A missing changelog
  section fails the release before anything is compiled.
- `windows.yml` gained a `workflow_call` trigger so the release reuses its build
  instead of repeating it.

### Notes on versioning

The last released tag was `v1.3.1`; the version in `CMakeLists.txt`, the
`PKGBUILD` and the Flatpak manifest had been left behind at `1.2.0`, and one
commit since is titled `1.4.1` without ever having been tagged or bumped. This
release moves every one of those to **1.5.0** at once — a number ahead of both
readings, so no packaging channel sees a version go backwards.

## [1.3.1] — 2026-08-23

Tagged; the version files were not bumped at the time.

- MIT `LICENSE` added at the root and installed with every package.

## [1.3.0] — 2026-08-21

Tagged; the version files were not bumped at the time.

- Rebranded from Codex to Tagoror, carrying existing notes over from the old
  folder and settings scope.
- Card reordering by drag, more foldable sections, images attached to notes.

## [1.2] — 2026-08-21

- Language selector (Spanish/English), a system tray icon, and a panel that
  folds and unfolds without wandering off its corner.

## [1.1] — 2026-08-21

## [1.0] — 2026-08-21

[1.5.0]: https://github.com/Larzt/tagoror/releases/tag/v1.5.0
[1.3.1]: https://github.com/Larzt/tagoror/releases/tag/v1.3.1
[1.3.0]: https://github.com/Larzt/tagoror/releases/tag/v1.3.0
