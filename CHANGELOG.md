# Changelog

All notable changes to Tagoror are recorded here. Versions follow
[semantic versioning](https://semver.org), and the format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.0.0] — 2026-09-18

Two new pages and a settings screen that is no longer a dropdown. The version
jumps to 2.0.0 because the panel stopped being "a list of notes with a calendar
attached": it now has four pages, and birthdays are kept in a file of their own.

### Added

- **Birthdays.** A page of their own, reached from the cake button in the
  header. The next birthday due is highlighted on a card — whether it falls
  today or in three months — and the rest are listed under month separators.
  Each person keeps a name, a day (the year is optional: without it there is no
  "turns 32"), a free-text relation, and an optional reminder that rings on the
  day through the same alarm as everything else. Today's card offers to mark
  them as greeted, a mark that expires on its own at the end of the year.
  - A birthday is **not** a yearly reminder note. Twenty of them in the note
    list would be twenty cards nobody wants to read there.
  - The list can be ordered **by how long is left** — the current month first,
    wrapping round the year — or **by the calendar**, January to December. The
    choice is remembered.
  - A 29 February is only ever a 29 February: it waits for the leap year
    instead of sliding to the 28th, the same rule repeating reminders follow.
- **Renaming checklist items.** Each row has a pencil that turns its text into a
  field; Enter or clicking away saves it, Escape leaves it alone. Clearing the
  field does **not** delete the item — there is a button for that, and emptying
  it by accident must not cost the line.

### Changed

- **Settings are a page, not a dropdown.** A menu row had nowhere to put a
  switch, a slider and a folder path without becoming a column taller than the
  panel it covered. The accent, the opacity, the language, the window options,
  the storage folder, the backups and the microphone now live on a page built
  like the calendar and the birthdays pages. Backups and the custom accent
  colour stay as their own menus, reached from a button: they are dialogues
  with their own flow, not rows.
- The header title names the page you are on, and **Escape closes any page**
  back to the note list.
- **Birthdays are stored in `birthdays.json`**, beside `notes.json` in the same
  data folder, so they still travel with the notes and still ride the USB
  stick. A folder from an earlier version has them inside `notes.json` and they
  are moved across on first run. Both halves are backed up together under one
  timestamp, and restoring one restores the other; a copy from before the split
  has no birthdays half, and then the current ones are left alone rather than
  wiped.

### Fixed

- **A checklist item that was one long unbroken word clipped every card in the
  list.** `wordWrap` breaks at spaces, so a password or a separator-less file
  name demanded the width of that whole word as the card's minimum — measured,
  335px against the 284 the list has — and one card over the edge clips all of
  them. The card's minimum no longer depends on what anybody types.

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
