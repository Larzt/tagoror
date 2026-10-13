# Changelog

All notable changes to Tagoror are recorded here. Versions follow
[semantic versioning](https://semver.org), and the format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [5.0.3] — 2026-10-13

Checklists that ring, and the minimise button where you expect it.

### Added

- **Checklists can have a reminder.** Optional: *Añadir recordatorio* in the
  list's menu gives it a date, with the same quick picks and repetition as a
  reminder. A dated list shows its date above the items, rings, appears on the
  planner and can be silenced or postponed like any reminder. *Quitar fecha*
  takes it back to a plain list.

### Changed

- **In app mode the minimise button comes after ⋯**, on the right edge where
  window buttons usually sit. As a panel, folding to the dock stays before ⋯.

## [5.0.1] — 2026-10-10

Folding in one click, resizing that works on Windows, and no more flashing window.

### Changed

- **Folding to the dock is back in the header**, a button next to ⋯ on every
  page, so the panel gets out of the way in one click instead of two. In app
  mode the same button minimises. Ctrl+M and the entry in ⋯ still work.
- New screenshots of the 5.0 interface and the new icon in the README, on the
  website and in the AppStream data.

### Fixed

- **A tiny window flashed in the middle of the desktop** when switching area
  (or anything else that rebuilds the list) with a checklist or a repeating
  reminder in it. The pencil of each checklist item and the repeat icon were
  shown before being placed in their card, and a widget shown with no parent
  opens as a window of its own until it is placed.
- **Resizing from the edges on Windows.** The grab band outside the frame let
  clicks through to the window behind wherever the shadow faded out — along
  the whole top edge, among others — and the band over the frame itself was
  3 px wide with no resize cursor. Each edge and corner now has a grip of its
  own that always takes the mouse and shows the cursor, with larger corners.

## [5.0.0] — 2026-10-09

A simpler interface: one fixed place for everything.

### Added

- **Markdown notes.** A note type of its own (Ctrl+⇧M): the card shows it
  rendered, and clicking opens its page — the raw text with the syntax dimmed
  and line numbers, or the preview (Ctrl+E), with a formatting bar (bold,
  italic, heading, list, code; Ctrl+B and Ctrl+I) while you edit. There Enter
  is a new line and Ctrl+Enter takes you back.
- **Fold any note** to its title with the chevron. Folded, it still says what
  matters: a checklist's 2/4, a reminder's time (in red while it rings), a
  recording's length.
- **Sync with more than Google Drive.** *Settings → Sync* offers a **WebDAV**
  server (Nextcloud, ownCloud, most NAS — best with an app password) or a
  **synced folder** kept in step by Syncthing, Dropbox, OneDrive or the
  Nextcloud client, besides Google Drive. All three merge note by note, so two
  computers editing at once don't overwrite each other. Choose when it runs: on
  save, every 5 minutes, or by hand. If you already used Drive, nothing changes.
- **Google Calendar is a connection of its own**, independent of where the
  notes sync: it works with any of the three, or with none.
- **Settings in tabs**: General, Appearance, Sync, Shortcuts (every shortcut
  in one place) and About.
- **A start screen** that says what it is waiting for — loading, syncing,
  looking for updates — while the stones of the tagoror light up in a ring.
  It only appears when there is something to wait for, and never when
  Tagoror starts hidden: the new `--tray` option starts it straight into the
  tray, for autostart.
- **Command palette (Ctrl+K)**, also in the ⋯ menu: type to go to any page,
  create any kind of note or switch area, from anywhere. Accents don't matter —
  *cumple* finds Birthdays, *recor* a new reminder — and ↑↓, Enter and Esc
  drive it.

### Changed

- **Text notes are plain text.** What you type is what you see. Text notes you
  had already written in Markdown were turned into Markdown notes when 5.0
  first opened them, so none of your formatting was lost.
- **Navigation that stays put.** Notes, Planner, Timers and Birthdays live in a
  bar along the bottom, with the open page marked; widen the panel past 480 px
  (the planner does) and it becomes a rail on the left. Before, each page was a
  header button that took you back to the notes when pressed again. Ctrl+1…4
  open them.
- **The header is just the page's name and ⋯** — settings (Ctrl+,), app mode,
  fold to the dock (Ctrl+M) and *About Tagoror* moved there. Settings has a
  back arrow; Escape still closes any page.
- **One row for the areas, search and new notes.** The search box unfolds over
  the tabs when you press the magnifier or Ctrl+F, and says so when nothing
  matches. The **+** opens the note types with their shortcuts: Ctrl+N for a
  text note straight away, Ctrl+⇧L, Ctrl+⇧R and Ctrl+⇧V for the others.
- **Area shortcuts are now Alt+1…9** (Ctrl+1…4 belong to the pages).
- **One place for notices.** A ringing reminder, birthday, timer or event, a
  data folder that isn't there, a failed sync and a new version all show in a
  single card over the bottom of the list, most urgent first, with *1 of N* to
  see the rest. Alarms can be stopped or snoozed from there. Nothing slides in
  under the header pushing your notes down any more.
- **Timers, redone.** The running (or chosen) timer sits on top with its
  ring and *Pause*, *+1 min* and *Reset*; the rest are listed as saved, and
  the *Quick* chips (1′ to 45′) start one at once. The **+** in the header
  opens the form for a named one.
- **Birthdays, redone.** The next one sits on a card with a soft colour
  backdrop and the days left in large figures; everyone else is a card in a
  grid of one or two columns, by month. **+** in the header adds one.
- **The dock is the tagoror's ring of stones.** While something rings the
  stones turn red, a halo beats around it and a badge counts the alarms.
- **The running timer's countdown** shows in the navigation, and a red dot on
  Notes tells you a reminder is ringing while you are on another page.

### Fixed

- Two computers that had never reordered their notes could keep sending their
  own order to each other on every sync; they now settle on one.

### Removed

- The footer with its counters and hints, and the resize corner — the panel
  resizes from any edge since 4.3.

## [4.3.0] — 2026-10-09

A date picker, Enter to finish, resizing from any edge, and a new typeface and icon.

### Added

- **Date picker for reminders.** The date menu now has a small month to pick
  the day, a column of times in steps of **5 or 15 minutes** (the choice is
  remembered) and quick picks — in an hour, today or tomorrow at 18:00,
  tomorrow at 09:00, next Monday at 09:00. Days already gone can't be chosen;
  the arrows, Page Up/Down and Enter work inside the grid, and *Save* confirms.
  It replaces the field where the date had to be typed as `dd/MM HH:mm`.
  A birthday's alert time is picked from the same column of times.
- **Resize from any edge.** The panel can be grabbed by any of its four edges
  or corners, not only the bottom-right one. In app mode the system frame
  already did this.

### Changed

- **Enter finishes, Shift+Enter breaks the line.** In a text note or a
  reminder's details, Enter now finishes writing (Ctrl+Enter too) and
  Shift+Enter starts a new line; the footer says so while you write. Enter in
  a note's title finishes it as well. An input method composing a word keeps
  its Enter.
- **IBM Plex Sans is the default typeface**, bundled together with IBM Plex
  Mono, which times, dates and counters already asked for but which was only
  there if it happened to be installed. They replace Libron, which is no
  longer bundled; the system's font or any installed one can still be chosen
  in *Settings → Appearance → Typeface*. Both are under the SIL Open Font
  License 1.1, installed alongside Tagoror's.
- **New icon**: seven stones in a ring around the sun, with a simplified
  version for small sizes.

### Fixed

- The *New timer* card no longer runs off the right edge of the panel at its
  minimum width.

## [4.2.0] — 2026-10-06

Search and new note next to the notes, a typeface of its own, and copying a note as text.

### Added

- **Copy a note's contents.** Right-click a text, checklist or reminder note and
  pick *Copy contents*: its title, date, text (as Markdown), checklist items as
  `- [ ]` / `- [x]` tasks and links go to the clipboard. Voice notes don't
  offer it — their content is the recording.

### Changed

- **Choose the typeface.** *Settings → Appearance → Typeface* switches between
  [Libron](https://github.com/nicoverbruggen/libron) (SIL Open Font License
  1.1), now the default and shipped inside the binary so it looks the same on
  every system, the system's own font, or any font installed on the computer —
  type its name, or just the start of it. Times, dates and counters keep their
  monospaced face. Libron's licence is installed alongside Tagoror's.

- **Search box and new-note button under the workspaces.** They left the header
  for a row of their own right below the area tabs, only on the note list. The
  search box is always there — no need to open it first — and the **+** sits
  beside it. **Ctrl+F** jumps to the search and **Ctrl+N** opens the new-note
  menu.

## [4.1.0] — 2026-10-03

A fuller tray menu, and app mode behaving like an ordinary window.

### Added

- **More in the tray menu.** Besides showing, hiding and creating notes, it can
  now **bring Tagoror to the front** (out of the desktop layer until you send it
  back), **send it to the back**, fold or unfold it (minimise in app mode),
  **open any page** directly — notes, planner, timers, birthdays or settings —
  and switch **Always on top** and **App mode** without opening settings.

### Changed

- **Size per page is now the widget's alone.** In app mode the window keeps the
  size and place you give it whichever page is open, and the switch no longer
  shows in settings. Your choice is kept for when you go back to the widget.

## [4.0.0] — 2026-09-29

Workspaces, the planner in step with Google Calendar, an ordinary-window mode,
and events you move by dragging them.

### Added

- **Workspace areas.** The note list is split into tabs (*Personal*, *Work*,
  whatever you need), each with an optional monochrome glyph. **Ctrl+T**
  creates one, **Ctrl+1…9** switch, **F2** renames, and dropping a card on a
  tab moves the note there. Deleting an area asks whether to move its notes or
  delete them. Areas sync through Google Drive; which one is open is kept per
  computer.
- **Sync with Google Calendar**, both ways, as part of the Google Drive sync.
  Turn it on per computer in *Settings → Google Calendar*; each calendar you
  follow becomes a planner category with Google's name and colour, and events
  in Tagoror's own categories go to the calendar you choose. Repeating events,
  all-day and multi-day events come across; unfollowing a calendar never
  deletes anything in Google.
- **App mode** (*Settings → Window*): Tagoror as an ordinary window with the
  system's title bar, in the taskbar and Alt+Tab, instead of the frameless
  widget on the desktop. The **–** button minimises instead of folding to the
  dock.
- **Drag to reschedule in the planner.** In the day and week views drag an
  event or reminder to another hour or day (in 15-minute steps), or drag its
  bottom edge to change when it ends; in the all-day band and the month view,
  drag it to another day. Moving one occurrence of a repeating event moves the
  series, as editing it does. Escape cancels the drag.
- The planner has **all-day and multi-day events** and a **yearly** repeat.

### Changed

- The planner's categories show Google calendars apart from Tagoror's own,
  under their own heading, both in the side panel and in the event form.
- New screenshots in the README and on the website, all in English.
- Code comments are now in English and in Doxygen format.

## [3.2.0] — 2026-09-27

Checklist items you can reorder, and a website.

### Added

- **Reorder checklist items.** Drag the grip at the end of an item's row, or
  press **Alt+↑** / **Alt+↓** on its checkbox; the list scrolls along when a
  drag reaches its edge, and the focus follows the item so it can keep moving.
- **A landing page** at <https://larzt.github.io/tagoror/>, in Spanish and
  English, with downloads for Linux and Windows.

### Fixed

- **Builds made from an older build tree reported an old version**, so the
  update check saw the release already installed as a new one. The version
  now follows `CMakeLists.txt` unless it is overridden on purpose, and the
  AppImage script passes it explicitly.

## [3.1.1] — 2026-09-27

A tidier settings page, and pages that no longer push each other around.

### Changed

- **The settings page is organised into sections.** Each one has a header
  with its icon and keeps its options together in one group, separated by
  thin lines; accent, opacity and text size are gathered under *Appearance*,
  and the language and text-size choices are a single segmented control with
  equal widths instead of loose buttons.

### Fixed

- **One page's size moved the others.** With *Size per page* on, a large page
  that opened leftwards or upwards near the screen edge dragged the window's
  corner along, and every page after it opened from there. All pages are now
  placed from the note list's position, so each one always opens in the same
  place, and folding to the dock and back no longer shifts anything.

## [3.1.0] — 2026-09-27

Categories of your own in the planner, a choice of how early an event alerts,
an alarm that no longer rings forever, and a window size for each page.

### Added

- **Your own planner categories.** Create them with a name and one of ten
  colours from *New category* in the side panel or *New…* in the event form;
  the pencil next to each one renames it, recolours it or deletes it (its
  events move to *Other*). They live in `events.json`, are backed up with the
  events and sync through Google Drive like everything else.
- **How early an event alerts**: none, at the start, 5, 10, 15 or 30 minutes,
  1 or 2 hours, or a day before. Existing events keep their ten minutes.
- **Size per page.** The planner, timers, birthdays and settings each remember
  their own window size, so enlarging one no longer enlarges the note list.
  It can be turned off in settings (*Window → Size per page*).

### Changed

- **The alarm goes quiet after a minute** instead of looping until someone
  stops it. Whatever rang stays red on its card, in the alarm strip and on the
  dock and tray icon until it is acknowledged. A new alarm restarts the minute.
- The alarm strip says the day, not just the time, when an event alerted the
  day before.

### Fixed

- **Closing a page selected the first note's title**, as if to edit it: the
  focus now stays on the page's header button.

## [3.0.0] — 2026-09-22

A planner in place of the calendar, timers, Markdown in text notes, a text-size
setting and a panel that can be used without the mouse.

### Added

- **Planner.** The calendar button now opens day, week and month views with
  events and tasks of their own: a start and an end, a category, an optional
  daily, weekly or monthly repeat, and an alert ten minutes before. Tasks are
  ticked per day, so a repeating one is not marked done all at once. Reminders
  and birthdays are drawn on the same grid and can be hidden by category. The
  panel widens while the planner is open and goes back to its size afterwards.
  Events live in `events.json`, backed up together with the notes.
- **Timers.** A page of their own from the stopwatch button: create one with a
  name and a duration (or 1/5/10/25 minutes), pause, reset or delete it. A
  timer stores when it ends, so time keeps running with the app closed. The
  running one counts down in the footer; when it finishes the alarm plays and a
  red strip offers *+1 min* or *Stop*.
- **Markdown in text notes** (and in reminder details): headings, bold,
  italic, code, lists, checkboxes, quotes and links are shown formatted and turn
  back into plain text while you edit. Single line breaks are kept, so existing
  notes look the same.
- **Text size**, in four steps with a live sample, from settings. It scales
  what you write and leaves the chrome alone.
- **Keyboard navigation.** Tab reaches everything clickable — menu rows,
  settings rows, switches, swatches, link rows, the date chip, timers and each
  block of the planner — Enter or Space activates it, arrows move through menus
  and the month, and the focus ring only appears when the focus came from the
  keyboard.

- **Sync between computers through Google Drive.** Connect the same Google
  account on several computers and they share notes, planner events and tasks,
  birthdays, timers, voice notes and images through a `Tagoror` folder in your
  Drive. Changes are merged element by element — the newer version of each note
  or event wins, what exists on only one side is added, deletions propagate —
  so nothing is overwritten wholesale. It asks for the narrowest permission
  (`drive.file`). Builds need an OAuth client ID (see the README); without one
  the option is shown disabled.

### Fixed

- **Deleting a reminder while it was ringing left the alarm playing**, with
  nothing left on screen to stop it. Deleting a ringing timer or event, or
  reloading the notes from a backup or another folder, now stops it too.

### Changed

- The day list under the month grid is gone with the old calendar: picking a
  day opens it in the day view instead.

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
