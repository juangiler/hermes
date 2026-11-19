# Kirigami split panes (slice S2 of the Qt/Kirigami port)

## Intent and why

The legacy Hermes window is not a list of tabs. It is a **tree of panels**: the
window is divided by horizontal and vertical splits, every panel draws its own
tab strip, and every tab shows one directory. That layout is part of what the
user has, because the legacy app saves it and restores it on the next start.

The Kirigami shell shipped so far has one global tab strip and one view, so it
cannot show a split window at all, and it has nothing to restore. This slice
brings the panel tree over, so the Qt frontend can show and keep the layout the
legacy app keeps.

The behaviour to port is the legacy behaviour, exactly. Where the master parity
design described S2 as "a tab holds a tree of panes", that description is
inverted with respect to the legacy application: in the legacy, a **panel**
holds a stack of tabs and the tree is of panels, and each tab shows one
directory. This document follows the legacy, and amends the master design's S2
text accordingly.

## Decisions taken with the user (2026-11-20)

- **Mirror the legacy.** Panels own their own tab stacks and draw their own tab
  strips. The Qt shell's single global tab strip becomes one strip per panel.
- **Split the slice in two pull requests.** S2a delivers the structure; S2b
  delivers session persistence and drag-and-drop. The reason is in the master
  design's own rejection of one large PR: the review rounds in this project find
  real defects only when each round stays small enough to examine, and the
  persistence half is the first slice to touch the legacy SDL application's
  settings code, so it must not be able to block the structure work.
- **The core owns the whole settings file.** The legacy's per-key-group
  three-way merge moves into the core with the file handling, so both
  applications share one implementation and the legacy keeps its multi-process
  safety. The alternative - each application keeping its own writer - was
  rejected because it leaves the file format handled in two places and lets one
  application's save undo the other's session.

## What the builder check settled

A builder reading this design asked six questions. Five are answered by the
legacy's own behaviour and are marked in the sections above: when the session is
written (a three second debounce plus a save at shutdown, never in screenshot
mode), when it is restored (`restore_session` on, no start path, not a
screenshot), what the core's session ABI covers (the whole line set, with the
optional per-tab view field left out so nothing is written lossily), what closes
a panel (closing its last tab, as in the legacy; no new command), and what the
tab state keeps (the displayed paths, the cursor and the anchor).

The sixth was the user's to decide and is recorded above: how much of the
legacy's settings machinery moves into the core.

## The legacy behaviour being ported

Read from `src/app.c` and `src/settings.c`; these are the facts the Qt side has
to reproduce.

**The tree.** `struct Node` (`src/app.c:421`) is binary. A node is either
`NODE_PANEL` (a leaf holding a `Panel`), `NODE_HSPLIT` (children `a` and `b`
side by side) or `NODE_VSPLIT` (`a` over `b`). Each split carries a ratio
clamped to 0.1 - 0.9. `MAX_PANELS` is 16 (`src/app.c:431`), and the legacy shows
a "Too many panels" toast beyond that.

**The panel.** `struct Panel` (`src/app.c:411`) holds the visible tab (`cur`)
and the rectangles of its area, its tab bar, its "new tab" button and its split
button. Each panel draws its own tab bar; `Tab.panel` names the panel whose bar
shows that tab (`src/app.c:358`).

**The tab.** `struct Tab` (`src/app.c:353`) holds `Pane pane[2]`, of which only
`pane[0]` is used, plus the terminal dock state that S8 will need. A `Pane`
(`src/app.c:270`) is one directory view: its path, listing, display order,
per-entry selection flags, cursor and anchor, sort key and direction, and its
own history.

**Splitting.** `panel_split(pn, zone, move, path)` (`src/app.c:5643`) turns the
leaf holding `pn` into a split node with ratio 0.5 and puts the new panel on
the requested side (`ZONE_LEFT`/`ZONE_RIGHT` give a horizontal split,
`ZONE_TOP`/`ZONE_BOTTOM` a vertical one). It either moves an existing tab into
the new panel (`move`) or opens `path` there as a new tab. The commands are
"Split panel right" (`Ctrl+\`) and "Split panel down" (`Ctrl+Shift+\`)
(`src/app.c:13772`).

**Opening and moving.** "Open in split right/down" is a context-menu action on a
folder: it splits the active panel and opens that folder in the new panel
(`src/app.c:8303`). "Split tab right/down" is a context-menu action on a tab: it
splits the panel and **moves** that tab into the new one, and only when the
source panel keeps at least one tab (`src/app.c:8397`).

**Dragging.** Folders dropped on a panel edge open in a new split there, and
folders dropped in the panel's centre open as new tabs in that panel
(`src/app.c:9995`). The edge zone is the nearest of the four sides, measured
within a band of `min(44 px, 15 %)` of the panel (`src/app.c:9976`). A tab
dropped on another panel moves to it.

**Cycling.** `panel_cycle` walks the panels in reading order - left to right,
top to bottom (`src/app.c:5670`) - and activates the next or previous one.

**The session.** `settings.conf` carries three kinds of session line
(`src/settings.c:117`):

- `layout=P0,H0.500(P1,P2)` - the tree as text. `P<id>` is a panel leaf,
  `H<ratio>(a,b)` and `V<ratio>(a,b)` are splits (`src/settings.c:610`).
- `ptab=<panel>\t<active>\t<path>[\t<view options>]` - one line per tab, naming
  the panel it belongs to, whether it is that panel's current tab, and its path.
  The optional fourth field carries the tab's view mode and icon sizes.
- `active_panel=<n>` - the panel that was active.

On restore (`src/settings.c:663`) the tree is parsed, a missing or corrupt
layout falls back to a single panel, tabs whose folder no longer exists are
dropped, and the panel named by `active_panel` is activated if it survived.

## Goals

- A window whose panels are arranged in a split tree, each panel with its own
  tab strip and its own stack of tabs.
- Split a panel right and down, by command and by drag; open a folder in a
  split; move a tab into a split; close a panel.
- Per-tab state - path, history, cursor and selection - survives a tab switch
  within a panel, the way the legacy keeps it in its `Pane`.
- (S2b) the panel tree, the per-panel tab stacks and the active panel are saved
  to and restored from the shared `settings.conf`, in the legacy's format.

## Non-goals

- Terminal tabs and the terminal dock under a folder (S8).
- The legacy's tree view inside a pane (the legacy has one; the Qt directory
  view does not, and adding it is not part of this slice).
- Per-panel view options beyond the per-tab view mode the legacy already
  persists. The shell-level options built in S1 - view mode, columns, zoom,
  hidden entries, folder sizes, sort key - stay shell-level in S2a; the legacy
  keeps the view mode per tab, and S2b restores that per-tab view mode from the
  `ptab` line's fourth field.
- Making the Kirigami application the default UI (S12).

## Anti-goals

- Porting the legacy renderer, widget kit or input handling. The Qt side draws
  its own widgets; only the behaviour is shared.
- Re-implementing engine logic in C++ or QML. Splitting panes is a shell
  concern; the engine keeps doing the listing, the sorting and the sizes.
- Changing the C engine's algorithms, or the legacy application's behaviour.

## Constraints

- `main` is PR-only. Each slice is a branch with atomic commits and a pull
  request; all seven required CI checks must be green before the next branch
  starts.
- The C engine sources stay pure ASCII and warning-clean.
- The legacy SDL application keeps building and keeps passing its `--selftest*`
  suites. S2b is the first slice to touch the legacy application's sources, and
  only to share the session format and the settings file handling with the
  core; its behaviour must not change.
- The GUI thread never blocks on engine work, and model mutation happens only on
  the GUI thread.
- Everything renders through Qt Quick's hardware scene graph: no `Canvas`, no
  `QPainter`, no `QGraphicsView`, and no forced software backend.
- Qt 6.11 is what the build container carries. `QtQuick.Controls` is already
  linked, so `SplitView` is available without a new dependency; no CI package
  addition is expected.
- KDE Plasma 6.6 - 6.8 is the target. Verified means builds and headless tests;
  live Plasma behaviour is target-only.

## Approaches

### The shape of the tree: where does it live?

**Considered and rejected: keep the tree in QML.** The `SplitView` would own the
layout and the controller would only track which pane is active. Rejected: the
pane tree is application state, not a view detail. Navigation, history, the
selection, the session format and the tests all need it, and a tree that lives
in QML cannot be serialized for the session without a second representation.

**Chosen: the tree lives in C++.** `ShellController` owns it and QML renders it.
This mirrors the legacy, where the tree is the application's state and the
renderer only draws it, and it makes the session format a straight
serialization of the controller's own data.

### One model per tab, or one per panel?

**Considered and rejected: one `DirectoryModel` per tab.** The most faithful
mapping of the legacy's per-tab `Pane`. Rejected: every model owns a worker
thread and a pending-job queue, and the legacy allows 16 panels with several
tabs each, so this would mean dozens of idle threads.

**Chosen: one `DirectoryModel` per panel.** A panel shows its current tab, so a
panel needs one model. Switching a panel's current tab re-points the model at
the new tab's path, which is the same thing the model already does for
navigation. The per-tab state that must survive the switch - path, history,
cursor, selection - lives in a C++ tab object instead of in the model, so it is
kept exactly as the legacy keeps it.

### Where does the selection live?

The selection is currently a QML property on the directory page. With tabs it
has to move into the C++ tab state, because a selection belongs to a tab and
must come back when that tab is shown again. This is a visible change to
`DirectoryPage.qml`, which today owns `selectedPaths` and `currentIndex`.

What the tab state keeps is the selection as the **displayed paths** the QML
already carries, plus the cursor index and the anchor index. The raw bytes stay
the model's business, exactly as S1 settled it: the operation boundary resolves
a displayed path back to its bytes and refuses the operation when the mapping
is ambiguous. A rescan that removes entries drops the entries whose paths are
gone from the selection, rather than carrying a selection that points at
nothing.

### What closes a panel

The legacy has no explicit close-panel command. A panel closes when its last
tab closes (`src/app.c:5597`), and the removed leaf's sibling inherits the
split node's space. S2a reproduces that and adds no new command, which is how
the master design's exit criterion "a pane can be closed" is met in the legacy.

### The session format

**Considered and rejected: the Qt side writes the format itself.** The format is
simple text, so a second implementation is possible. Rejected: the legacy
application must be able to read what the Qt application writes, and a format
implemented twice is a format that will drift.

**Chosen: the core owns the format and the file.** The tree becomes a neutral,
flat node array in the C ABI, and the core serializes and parses it. The legacy
application converts its `Node` tree to and from that form and calls the same
functions, so there is one implementation of the format. The settings file
itself becomes core-owned: the core reads it, merges it per key group and
writes it back, and both applications are clients of that. The legacy's
three-way merge - a key changed in memory since the last sync wins, otherwise
a key changed on disk by another process wins, with the session lines merged
as one group (`src/settings.c:389`) - moves into the core with the file
handling, so the legacy application keeps its multi-process safety and the Qt
application gets it too.

The tree as a flat array is the ABI shape: an array of nodes, each naming its
kind, its ratio, its panel id for a leaf, and its two children by index. It is
easy to convert to and from the legacy's `Node` tree, easy to hand to QML, and
it needs no recursive allocation in C.

## The parts of the how

### The shell

- `ShellController` owns the panel tree. A panel holds an ordered list of tabs
  and the index of its current tab; a tab holds its path, its history, its
  cursor and its selection. The controller keeps the panel that is active.
- Each panel owns a `DirectoryModel`, showing its current tab's directory.
- The controller's navigation, history and clipboard operations act on the
  active panel's current tab, exactly as the legacy's act on the active pane.
- The directory watcher follows every visible panel's directory, not only the
  active one's, so a panel that is not focused still refreshes.
- Closing a panel removes it from the tree. The removed leaf's sibling takes
  the whole split node's place and inherits its space, and the first panel in
  reading order becomes active if the removed one was active
  (`src/app.c:5449`). Closing a panel's last tab closes the panel
  (`src/app.c:5597`).
- The legacy's limit of 16 panels applies. Beyond it the shell refuses the
  split, as the legacy does.

### The QML

- The window's content becomes a tree of panel views. Each panel view draws its
  own tab strip and the directory content for its current tab.
- The tree is drawn with `SplitView` from `QtQuick.Controls`. A horizontal split
  is a `SplitView` with its children side by side, a vertical split has them
  stacked, and the split's ratio is the legacy's ratio.
- The directory content becomes reusable: the three views, the size and date
  formatting, the selection and the keyboard handling move into a component
  that takes a panel as a property, so every panel renders the same thing from
  its own model and its own tab state.
- The toolbar that S1 built stays where the user expects it, at the top of the
  window, and acts on the active panel. It is not repeated per panel.
- Every new widget carries an `objectName`, as the S1 widgets do, so the tests
  can drive it.

### The session (S2b)

- The core owns the whole settings file, not just the session lines. It reads
  `settings.conf`, merges it per key group and writes it back, so the legacy
  application's multi-process safety is preserved and the Qt application gets
  the same guarantee. The legacy application calls the core instead of doing
  the file handling itself; its behaviour does not change.
- The core owns the layout format: parse a `layout=` string into a flat node
  array and serialize a node array back into that string, with the legacy's
  exact spelling. It also owns the whole session line set - `layout=`, the
  `ptab=` lines and `active_panel=` - so the format has one implementation.
- The Qt shell writes `layout=`, one `ptab=` line per tab, and `active_panel=`
  when the session is saved, and restores the tree from them when it starts. A
  missing or corrupt layout falls back to a single panel, and tabs whose folder
  no longer exists are dropped, as the legacy does.
- The `ptab=` line the Qt side writes carries three fields - the panel id, the
  current flag and the path - and leaves out the legacy's optional fourth
  field, which holds per-tab view options the Qt side does not have. The
  legacy's parser tolerates its absence (`src/settings.c:687` guards it with
  `n == 4`), so a session written by the Qt application never shrinks the
  legacy application's rows.
- **When the session is written.** The shell marks the settings dirty when the
  panel tree, a tab stack or the active panel changes, and saves on a three
  second debounce after the last change, then once more at shutdown. That is
  the legacy's cadence (`src/app.c:16621` and `:16667`). It never saves in
  `--screenshot` mode, as the legacy does not.
- **When the session is restored.** At startup, and only when `restore_session`
  is on, the application was not started with a path to open, and the
  application is not taking a screenshot. That is the legacy's gate
  (`A.restore_session && !A.screenshot && session_restore()`, `src/app.c:16473`).
  The Qt shell honours `restore_session=0`, never restores in `--screenshot`
  mode, and treats a path given on the command line as "no restore".

### Dragging (S2b)

- Dragging a folder over a panel shows the edge zones, the same four sides
  within the same `min(44 px, 15 %)` band, and drops it into a new split at
  that edge, or into the panel as a new tab when dropped in the centre.
- Dragging a tab onto another panel moves the tab there, and dragging a tab to
  an edge of a panel splits that panel and moves the tab into the new one.

## Testing strategy

- The panel tree, the split, the close, the tab switch and the session
  round-trip get cases in `tools/qtui-selftest.cpp`, driving the real QML the
  way PR 3 established: show the window first, then act on it.
- The layout format gets cases in `tools/core-abi-selftest.c`, including the
  legacy's own spelling of a known tree, a corrupt string, and a round trip.
- The legacy `--selftest*` suites stay green throughout; S2b adds a case there
  for the shared format, because the legacy application is the other consumer
  of it.
- The C ABI cases run in CI as a non-root user.

## What's left to the builder

- The exact C++ type names and file layout in `qtui/` for the panel tree, the
  tab state and the per-panel models.
- The exact ABI names for the layout node array, the parse and the serialize
  functions, and the settings-file accessors.
- The QML component split: how much of `DirectoryPage.qml` becomes a reusable
  per-panel component and how it is instantiated from the tree.
- The model role names and the property names the QML binds to.
- The test names per behaviour.
- Whether the per-tab view mode is restored in S2b or documented as a
  limitation.

## Provenance of decisions

The user chose: the legacy's behaviour, with each panel holding its own tabs,
and the slice delivered as two pull requests. The tree living in C++, one model
per panel, the selection moving into the tab state, the core owning the
session format and the settings file, and the `SplitView` rendering are the
author's calls, consistent with the master design's rules that the C ABI stays
the only channel, that the model/controller split stays as it is, and that
configuration has a single source of truth.
