# Ideas

Possible future additions for Looking Glass, roughly from smallest to largest.

## Improvements to existing features

- **More obs-websocket requests**
  - `GetMultiviewList`: every multiview with its open and fullscreen state.
  - `SetMultiviewFullscreen`: fullscreen on a named monitor.
  - `SetCellContent`: change what a cell shows from outside OBS.
  - Events when windows open or close.
- **Tally on source cells:** show the red/green border when a source is visible in Program or Preview, including inside nested scenes.
- **Richer labels:** text color, outline or shadow, and tokens such as `{name}`, `{time}` and `{status}`.
- **Template import/export:** share one template as a `.json` file instead of the single global `templates.json`.
- **Spacing between cells:** a gap between cells and padding inside each cell, as an alternative to grid lines.
- **Clock options:** 12-hour time, a date line, and a countdown to a duration rather than a time of day.

## New cell types

- **Status:** streaming and recording state, dropped frames, CPU usage and FPS.
- **Browser / chat:** a browser source in a cell, e.g. Twitch or YouTube chat on a confidence monitor.
- **Media timeline:** progress bar and time remaining for a playing media source.

## Bigger ideas

- **Send a multiview out:** to the virtual camera or NDI, to feed another PC or a hardware monitor without a window.
- **Layout per scene:** the wall switches layout automatically when the Program scene changes.
- **Audio follow:** highlight or enlarge the cell of whatever is on Program.
- **Lock / kiosk mode:** fullscreen with no context menu or clicks, for control room or green room screens.
