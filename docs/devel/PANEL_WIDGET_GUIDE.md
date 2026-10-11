# Panel Widgets (Developer Guide)

How home-dashboard widgets are built, and the reference pattern for making
one size itself responsively. The nozzle-temps widget is the exemplar to copy:
`src/ui/panel_widgets/nozzle_temps_widget.{h,cpp}`,
`src/ui/panel_widgets/nozzle_layout.h`, and its three XML components. The eighteen
centred-icon tiles run the same pattern one instance deeper; see
[The tile instance of the pattern](#the-tile-instance-of-the-pattern) below, and the
printer image's live callouts are a third instance
([The printer image callouts instance](#the-printer-image-callouts-instance)).

**Related**: `LAYOUT_SYSTEM.md` (the home grid that sizes tiles),
`LVGL9_XML_GUIDE.md` (bindings), `ARCHITECTURE.md` (subjects).

---

## The pieces of a widget

| Piece | Where | Notes |
|-------|-------|-------|
| Registry def | `src/ui/panel_widget_registry.cpp` | id, display name, icon, category, default/enabled, colspan/rowspan and their min/max, whether it may span half cells |
| Widget class | `src/ui/panel_widgets/<name>_widget.{h,cpp}` | extends `PanelWidget`; implements `attach()` / `detach()` / `on_size_changed()` |
| XML component | `ui_xml/components/panel_widget_<name>.xml` | the entire appearance |
| Row/child components | `ui_xml/components/<name>_*.xml` | repeated fragments the widget creates per item |
| Registration | `register_<name>_widget()` | factory + `register_widget_subjects()` (see below) |
| Placement | `assets/config/panel_widgets/<preset>/home.json` | per-printer-preset seeds; users rearrange at runtime |

Not every definition is compiled in. A Lua plugin registers its widgets at load
time as runtime definitions (`register_runtime_widget_def`,
`include/panel_widget_registry.h`): they land in the catalog's Plugins category,
are never enabled by default, and their ids carry the plugin's `<id>__` prefix.
The saved layout keeps ids it has no definition for
(`PanelWidgetConfig::parse_widget_array` in `src/system/panel_widget_config.cpp`),
so a plugin widget's placement survives a disabled or absent plugin and returns
when the definition is registered again. The registry bumps a generation counter
on every runtime register and unregister (`runtime_widget_generation`), because a
plugin reload re-registers the same ids while the factories now build widgets
bound to a different runtime.

## The reference pattern: measure, decide, publish, bind

A widget that sizes itself responsively is four layers with exactly one
responsibility each. `NozzleTempsWidget` (#1613) is the working instance.

**1. A pure decision function, no LVGL.** All sizing logic lives in a
header-only function next to the widget (`nozzle_layout.h`) taking measured
pixel inputs and returning a small verdict struct. It is unit-testable
without a display, and the tests' fixtures are the widths the widget really
measured, so the boundaries they pin are the live ones. Split the decision
the way the questions actually split — for nozzle-temps, the font tier is a
HEIGHT question (a stack that overflows the tile overflows it at any width)
and the label rung a WIDTH question (a row is one line tall whatever it
says, so label richness never costs height). When two axes both matter,
model both explicitly rather than folding one into constants.

**2. Measurement in the widget, one composer for render and measure.**
`on_size_changed()` measures text in the fonts the rows actually render, and
the strings it measures come from the same composer that `update_row_display`
renders — measurement and rendering cannot drift apart, or the ladder decides
on widths the row does not draw. Measure the values the rows are ACTUALLY
showing, not worst-case glyphs: an "888°" budget excludes normal-font rungs
from tiles where every real value fits.

**3. Publish the verdict as subjects.** One int subject per axis
(`nozzle_row_label_mode`, `nozzle_row_columns`, `nozzle_row_compact`),
registered via `register_widget_subjects("<id>", fn)` so they exist before
any XML that binds them is parsed — the parser permanently skips a binding
whose subject does not exist at parse time. `on_size_changed()` becomes
measure-and-publish with zero widget calls. Enum values ARE the subject ints
(`NozzleLabelMode`), so XML `ref_value`s cannot drift from C++.

**4. Bind every appearance in XML.** Row widths, container flow, label
visibility, fonts — all `bind_flag_if_*` / `bind_style_if_*` off the
subjects. A row created by a late rebuild reads the current values at
creation and agrees with its siblings with no seeding pass. Prefer one
threshold bind (`bind_flag_if_lt subject=... ref_value="2"`) over enumerated
equality pairs; it reads as the predicate it is. A style the binds apply lives in
the widget's own `<styles>` block when one file uses it; when several widgets bind
the same look, it lives once in `ui_xml/styles.xml` and every file binds it by
dotted name (`bind_style_if_eq name="styles.<name>" ...`).

### Degradation design

Let content degrade before identity does: the nozzle-temps value drops its
target half (one tap away in a detail view) before the row drops its tool
number, and the number before the icon. Budget each rung for what it
ACTUALLY draws at that rung — the target-hiding rungs measure the
current-only value. Re-decide when the value's width class changes (a
target setting or clearing), so a tile sized under idle values never draws
the wide value in the narrow rung.

### The tile instance of the pattern

The eighteen centred-icon tiles run the same four layers one instance deeper: a tile has
no content of its own to arrange, so the rung its glyph draws at IS the layout
(prestonbrown/helixscreen#1559). Pure decision: `helix::decide_tile_layout()`
(`src/ui/panel_widgets/tile_layout.h`) takes measured content widths per icon rung and
returns a `TileVerdict` - the rung, the direction (icon above or beside the value), label
visibility, whether the target half is drawn, and `fits`. Content is surrendered in a
fixed order (the target half first, then the label), and within each step the largest
rung that fits wins, so a tile grows its glyph rather than its text.

- **`helix::TileSizing`** (`src/ui/panel_widgets/tile_sizing.h`) is the measure/decide/
  publish layer, one per widget INSTANCE - `fan`, `thermistor` and `power_device` are
  multi-instance, and a type-global subject would make the second instance overwrite the
  first's verdict. It measures the WORST-CASE value strings and publishes four subjects:
  icon rung, label, direction, show-target. Budget the string the tile can ACTUALLY
  render, including any part a sibling label draws: `temp_display` puts the unit in its
  own label, so a heater budgeted as "888 / 888" is narrower than the row it draws, and a
  sensor reading is seven glyphs ("110.0°C"), not three.
- **The verdict is computed against the content box, not the tile.** `TileSizing` measures
  the padding and flex gap of every container between the tile's outer edge and its glyph
  (`set_content_root()`), because that chrome is a theme value that varies by widget and
  by tier. Estimating it picks a rung that spills the tile's own container.
- **Constructor registration is load-bearing.** A tile's subjects are per-instance, so
  they register in the widget's CONSTRUCTOR, never from `attach()`. The manager creates
  the widget, then calls `lv_xml_create()` on its component, and only then `attach()`;
  the parser permanently and silently drops a binding whose subject does not exist at
  parse time, so subjects registered from `attach()` never bind and the tile renders at
  its default appearance forever. The nozzle-temps spelling satisfies the same constraint
  with `register_widget_subjects()`, which runs at registration, before any XML is parsed.
- **Per-instance subject names reach XML through `xml_attrs()`.**
  `PanelWidget::xml_attrs()` returns the flat key/value list `lv_xml_create()` takes; the
  manager passes it when creating the component, and the component's `tile_icon_subject`
  prop carries the instance's rung subject name. Whatever supplies the attrs is
  constructed with the widget, for the same parse-time reason.
- **`PanelWidget::fits_at()` is the refusal path.** Defaults to true; a tile overrides
  it with `TileSizing::fits()`. It must be MONOTONIC (fits at a size means fits at every
  larger size), because the clamp walks outward assuming the first accepting size is the
  nearest one. Edit mode's resize clamp and the load path both consult it through
  `helix::grow_span_to_fit()` (`include/grid_layout.h`), so the rule lives once.
- **`helix::TiledPanelWidget`** (`src/ui/panel_widgets/tiled_panel_widget.h`) owns the
  TileSizing and the four hooks that reach it (`on_size_changed`, `fits_at`, `xml_attrs`,
  `tile_sizing`). A tile with behaviour derives from it and passes its TileSizing
  arguments to the base initializer; a tile with its own fit rule overrides `fits_at()`.
- **`helix::TileWidget`** (`src/ui/panel_widgets/tile_widget.h`) is a `TiledPanelWidget`
  for a tile that has no widget class of its own: `notifications`, `firmware_restart`,
  `humidity`, `width_sensor` and `lock` (rows of `kPureXmlTiles` in `tile_widget.cpp`).
  Every other centred-icon tile owns one on its own class, including the three heaters,
  which share `HeaterTempWidget`, and `power_device` and `filament`, whose classes sit
  outside `src/ui/panel_widgets/`. It is registered LAST and skips any id a class
  already took, because the last factory registration wins and a sizing-only shell
  would otherwise replace real behaviour. A tile that grows real behaviour stops using
  it and derives `TiledPanelWidget` directly.
- **The root's `user_data` is the base's.** `attach_tile()` binds the tile root (and
  `root()` returns it) before `attach()`, `detach_tile()` clears it after `detach()`, on
  every reuse, so a widget never sets or clears it. Anything that hands a widget a tree
  calls those two, not `attach()`/`detach()` directly.

On the XML side, every part of a tile binds its face with
`<bind_tile_rung ladder="icon|value|label" subject="$tile_icon_subject"/>`
(`include/ui_tile_rung.h`). The ladders there are the one table TileSizing measures in
and the binding draws in, and each rung names a TOKEN (`#icon_font_*`, `font_*`), never a
literal face, because a literal face a platform did not link renders tofu.
The icon's top rung, xxl, is a size rather than a token (`#tile_icon_xxl_size`, twice the
tier's xl): it draws in the largest MDI face the build links at or below that size and scales
it up to reach it, at most 2x (`ui::TileFace`), so a tile grows past a cell and a half on every
platform. Which boards link the larger faces is decided in `mk/fonts.mk`.
`styles.tile_column` / `styles.tile_row` in `ui_xml/styles.xml` carry the direction. The seven single-icon action tiles share
`ui_xml/components/home_action_tile.xml`, whose `tile_icon_subject` prop installs the
per-instance rung binding (empty installs none). The Controls panel's calibration cells and
Motors Off use the same component; its props are listed in
[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md#home_action_tile).

### The printer image callouts instance

`PrinterImageWidget` (`src/ui/panel_widgets/printer_image_widget.{h,cpp}`) runs the pattern
over live chips on the home printer picture: temperature chips for the nozzle, bed and
chamber, the part fan's speed, and a light chip (prestonbrown/helixscreen#1397). The pure
decision is `helix::compute_callout_layout()` (`src/ui/panel_widgets/callout_layout.h`),
which returns a `CalloutMode`, the image rect, and each chip's rect and leader line.

- **The mode ladder.** Image only (one cell on both axes, or an image too short for three
  chip heights); both sides (each side band of the centred image fits a chip column, each
  chip on the side nearer its point); one side (the image moves to the near edge and every
  chip stacks in the far band); pinned (tagged image, no band: chips sit on their points,
  nozzle + fan merge into one toolhead chip, and chips that would overlap slide apart);
  docked (untagged image: chips in the free band, else along the bottom edge, never a
  line). A tile taller than the image's aspect runs the same ladder with bands above and
  below. When neither line mode fits at the contain-fit size, the image may shrink by up
  to `kMaxImageShrinkPct` (8%), just enough for one side, then for both, before pinned.
- **The budget decides the mode; the active chips get positions.** `CalloutLayoutInput`
  carries both: `budget` is every chip this printer can ever show at its widest text,
  `active` is what shows now. Fitting against the budget is what keeps the image still
  as chips come and go. A capability (`printer_has_led`, `printer_has_chamber_heater`)
  changes the budget, so its observer relayouts even when no chip text changes.
- **Chips are measured with the composer they render.** `apply_callout_layout()` measures
  in the chips' own fonts, takes padding and border from the live chip's style, and
  `around_text()` is the single expression for everything in a chip except its text. It
  sizes the chip and bounds the label's `max_width`, so a chip clamped narrower than its
  text ends in dots instead of spilling.
- **Pinned chips are compact.** A pinned chip sits on the picture, so it drops its icon
  and unit letter and sets its text in `font_xs` (`printer_callout_mode` bindings in the
  XML). The text and the chip's width and height come from
  `src/ui/panel_widgets/callout_chip.h`, which the image tagger's review uses too, so its
  preview is the home chip. The budget pass still measures full chips to pick the mode; a
  second pass without the budget places the active chips at their compact size. The light
  chip keeps its xs icon, which at some breakpoints is taller than a `font_xs` line, so
  while it shows every chip slot takes the icon's height. The merged toolhead chip exists
  only in pinned mode, so it is text alone.
- **Geometry is set only from the deferred timer.** Observers publish subjects and call
  `schedule_callout_layout()`; the one-shot timer measures and places. Nothing forces a
  layout pass during a grid rebuild (#983, #1025). Unchanged coordinates are not
  rewritten, since every style write invalidates and temperatures relayout every tick.
- **One side and the exact-size image cache.** A moved image declares a pixel rect whose
  coords only follow at the next layout pass, and the cache check runs from timers that
  can fire first, so the cache reads the DECLARED size
  (`src/ui/panel_widgets/printer_image_widget.cpp#declared_image_size`), never the coords.
  A generated copy whose size no longer matches the declared rect is dropped and the check
  rescheduled. An exact copy is shown 1:1: `show_exact_copy()` sets the inner align to
  CENTER, then `LV_SCALE_NONE`, then the source, because LVGL keeps the scale CONTAIN
  computed until the align changes and refuses a scale while CONTAIN is still set.
- **Pinned chips slide apart.** After the pinned chips are placed on their points (and
  after the toolhead merge and docking), `callout_detail::slide_apart()`
  (`src/ui/panel_widgets/callout_layout.h#slide_apart`) groups pinned chips whose x-ranges
  intersect into columns and spreads each column vertically with the same `spread_1d()`
  the side bands use, between the top edge and the docked row. A chip moves only within
  its column, only as far as `spread_1d()` needs, and never horizontally; a lone chip
  moves only to clear the docked row. A column too tall to stack there gives up its
  lowest-priority chip (light, fan, chamber, then bed; never the nozzle or toolhead) to
  the docked row, and the layout runs again until every column fits.
- **Leader lines are elbows.** Each line is three points: the tagged point, an elbow, and
  the middle of the chip's inner edge. From the point it runs at 45 degrees toward the
  chip's height, then straight into the chip: level into a side-band chip, vertical into a
  chip above or below the image. When 45 degrees would leave less than `min_line` of
  straight run, the diagonal steepens to keep that stub, and it never runs back past the
  point. `callout_detail::leader_elbow()`
  (`src/ui/panel_widgets/callout_layout.h#leader_elbow`) is the rule;
  `stack()` records the elbow in `CalloutChipOut::line_xm` / `line_ym`.
- **Leader lines keep their points alive.** `<leader_line>` (`include/ui_leader_line.h`)
  is a bare `lv_line`, and `lv_line` keeps the pointer it is given, so each line's three
  points live in the widget (`callout_line_pts_`, one set per `CalloutKind` that can draw
  a line) for as long as the line exists.
- **A chip's shown int has three values.** `callout_<kind>_shown` is 0 hidden, 1 active,
  2 residual: a heater that is off but still above 50C. XML hides a chip on `eq 0`, and
  `activity_chip` takes the chip's shown subject as `shown_subject` and binds a
  `#text_subtle` style on its label for 2, so a residual chip's text is greyed with no C++
  styling (the chip text is already `#text_muted`).
- **Where the points come from.** Each shipped image's nozzle, bed edge, part fan, chamber
  and light are hand-tagged, normalized over the source PNG, in
  `assets/images/printers/regions.json`; `assets/images/printers/README.md` documents the
  format and `tools/printer-regions-tagger.html` is the tagging tool. `[regions]` fails,
  naming the image, when a PNG no longer matches its recorded size.
- **Users tag their own.** Tag parts in the printer image picker opens
  `PrinterImageTaggerOverlay` (`src/ui/ui_overlay_printer_image_tagger.cpp`), which walks
  `helix::ImageTagSession` through the same six prompts on the image the widget displays and
  saves to `<config dir>/printer_image_regions.json` through `save_user_image_regions()`.
  Both files are keyed by `printer_image_region_key()`, which gives a custom photo
  `custom:<name>`. `lookup_image_regions()` prefers a user entry, but only when its `size`
  matches the displayed image's natural size, so tags made on another screen tier, or on
  shipped art that has since been re-cut, fall back to the shipped entry or to docked chips.
  The size cannot tell one custom photo from another of the same aspect, so
  `PrinterImageManager::import_image()` and `delete_custom_image()` clear that image's tags.
  A save or reset changes memory only after the file is written. A user file that does not
  parse is moved aside to `printer_image_regions.json.bad` on load, so tagging carries on;
  one that cannot be read, or moved, is never written over. Save and Reset tags bump
  `PrinterImageManager::notify_image_changed()`, which relayouts the widget.

### Engine contracts this pattern relies on

- A `<style>` carrying `flex_flow` must ALSO carry `layout="flex"` — the
  inline attribute path sets both, the style path does not, and a container
  without an active layout piles every child onto the first while
  `lv_obj_get_style_flex_flow` still reports the flow. Test layouts by
  asserting `LV_STYLE_LAYOUT` and child positions, not the flow value.
- Inline `style_*` attributes write LOCAL styles that outrank any bound
  style (declarative rule 6) — including `style_pad_all`, which shadows a
  per-state bound `pad_top`. Split the inline all-sides form when any side
  is bound.
- A measured-exact fit renders as an overlap: measurement and rendered width
  disagree by a few pixels, so keep a comfort margin on text-budgeted rungs
  and pin one-line labels (`long_mode="dots"`) when the stack model budgets
  one line.
- Bindable fonts on semantic `text_*` widgets work because the semantic font
  rides a shared ADDED style (#1614). An `<icon>`'s face and `temp_display`'s four labels
  ride the same mechanism (`helix::ui::apply_font_style`,
  `include/helix/ui/shared_font_style.h`), which is what the tile rung binds rely on.
  Do not reintroduce local font writes.

## Testing the pattern

- Pure decision tests on measured-live fixtures, every boundary exact
  (`[nozzle][layout]`).
- Widget tests asserting the BOUND OUTCOME: which label carries HIDDEN, the
  resolved width/flow/layout, distinct cell positions — not cached decisions
  (`[widget_size][nozzle_temps]`). Drive value-shape transitions through the
  real subjects to pin the re-decide.
- `test_widget_content_fits.cpp` sweeps every widget at every shipping
  geometry at its authored minimum; its baseline entries are the honest
  record of which tiles sit below a widget's floor.
