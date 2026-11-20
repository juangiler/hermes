# Kirigami split panes (slice S2 of the Qt/Kirigami port)

## Intent and why

The Kirigami shell shipped so far has one global tab strip and one view, so it
cannot show a split window at all, and it has nothing to restore. This slice
brings split panes to it: **the window keeps its one tab strip, and every tab
holds its own tree of panes.** Switching tabs switches the whole arrangement,
so a tab that was never split shows a single pane.

The legacy Hermes window arranges things the other way round: a tree of panels,
each with its own tab strip and one directory per tab. The legacy remains the
authority for the *behaviour* to port - the split, the close, the 16-pane limit,
the focused directory's state - but not for the shape. The legacy SDL3
application is scaffolding and is deleted at the end of the port, so nothing
here is built to preserve its arrangement or its session format.

## Decisions taken with the user (2026-11-20)

- **One tab strip for the window, a tree of panes per tab.** The user first
  chose to mirror the legacy's tree of panels with a strip per panel, then
  reversed that decision after seeing how the shell would have to carry several
  strips. The current decision is the one in the goals below; the trade-off is
  recorded in the approaches section.
- **Split the slice in two pull requests.** S2a delivers the structure; S2b
  delivers session persistence and drag-and-drop. The reason is in the master
  design's own rejection of one large PR: the review rounds in this project find
  real defects only when each round stays small enough to examine, and the
  persistence half is the first slice to touch the legacy SDL application's
  settings code, so it must not be able to block the structure work.
- **The core owns the whole shared settings file.** The legacy's per-key-group
  three-way merge moves into the core with the file handling, so both
  applications share one implementation and the legacy keeps its multi-process
  safety. The alternative - each application keeping its own writer - was
  rejected because it leaves the file format handled in two places and lets one
  application's save undo the other's settings. The *session* is not part of
  that shared configuration; it belongs to the Qt application alone.

## What the builder check settled

A builder reading this design asked six questions. Five are answered by the
legacy's own behaviour and are marked in the sections above: when the session is
written (a three second debounce plus a save at shutdown, never in screenshot
mode), when it is restored (`restore_session` on, no start path, not a
screenshot), what the core's session ABI covers (the whole line set, with the
optional per-tab view field left out so nothing is written lossily), what closes
a pane (removing the leaf, with its sibling inheriting the space, as the legacy
does when a panel loses its last tab), and what the pane state keeps (the
displayed paths, the cursor and the anchor).

The sixth was the user's to decide and is recorded above: how much of the
legacy's settings machinery moves into the core. A seventh question came later,
from the user: the shape of the whole thing, which is recorded in the decisions
and in the approaches.

## The legacy behaviour being ported

Read from `src/app.c` and `src/settings.c`; these are the facts the Qt side has
to reproduce.

**How the legacy's shape maps onto the new one.** The legacy's *panel* is the
closest thing to the new model's *tab*: a container with a strip and a
directory. The legacy's *tab* is the closest thing to the new model's *pane*:
one directory, with the path, the listing, the selection, the cursor and the
history. The tree itself is the same: a binary tree of leaves and splits with
a ratio. What differs is the level the tree sits at - in the legacy the tree
of panels is the window's, and in the new model the tree of panes is a tab's -
and that is why the legacy's arrangement of strips cannot be restored into a
single strip.

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

- One tab strip for the window, and every tab holds its own tree of panes.
- Every pane shows one directory. The focused pane - the one the user last
  acted in - receives navigation, selection and commands.
- A pane splits right and down, a folder opens in a split, a pane closes, and
  the tab's arrangement is remembered while the tab is not the active one.
- A tab can be renamed, and a tab that holds several panes is marked with a
  `MULTI` badge next to its name.
- Per-pane state - path, history, cursor and selection - belongs to the pane
  and survives the tab being switched away and back.
- (S2b) the arrangement, the tabs and their names are saved to and restored
  from the Qt application's own session file; the shared `settings.conf` keeps
  its configuration role and is not used for the session.

## Non-goals

- Terminal tabs and the terminal dock under a folder (S8).
- The legacy's tree view inside a pane (the legacy has one; the Qt directory
  view does not, and adding it is not part of this slice).
- Per-pane view options beyond what S1 built. The shell-level options - view
  mode, columns, zoom, hidden entries, folder sizes, sort key - stay
  shell-level.
- Making the Kirigami application the default UI (S12).

## The SDL application is scaffolding

The legacy SDL3 application is temporary. The port's last step deletes it, and
the SDL sources are not to be kept. Nothing in this slice is designed to keep
it alive, to extend it, or to preserve a format for its sake: where the two
applications differ, the new shape wins and the legacy adapts.

Two things follow from that, and they are the reason the shared settings and
the session are treated differently here. The **shared settings** - the sort
defaults, the window size, the servers, the devices, the keybindings - stay in
the legacy's format and file, because they are one configuration that both
applications read and write while the legacy still exists, and because the
legacy must keep working. The **session** is the Qt application's own, in its
own file and its own format, because the legacy's session format cannot
express a list of tabs each holding a tree of panes, and because a mapping
that only the legacy could read has no value once the legacy is gone.

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
  linked, so no CI package addition is expected.
- QML cannot render a tree of splits by instantiating a type recursively: the
  compiler rejects it, and the URL-loading workaround hangs. The layout is
  therefore computed in C++ and the QML places the panels.
- KDE Plasma 6.6 - 6.8 is the target. Verified means builds and headless tests;
  live Plasma behaviour is target-only.

## Approaches

### The shape of the whole thing: a tree of panels, or a tree of panes in each tab?

This is the decision the user reversed, so both are recorded with what each
costs.

**The legacy's shape, chosen first: a tree of panels in the window, each panel
owning its own tab stack and drawing its own tab strip.** It ports the legacy
literally, and it restores the legacy's session exactly. Its cost is visible
in the shell: the window would carry several tab strips, one per panel, which
is an arrangement most people do not expect from a file manager, and it
replaces the tab strip the shell already has.

**The shape chosen instead, on 2026-11-20: one tab strip for the window, and
every tab holds its own tree of panes.** The window keeps the strip it already
has; a tab remembers the arrangement it was left in; a pane is a directory in
that arrangement. Its cost is the legacy's per-panel tab stacks, which have no
home in a single strip, and therefore a partial session restore.

### The shape of the tree: where does it live?

**Considered and rejected: keep the tree in QML.** The `SplitView` would own the
layout and the controller would only track which pane is active. Rejected: the
pane tree is application state, not a view detail. Navigation, history, the
selection, the session format and the tests all need it, and a tree that lives
in QML cannot be serialized for the session without a second representation.

**Chosen: the tree lives in C++.** Each tab holds one, `ShellController` owns
the tabs, and QML renders them. This mirrors the legacy, where the tree is the
application's state and the renderer only draws it, and it makes the session
format a straight serialization of the controller's own data.

### One model per pane, or one per tab?

**Considered and rejected: one `DirectoryModel` per tab.** A tab holds several
panes, and a tab that is not active still holds its panes' state. Rejected:
every model owns a worker thread and a pending-job queue, so a handful of tabs
with several panes each would mean dozens of idle threads.

**Chosen: one `DirectoryModel` per pane.** A pane is a directory view, so it
needs a model. The state that must survive a tab switch - path, history,
cursor, selection - lives in the pane rather than in the model, so it is kept
exactly as the legacy keeps it in its `Pane`.

### Where does the selection live?

The selection is currently a QML property on the directory page. With panes it
has to move into the C++ pane state, because a selection belongs to a pane and
must come back when that pane is shown again. This is a visible change to
`DirectoryPage.qml`, which today owns `selectedPaths` and `currentIndex`.

What the pane state keeps is the selection as the **displayed paths** the QML
already carries, plus the cursor index and the anchor index. The raw bytes stay
the model's business, exactly as S1 settled it: the operation boundary resolves
a displayed path back to its bytes and refuses the operation when the mapping
is ambiguous. A rescan that removes entries drops the entries whose paths are
gone from the selection, rather than carrying a selection that points at
nothing.

### What closes a pane

In the legacy, a panel dies when its last tab closes (`src/app.c:5597`), and
the removed leaf's sibling inherits the split node's space. In the new shape a
pane is a leaf of a tab's tree, so closing the focused pane removes that leaf
and the sibling inherits the space. The tab strip's close button closes the
whole tab, with its tree - the shell already has that button, and the legacy's
per-panel stacks are not reproduced.

### How a tab is named

A tab holds several directories, so its label is the **focused pane's display
name**: the directory's base name, or `/` for the root, or `Trash` for the
trash directory, and with the trailing `::` stripped from a remote path - the
legacy's own rule in `disp_name()` (`src/app.c:1144`). The focus is per tab:
each tab remembers which of its panes was last acted in, and the label follows
that pane as it navigates, the way the legacy's tab label follows its
directory.

**A tab can also be renamed by the user.** The name the user gives wins over
the derived one, and clearing it brings the derived name back. The rename
matters most for a tab that holds several panes, where the derived name names
only one of them - but the affordance is the same for every tab, because one
path is simpler than two and the derived name is still available by clearing
the custom one.

**A split tab is marked.** Whenever a tab holds more than one pane, a small
badge reading `MULTI` sits next to its name in the tab strip, so a tab that was
split is recognisable at a glance even when it is not the active one. The
badge appears as soon as the pane is split and disappears when the tab is back
to a single pane.

The tab's name is the user's, so it is the user's to keep: it is saved with
the session, in S2b, where the session format gains a line for it.


### The session format

**Considered and rejected: reuse the legacy's session lines.** The legacy's
`layout=`, `ptab=` and `active_panel=` lines can describe a tree of panels with
tab stacks. Rejected: they cannot describe a list of tabs each holding a tree
of panes, so the mapping would be lossy by construction - and once the legacy
application is deleted, a format that only it could read has no value.

**Chosen: the Qt application owns its session, the core owns the shared
settings file.** The Qt session is its own file and its own format: the tabs,
their names, their focused panes and their pane trees, written whole and
atomically. The shared `settings.conf` keeps its configuration role - the sort
defaults, the window size, the servers, the devices, the keybindings - and the
core owns that file, merging it per key group so neither application undoes the
other's settings. The legacy's three-way merge (`src/settings.c:389`) moves
into the core with that file handling, so the legacy keeps its multi-process
safety while the Qt session stays independent of it.

## The parts of the how

### The shell

- `ShellController` owns an ordered list of tabs, and the index of the active
  one. Each tab holds a tree of panes and remembers which of its panes was last
  acted in - the focused pane. A pane holds its path, its history, its cursor
  and its selection.
- Each pane owns a `DirectoryModel` and a `DirectoryWatcher`, showing that
  pane's directory.
- The controller's navigation, history, clipboard and file operations act on
  the active tab's focused pane, exactly as the legacy's act on the active
  pane.
- The directory watcher follows every pane's directory, not only the focused
  one's, so a pane that is not focused still refreshes.
- Closing a pane removes it from the tab's tree. The removed leaf's sibling
  takes the whole split node's place and inherits its space, and the first pane
  in reading order becomes focused if the removed one was focused
  (`src/app.c:5449`).
- The legacy's limit of 16 panes applies. Beyond it the shell refuses the
  split, as the legacy does.

### The QML

- The window keeps the tab strip it already has. The active tab's page holds
  the tree of panes; the other tabs' pages hold their own trees.
- **The controller lays the panes out, and the QML places them.** The tree is
  walked in C++ and each pane gets a rectangle, the way the legacy computes
  its panel rectangles in `layout()` (`src/app.c:1603`). The QML binds one view
  per pane and positions it. This replaces an earlier plan to render the tree
  with nested `SplitView`s and a recursive QML component: a spike showed that
  QML refuses a type that instantiates itself ("Node is instantiated
  recursively"), and the workaround - loading the type by URL instead of by
  name - loops forever and hangs the application. Neither is acceptable in a
  file manager's window, and the legacy's own arrangement is computed in C
  anyway.
- The legacy never lets the user drag a divider: the split ratio is only ever
  set to 0.5 (`src/app.c:5398` and `:5656`), and the format stores the ratio so
  that a saved one is honoured when the session is restored. The Qt side lays
  the panes out the same way and persists the ratio it read, so the two
  applications agree about the saved layout.
- The directory content becomes reusable: the three views, the size and date
  formatting, the selection and the keyboard handling move into a component
  that takes a pane as a property, so every pane renders the same thing from
  its own model and its own state.
- The toolbar that S1 built stays where the user expects it, at the top of the
  window, and acts on the active tab's focused pane. It is not repeated.
- The tab strip's label for a tab is its focused pane's display name, or the
  name the user gave it, and a badge reading `MULTI` sits next to the name of
  every tab that holds more than one pane.
- **The split animates.** A pane that splits and a pane that closes get a
  motion treatment, as the master design's motion requirements ask for. The
  animation is drawn on top of content that is already there and never delays
  it: the new pane's rows appear as soon as they are ready.
- Every new widget carries an `objectName`, as the S1 widgets do, so the tests
  can drive it.

### The session (S2b)

- The core owns the whole settings file, not just the session lines. It reads
  `settings.conf`, merges it per key group and writes it back, so the legacy
  application's multi-process safety is preserved and the Qt application gets
  the same guarantee. The legacy application calls the core instead of doing
  the file handling itself; its behaviour does not change. This also means the
  file handling does not have to move again when the SDL application is
  deleted.
- **The session itself is the Qt application's own.** The legacy's session
  format describes a window of panels, each with its own stack of tabs, and
  the new shape is a list of tabs, each with a tree of panes - the legacy's
  format cannot express it, and a lossy mapping buys nothing once the legacy
  application is gone. So the Qt application writes its own session: its tabs,
  their names, their focused panes and their pane trees, in its own file,
  written whole and atomically. The legacy's `layout=`, `ptab=` and
  `active_panel=` lines stay the legacy application's business; the Qt
  application neither writes nor reads them.
- **The shared settings still use the legacy's format and file.** The sort
  defaults, the window size, the servers, the devices and the keybindings are
  one configuration that both applications read and write during the
  transition, and they stay exactly as they are. Only the session - which is
  application state rather than configuration - becomes the Qt
  application's own.
- **The session is a snapshot of the current state, never an accumulation.**
  What is written is the tabs that exist now, their names and their panes. A
  tab that was closed is simply not in it - closing a tab takes it out of the
  session exactly as it takes it out of the window - and the next save writes
  what is there, not what was once there.
- **The write-back keeps what the writer is not writing.** The core owns the
  settings file, so a writer supplies the groups it owns and the core
  preserves the rest: the two applications do not undo each other's settings
  while both exist.
- **When the session is written.** The shell marks the settings dirty when a
  tab's tree, a tab or the active tab changes, and saves on a three second
  debounce after the last change, then once more at shutdown. That is the
  legacy's cadence (`src/app.c:16621` and `:16667`). It never saves in
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

The user chose: the legacy's split/close/cycling behaviour, one tab strip with
a pane tree per tab, tab names and the `MULTI` badge, and the slice delivered as
two pull requests. The tree living in C++, one model per pane, the selection
moving into the pane state, the C++-computed layout, the Qt-owned session file
and the core-owned shared settings file are the author's calls, consistent with
the master design's rules that the C ABI stays the only channel, that the
model/controller split stays as it is, and that configuration has a single
source of truth.
