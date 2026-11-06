# Shareable layouts

A **layout file** saves a dashboard — figures, subplot grid, axes limits and
labels, series styles and colours, transform presets, the dock arrangement and
the theme — together with **where each series' data comes from**, but not the
data itself. A teammate opens the same file against their own CSV files or
their own live system.

| Action | Where |
|--------|-------|
| Save the current window as a layout | **File > Save Layout...** |
| Open a layout (adds its figures as tabs) | **File > Open Layout...** |
| Reopen a recent layout | **File > Recent Layouts** |
| Open a layout at startup | `spectra --layout dashboard.spectra` |
| See / fix bindings | **File > Data Bindings** |

## File format

A layout is a workspace JSON file (`.spectra`, format version 4) with two
extra top-level keys:

```json
{
  "version": 4,
  "layout_only": true,
  "layout_version": 1,
  ...
  "series": [
    {
      "name": "accel_x",
      "type": "line",
      "axes_index": 0,
      "binding": "topic://imu/accel_x",
      "point_count": 0,
      ...
    }
  ]
}
```

The keys are optional, so older builds open a layout as an ordinary
workspace, and existing v1–v4 workspaces still load. Layouts leave out
per-user and per-session state: data tips (markers), undo counts, modified
flags, shortcut overrides, plugin state and the last export directory. Dock
panes refer to figures by position in the file, not by this session's ids.

## Binding URIs

| URI | Meaning |
|-----|---------|
| `topic://<name>` | Live [Spectra topic](topics.md), e.g. `topic://imu/accel_x` |
| `csv://<file>#<column>` | Column of a CSV file; x is the row index |
| `csv://<file>?x=<column>#<column>` | Same, with an x column, e.g. `csv://flight.csv?x=time#alt` |
| `ulog://<topic>.<field>` | PX4 ULog field, e.g. `ulog://vehicle_attitude.roll` — **reserved**: saved and shown, not resolved yet |

CSV files are stored by file name only. `%`, `#`, `?`, `&`, `=` and control
characters in names are percent-encoded (`%23` for `#`). Scheme names are
case-insensitive.

Bindings are recorded when a series is created: dropping a topic on a plot or
pressing **Plot** in the Topics panel records `topic://<name>`, plotting
columns from **Load CSV** records `csv://<file>?x=<x column>#<y column>`.
Series created from code have no binding unless you set one with
`Series::set_data_binding()`.

## Opening a layout

Each series is created empty with its saved style, then its binding is
resolved:

- **topic://** — subscribes to the topic. If no publisher has declared it yet,
  the subscription is *pending* and starts receiving samples as soon as the
  publisher appears (status *Waiting*). Axes with a live topic follow the data
  until you pan or zoom, as with a dropped topic.
- **csv://** — the file is looked up next to the layout file, then in the
  working directory, and the column is matched by header name (`Column N` for
  files without a header row). The axes' saved transform steps (scale, offset,
  abs, ...) are applied to the loaded data.
- **ulog://** — reported as unresolved for now.

A series whose binding cannot be resolved stays empty and its legend shows
`<label> [unresolved: <uri>]`. The **Data Bindings** window opens and lists
every binding with its status; select one to type a replacement URI, pick a
live topic, or **Locate file...** for a missing CSV (all series bound to the
same file name are retried). Saving the layout again writes the original
labels, not the `[unresolved: ...]` text.

When a layout is opened in an empty window it also restores the theme, data
palette, panel visibility and dock arrangement; opened next to existing
figures it only adds tabs.

## Limitations

- Topic bindings resolve in the in-process app (`spectra`, the default when no
  daemon is running). `spectra --layout` stays in-process; in a multi-process
  `spectra-window`, topic bindings are reported as unresolved.
- Only 2D line and scatter series are rebuilt; 3D, surface, mesh and bar
  series in a layout are skipped.
- Transform steps are applied to CSV data, not yet to live topics.
- The ROS2 app keeps its own session format (`ros_session.hpp`); it does not
  read or write layout bindings yet.
