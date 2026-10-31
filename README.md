# Looking Glass

Custom, dynamic multiviews for OBS Studio.

OBS ships with a single fixed multiview layout. Looking Glass lets you build as many
multiview windows as you like, each with its own grid, its own mix of content, and its
own per-cell labels — then open them on any monitor, in a window or fullscreen. Layouts
are saved per scene collection and restored automatically, and every window can be
driven remotely over obs-websocket.

## Features

* **Any number of multiviews.** Create, duplicate, rename, and delete as many layouts as you need.
* **Freeform grids.** 1–16 rows by 1–16 columns, with cells merged into arbitrary rectangular spans.
* **Mixed content per cell.** Program, studio-mode preview, any scene, any video source, a named canvas, or a placeholder.
* **Fully styled labels.** Per-cell text, font, size, background color and opacity, and nine-way alignment — rendered inside the OBS graphics pipeline and scaled with the cell like OBS's own multiview labels.
* **Broadcast safe areas.** Optional EBU R 95 overlays (action safe, title safe, 4:3 inner safe, and a center crosshair) per cell.
* **On-air tally.** Optional status border on scene cells: red for Program, green for Preview in studio mode.
* **Reusable templates.** Save any layout as a template shared across scene collections, with or without its exact source and scene names.
* **Per-monitor window control.** Windowed, fullscreen on a specific monitor, or centered on the main display.
* **State that sticks.** Geometry, monitor, fullscreen state, and which windows were open persist across scene collection switches and OBS restarts.
* **obs-websocket control.** Open, close, and send multiviews to the main display from outside OBS.

## Requirements

OBS Studio 31.1 or later. (Looking Glass uses the canvas API introduced in that release.)

On Linux, multiview windows work under X11 and XWayland. Running OBS as a native Wayland
client needs Qt 6.9 or later; with older Qt, start OBS with `QT_QPA_PLATFORM=xcb`.

## Getting Started

Everything lives under **Tools ▸ Looking Glass**.

1. Choose **Create New Multiview…**
2. Give it a name. The layout starts from the built-in default template: a 2x2 Preview,
   a 2x2 Program, and the bottom two rows auto-filled with the scenes in your current
   collection. **Start from template** swaps in any other template.
3. Fill the cells: drag scenes and sources from the content list onto the canvas, or
   select cells and click an item. Fine-tune the selection in the inspector.
4. Click **Create and Open**. The new multiview opens immediately.

### The Layout Editor

The editor is one resizable window with three panes:

* **Content list** (left) — every scene, source and canvas, plus Preview and Program,
  with a filter box. Scenes currently on air are marked.
* **Canvas** (center) — the layout drawn the way the multiview window will look, with the
  real grid lines and labels. A status line underneath describes the selection.
* **Inspector** (right) — edits whatever is selected: content, label, tally border and
  safe areas. With nothing selected it shows the grid line width and color. With several
  cells selected it changes them all at once.

The toolbar above holds the **Rows** and **Columns** boxes, **Merge**, **Split**,
**Clear**, and **Undo** / **Redo**.

| Action | How |
|---|---|
| Select a cell | Click it |
| Select a range | Click and drag across the canvas |
| Add to / toggle a selection | Ctrl+click, or Ctrl+drag |
| Extend a selection | Shift+click |
| Assign content | Drag an item from the list onto a cell, or select cells and click the item |
| Edit a cell | Select it and use the inspector |
| Combine cells | Select a full rectangle, then **Merge** or press `M` |
| Break merged cells apart | Select them, then **Split** (the content stays on the top-left cell) |
| Empty cells | Select them, then **Clear** or press `Delete` |
| Undo / redo | `Ctrl+Z`, and `Ctrl+Shift+Z` or `Ctrl+Y` |
| Deselect | `Esc` |

Merging requires the selection to form a complete rectangle, and any already-merged cell
it touches must be fully inside the selection — partial overlaps are rejected, and the
status line says why. **Clear** also breaks merged cells back into empty 1x1 cells.

Every change is undoable, including template changes and grid resizes. **Rows** and
**Columns** resize the grid in place. Cells that still fit are kept (with their spans
clamped to the new bounds); anything outside the new bounds is dropped and replaced with
empty cells, and a notice with an **Undo** button tells you when that removed cells with
content. The grid line width and color are set in the inspector when nothing is selected.

Closing the window with unsaved changes asks before discarding them.

### Widget Types

Each cell is assigned one widget type, either by dragging content onto it or from the inspector:

| Type | Shows |
|---|---|
| `None` | Nothing — an empty cell |
| `Preview` | The studio-mode preview scene, falling back to the program output when studio mode is off |
| `Program` | The main program output |
| `Canvas` | The main canvas, or a specific named canvas |
| `Scene` | A specific scene |
| `Source` | A specific video source |
| `Placeholder` | The Looking Glass icon, as a visual spacer |

Content is scaled to fit its cell with the aspect ratio preserved and centered, so mixed
cell shapes never distort the picture.

`Scene` and `Source` cells follow their scene or source when it is renamed in OBS, and
pick it up again if it is deleted and later recreated under the same name.

Two per-cell toggles sit alongside the type:

* **Draw Safe Areas** overlays broadcast safe regions on the cell content — action safe, title safe, 4:3 inner safe, and a center crosshair.
* **Show Status Border** draws a colored border reflecting live status: red when the scene is on air in Program, green when it is in Preview in studio mode, nothing otherwise. Offered on `Scene` cells.

### Labels

Every cell has its own label settings:

* **Show Label** toggles it entirely.
* **Custom Text** overrides the label; leave it empty to use the source or scene name automatically (it updates when the scene or source is renamed).
* **Font** picks the family and style.
* **Size** sets how big the label is relative to its cell, from 25% to 400%. Labels scale with their cell the way OBS Studio's own multiview labels do: at 100% the text is about a tenth of the cell's height, whatever the window size. A label too long for its cell shrinks to fit the cell's width.
* **Background** sets the label backing color, including opacity — the picker shows the resulting hex value and opacity percentage.
* **Vertical** and **Horizontal** place the label in any of nine positions within the cell.

## Managing Multiviews

**Tools ▸ Looking Glass ▸ Manage Multiviews…** lists every multiview in the current scene
collection:

* **Show Multiview** — open it, or focus it if it is already open
* **Edit Multiview** — reopen the grid editor; open windows reload live
* **Rename** / **Delete** / **Duplicate**
* **Create Template** — save this layout as a reusable template

## Templates

Templates are layouts without window state, shared across *all* scene collections.

Creating one from **Manage Multiviews ▸ Create Template** offers a **Preserve exact source
and scene names** checkbox:

* **Checked** — the template keeps the exact widget types and source/scene names from the multiview. Best for reproducing a layout inside one collection.
* **Unchecked** — content cells become placeholders labeled with what they showed; fill them in after applying the template. Best for a structural layout you will reuse against different scenes.

Apply a template from the **Template** dropdown in the multiview editor. Applying replaces
the current grid, so you are asked to confirm first.

**Tools ▸ Looking Glass ▸ Manage Templates…** renames, edits, and deletes your templates.
**Edit** opens the same editor used for multiviews (content library, canvas, inspector,
undo), without the grid line style, which templates do not store. The name is checked as
you type: it must not be empty or match another template (or the built-in one), and
**Save Template** stays disabled until it is valid, so a clash never discards your edits.
Preserve exact source and scene names is chosen when the template is created, not in the editor. Closing with unsaved changes (including a rename) asks before
discarding them.
The built-in **Default (OBS-style)** template is always present and cannot be modified —
it is generated each time it is used, so its auto-filled scene cells always match the
current collection.

## Window Controls

Each multiview gets its own submenu under **Tools ▸ Looking Glass**:

| Entry | Effect |
|---|---|
| **Open** | Opens the window, or focuses it if already open |
| **Close** | Closes the window (enabled only while it is open) |
| **Edit…** | Opens the grid editor for this layout |
| **Send to Main Display** | Opens if needed, then centers it as a 1280x720 window on the primary display |
| **Fullscreen on \<monitor\>** | Opens if needed, then goes fullscreen on that monitor |
| **Windowed** | Leaves fullscreen and restores the last windowed geometry |

Right-clicking inside a multiview window offers the same per-monitor fullscreen options,
plus **Edit Multiview…** and **Close Multiview**. **Windowed** appears there only while the
window is fullscreen.

Window state is saved as you move and resize. When you switch scene collections or restart
OBS, the multiviews that were open reopen where you left them, on the monitor they were on,
fullscreen if they were fullscreen. Monitors are remembered by name, so unplugging or
reordering displays does not send a window to the wrong one; a window whose monitor is gone
reopens centered on the primary display.

## Controlling Multiviews from obs-websocket

Looking Glass registers an obs-websocket vendor named `looking-glass`, so multiview
windows can be opened, closed, and sent to the main display from outside OBS Studio
(Stream Deck, Companion, custom tooling) — the same actions offered by the
`Tools ▸ Looking Glass ▸ <multiview name>` submenus.

| Request type | Description |
|---|---|
| `OpenMultiview` | Opens the multiview window, or focuses it if it is already open. |
| `CloseMultiview` | Closes the multiview window. Succeeds as a no-op when it is not open. |
| `SendMultiviewToMainDisplay` | Opens the multiview if needed, then centers it as a 1280x720 window on the primary display. |

Call them with obs-websocket's `CallVendorRequest` request. Every request takes a single
field, `multiviewName`, naming a multiview in the current scene collection:

```json
{
  "requestType": "CallVendorRequest",
  "requestData": {
    "vendorName": "looking-glass",
    "requestType": "OpenMultiview",
    "requestData": {
      "multiviewName": "Studio Wall"
    }
  }
}
```

The vendor response data carries a `success` boolean, plus an `error` string describing
the problem when `success` is `false`:

```json
{
  "success": false,
  "error": "No multiview named 'Studio Wall' exists in the current scene collection."
}
```

Requests are registered once obs-websocket has loaded. If obs-websocket is not available,
Looking Glass logs a note and everything else works as normal.

## Where Settings Are Stored

Multiview layouts, and which windows are open and where, are saved **inside the scene
collection** itself. They follow the collection when it is renamed, duplicated, exported,
or imported, and OBS's own scene collection backups cover them.

Templates are global, shared by every scene collection, and live in `templates.json` in the
plugin's config directory:

| Platform | Path |
|---|---|
| Windows | `%APPDATA%\obs-studio\plugin_config\looking-glass\` |
| macOS | `~/Library/Application Support/obs-studio/plugin_config/looking-glass/` |
| Linux | `~/.config/obs-studio/plugin_config/looking-glass/` |

### Upgrading from 1.0

Version 1.0 kept each collection's multiviews in `multiviews/<Scene Collection>.json` in
the directory above. The first time a collection without embedded multiview data loads, its
file is imported into the collection automatically. The old files are left untouched, so
downgrading still finds them.

## Building

Looking Glass is built with the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate)
build system. `buildspec.json` pins the OBS, Qt, and prebuilt dependency versions used for
release builds.

### Supported Build Environments

| Platform  | Tool   |
|-----------|--------|
| Windows   | Visual Studio 17 2022 |
| macOS     | XCode 16.0 |
| Windows, macOS  | CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3 |
| Ubuntu 24.04 | `ninja-build` |
| Ubuntu 24.04 | `pkg-config` |
| Ubuntu 24.04 | `build-essential` |

Suggested reading from the plugin template wiki:

* [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide)
* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

### Project Layout

| Path | Contents |
|---|---|
| `src/core/` | Layout data model and JSON serialization, grid operations (`GridModel`), and persistence (`ConfigManager`) |
| `src/ui/` | Tools menu, grid editor, dialogs, and the multiview window with its OBS display |
| `src/render/` | Grid rendering into a single OBS display per window, labels, safe areas, status borders |
| `src/api/` | obs-websocket vendor requests |
| `deps/` | Vendored third-party headers (`obs-websocket-api.h`) |
| `data/locale/` | Translatable UI strings |
| `tests/` | Unit tests for the layout model, serialization, and grid operations |

### Unit Tests

The data model, its serialization, and the grid operations do not depend on libobs, so
their tests build on their own with nothing but Qt 6:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

CI runs them on every pull request and push.

### Code Formatting

C++ sources are formatted with clang-format 19.1.1 and CMake files with gersemi; CI checks
both on every changed file. Run them locally with `./build-aux/run-clang-format` and
`./build-aux/run-gersemi`.

## GitHub Actions & CI

GitHub Actions workflows are available for the following repository actions:

* `push`: Run for commits or tags pushed to `master` or `main` branches.
* `pr-pull`: Run when a Pull Request has been pushed or synchronized.
* `dispatch`: Run when triggered by the workflow dispatch in GitHub's user interface.
* `build-project`: Builds the actual project and is triggered by other workflows.
* `check-format`: Checks CMake and plugin source code formatting and is triggered by other workflows.
* `unit-tests`: Builds and runs the unit tests in `tests/` and is triggered by other workflows.

The workflows make use of GitHub repository actions (contained in `.github/actions`) and build scripts (contained in `.github/scripts`) which are not needed for local development, but might need to be adjusted if additional/different steps are required to build the plugin.

### Retrieving build artifacts

Successful builds on GitHub Actions will produce build artifacts that can be downloaded for testing. These artifacts are commonly simple archives and will not contain package installers or installation programs.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the corresponding [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).

## License

Looking Glass is licensed under the GNU General Public License v2.0 or later. See [LICENSE](LICENSE).
