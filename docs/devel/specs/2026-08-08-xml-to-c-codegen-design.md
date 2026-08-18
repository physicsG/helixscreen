# XML to C Code Generation (build-time component compiler) - Design Spec

**Date:** 2026-08-08
**Status:** CLOSED, not proceeding. Every justification was measured and none held:
CPU is 2.7% of create time, flash gets worse, and the K-Touch has 3.11 MB of PSRAM free
against a 1.05 MB prize. Kept as the record of why. The phase 1 resolver refactor is
independent and still worth doing.
**Author:** Preston Brown (with Claude)
**Component:** `lib/helix-xml` (the generator ships in the fork's own repo, not here)

## Summary

Today every HelixScreen panel is built by re-parsing XML text with expat at runtime,
every time it is created. This spec describes a build-time compiler that turns
`ui_xml/*.xml` into C translation units, so a release build creates the same widget
tree with no parser, no markup in flash, and no per-navigation parse cost. The runtime
parser stays for development builds and hot reload; both paths are generated from the
same XML files and a differential test proves they produce identical trees.

## Why

### Phase 0 measurements (2026-08-08, native SDL build, `--test -vvv`)

| Fact | Number | Source |
|------|--------|--------|
| XML files under `ui_xml/` | 338 | filesystem |
| Component markup (excl. translations) | 1.72 MB | `ui_xml` + `components` + `micro` + `portrait` |
| `<view>` bodies held resident in `scope->view_def` | **1.47 MB** | static extraction over 314 files |
| Translation XML (**not addressed by this spec**) | 2.78 MB | `ui_xml/translations`, 10 files |
| Components registered | 303 | `ctl list_components` |
| Registration of all 303, including file I/O | **10 ms** | log `+00000.362` to `+00000.372` |
| Panel navigation, first visit vs revisit | indistinguishable | see below |
| `lv_xml_create*` call sites | 205 across 125 files | source |
| Widgets registered via `lv_xml_register_widget` | 37 calls across 31 files | source |
| Idle RSS, native | ~117 MB | `MemoryMonitor` |

### What survived contact with the numbers

**Resident RAM: the case holds.** 1.47 MB of markup sits on the heap for process
lifetime. That is an ESP32 payoff; on a Pi-class board with hundreds of MB it is noise.
(The flash half of this claim does not survive Gate B below.)

**Boot CPU: the case is dead.** Registering all 303 components takes **10 ms**, file I/O
included. There is nothing here worth optimizing.

**Per-navigation CPU: the original claim was wrong.** Panels are built once and retained
(`PanelBase::rebuild` at `ui_panel_base.cpp:155`, panels held by `StaticPanelRegistry`),
so navigating does not re-parse. First visit and revisit both measure ~9 ms, which is
`ctl` process spawn, not panel construction.

The repeated-create cost is real and lives elsewhere: modals (`ui_modal.cpp:602`, `:912`
create on every show), toasts, overlays, panel widgets, and `<repeat>` rebuilds.

**Measured (Gate A, `LV_XML_PROFILE=1`, 216 `ctl` steps over every reachable panel,
overlay and modal three times):**

| | |
|---|---|
| Creates | 150 |
| View bytes parsed | 713 KB |
| Total create time | 100.2 ms |
| Inside element handlers | 97.5 ms |
| **expat / SAX dispatch** | **2.7 ms (2.7%)** |

Since `XML_Parse` drives the handlers, the two have to be timed separately or the parse
timer swallows the widget build. The handler bucket is `resolve_params`,
`resolve_consts`, `create_cb` and `apply_cb` — everything codegen keeps. Only the 2.7%
goes away.

That figure is a **floor**. Nested component creates run inside an enclosing handler
frame, so their expat time is counted as handler time; the 713 KB is outer views only
(4.75 KB per create) against 1.47 MB of view bodies in the tree. Per-parse self-time
accounting would give the exact number. It was not worth building, because even several
times 2.7% leaves the conclusion unchanged.

**So the CPU argument is dead in both halves**: 10 ms at boot, and low single-digit
percent of create time thereafter.

### Gate B: the K-Touch budget, where the flash argument inverts

Measured from `firmware/helixscreen-esp32` on branch `esp32/port-4-app`:

| Partition | Size | Used | Free |
|-----------|------|------|------|
| `ota_0` / `ota_1` app slot (**two of them**) | 6.50 MB | 6,271,568 | **544,176 (8%)** |
| `storage` (frogfs) | 2.75 MB | 2,300,456 | 583,128 (20%) |

And the frogfs image splits as `ui_xml` 654,428 bytes against `assets` 1,621,868. **The
markup costs 639 KB on the device, not 1.72 MB** — it is deflate-compressed in frogfs at
roughly 2.6:1.

That kills the flash case, and worse than kills it:

- Codegen does not delete the markup, it **relocates** it. The bytes leave `storage` as
  compressed data and arrive in the app image as string tables, where they are
  uncompressed rodata, because the app partition is XIP-mapped and cannot be compressed.
- It moves them from the partition with 583 KB free **into the one with 544 KB free**,
  and deduplicated attribute tables from 1.72 MB of markup will not fit in that.
- The app slot is doubled for OTA A/B, so every byte added to the image costs **two**
  bytes of the 16 MB budget, while the `storage` byte freed is counted once.
- Re-cutting the partition table is not a free out: per the header in `partitions.csv`,
  changed offsets require `erase_flash` and a full reflash of every device.

**Net flash effect is negative.** The change would trade 639 KB of compressed data in a
roomy partition for a larger volume of uncompressed data in the tightest one, charged
twice.

### What was left: PSRAM. Measured on the device, and it is not tight.

Console capture from the K-Touch over its CH340 (`/dev/ttyUSB0`, 115200) across a cold
`rst:0x1 (POWERON)`. The part reports `octal_psram: density 0x03 (64 Mbit)`, so **8 MB**.

| Boot stage | PSRAM free |
|------------|------------|
| `boot-ui-start` | 6,780,900 |
| `theme-up` | 6,203,568 |
| `translations-up` | 5,997,720 |
| `xml-registered` | **4,948,672** |
| `subjects-up` | 4,797,476 |
| `home-panel-up` | 3,944,320 |
| `steady-60s` (everything up) | **3,106,192** |

**Registering all 303 components costs 1,049,048 bytes of PSRAM** (`translations-up`
minus `xml-registered`). That is the entire prize, and it is smaller than the 1.47 MB the
native-side static count suggested.

**At steady state the device still has 3.11 MB of its 8 MB free.** Reclaiming the markup
would take that to roughly 4.2 MB. Nothing needs it.

### Conclusion: do not build this

Every justification has now been measured and none survives.

| Claim | Verdict |
|-------|---------|
| Boot CPU | 10 ms. Nothing to win. |
| Per-navigation CPU | Wrong premise. Panels are built once and retained. |
| Per-create CPU | 2.7% of create time is expat. The rest is work codegen keeps. |
| Flash | Negative. Relocates compressed data into the tighter partition, charged twice for OTA A/B. |
| PSRAM | 1.05 MB, against 3.11 MB already free at steady state. |

Phases 2 through 5 are **closed**. This document stands as the record of why, so the idea
does not get proposed again from first principles.

**Phase 1 is unaffected and still worth doing.** The resolver defects are real on their
own terms: in-place mutation of the caller's attribute array, three ownership regimes
with nothing marking which is which, and attribute removal encoded as `""` poisoning.
None of that needed a compiler to justify fixing.

## The seam this targets

The important structural fact is that the XML engine already funnels every element
through a two-callback interface:

```c
typedef void * (*lv_xml_widget_create_cb_t)(lv_xml_parser_state_t *, const char ** attrs);
typedef void   (*lv_xml_widget_apply_cb_t) (lv_xml_parser_state_t *, const char ** attrs);
```

`view_start_element_handler()` (`lv_xml.c:2194`) does a fixed sequence per element:
push pcdata, resolve the parent, `resolve_params()`, `resolve_consts()`, look up the
processor by name, `create_cb()`, `apply_cb()`, push the new parent.

**Codegen targets that sequence, not the individual setters.** The generated code hands
the same `(name, attrs)` pairs to the same processors. This is what makes the project
tractable: all 25 widget parsers and all 31 C++-registered custom widgets keep working
untouched, because their `apply_cb` still receives the string attribute array it expects.
No per-widget migration, no attribute coverage matrix, no semantic drift.

What disappears is expat, the SAX state machine, the buffered-fragment replay, and the
markup itself. What stays is attribute interpretation, which is where all the widget
specific behavior lives.

## Generated output

### Element tree

For ui_xml/components/foo.xml:

```xml
<component>
  <api><prop name="title" type="string" default="Hi"/></api>
  <view extends="lv_obj" width="100">
    <lv_label text="${title}"/>
  </view>
</component>
```

the generator emits:

```c
/* GENERATED from ui_xml/components/foo.xml - do not edit */
static const char * const A0[] = { "width", "100", NULL };
static const char * const A1[] = { "text", "${title}", NULL };

static void * gen_foo_view(lv_xml_parser_state_t * st, lv_obj_t * parent)
{
    lv_obj_t * v  = lv_xml_emit_element(st, "lv_obj",   A0, parent, /*is_view=*/true);
    (void)          lv_xml_emit_element(st, "lv_label", A1, v,      false);
    return v;
}
```

`lv_xml_emit_element()` is a new, small runtime helper holding exactly the body of
`view_start_element_handler` from `state->tag_name = name` onward. Step one of the
implementation is to refactor the existing handler to call it, so the runtime path and
the generated path share one implementation and cannot diverge.

Note that `A0`/`A1` are `const` and shared by every instantiation, which today's
resolver cannot accept. See "Attribute resolution" below, it is a prerequisite.

### Component metadata

`<api>`, `<consts>`, `<styles>`, `<subjects>`, `<subject_expr>`, `<fonts>`, `<images>`,
`<gradients>`, `<timelines>` are parsed at registration time by
`start_metadata_handler`. The generator emits a scope initializer that calls the same
registration functions directly:

```c
static void gen_foo_register(void)
{
    lv_xml_component_scope_t * s = lv_xml_scope_create("foo");
    lv_xml_register_const(s, "row_h", "40");
    lv_xml_param_add(s, "title", "string", "Hi");
    lv_xml_scope_set_builder(s, gen_foo_view, "lv_obj");
}
```

With both the metadata parse and the view parse generated, `scope->view_def` is never
allocated and expat has no remaining caller.

### Reactive fragments

`<repeat count="subject">` and `<if cond="...">` currently buffer raw SAX events into
`xml_frag_capture_t` and replay them on subject change. Generated code replaces the
event buffer with a builder function pointer plus the index to inject, which is both
smaller and faster:

```c
static void gen_foo_rep3(lv_xml_parser_state_t * st, lv_obj_t * parent, int32_t i);
```

`xml_frag_record_t` keeps its observer, teardown, and reentrancy guard exactly as they
are; only the `capture` field changes representation. A `<repeat count="4">` with a
literal count and an `<if>` over a constant expression are unrolled or resolved at
generate time and cost nothing at runtime.

## Attribute resolution (prerequisite refactor)

Codegen cannot sit on top of the resolver as it stands, and the reason is worth fixing
properly rather than working around.

`resolve_params()` (`lv_xml.c:1111`) and `resolve_consts()` (`:1244`) **mutate the
caller's attribute array in place**, repointing value slots at parameter or const
storage. Generated arrays are `const` and shared across every instantiation of a
component, so the mutation is not merely unsafe, it would corrupt the template on the
first create and every subsequent create would see the resolved values of the first
caller. A scratch copy per element would hide the problem; the array itself is the
problem.

Four other things fall out of the same design:

- **Three ownership regimes coexist in one array** with nothing marking which is which:
  borrowed repoints (params, consts), owned allocations in `state->composed_strings`,
  and per-expansion transients in `idx_strings`. The defensive comments at
  `lv_xml_parser.h:117-127` exist to hold this together by hand.
- **"Drop this attribute" is encoded by poisoning both slots with `""`** (`:1178`,
  `:1273`), so every downstream `apply_cb` iterates over dead entries.
- **`#` is ambiguous**, meaning both "const reference" and "hex color", disambiguated by
  `is_hex_color()` counting six hex digits (`:1229`). A const named `ABCDEF` is
  unreachable.
- **`$prop|ref` packs two values into one attribute string** (`:1151` onward) because only
  whole-value substitution exists, and the obj parser splits on the pipe downstream.

### The change

Resolution becomes a pure function: immutable template in, resolved attributes out.

```c
lv_xml_attrs_t * lv_xml_resolve(const char * const * tmpl,
                                const lv_xml_resolve_ctx_t * ctx);
```

`lv_xml_attrs_t` carries per-entry `name` / `value` / a flag for borrowed versus
arena-owned, and a `dropped` bit in place of the `""` poisoning. One arena, allocated
per create and freed at the end of it, replaces all three lifetime regimes.
`lv_xml_resolve_ctx_t` bundles what resolution actually reads: item scope, parent scope,
parent attrs, and the active repeat index.

Both callers use it identically, `view_start_element_handler` for the runtime path and
`lv_xml_emit_element` for the generated path, so there is one resolver and no way for
the two paths to disagree.

Worth doing on its own terms: the resolver becomes testable as a pure function with no
LVGL, no expat, and no widget tree, which is the one part of this system that currently
has no direct test coverage.

### Deliberately out of scope

The `#` ambiguity and the `|` packing are real defects, but fixing either changes the
XML dialect and means touching some of the 338 files. Keep them exactly as they are
here. The refactor changes the plumbing, not the language.

### Const folding is not on the table

The tempting next step, resolving `#const` references at generate time, does not work.
Consts are mutated after registration: `src/xml_registration.cpp:200` calls
`lv_xml_update_const()` on `grid_swatch_size` / `grid_gap` / `grid_width`, and `:106-109`
registers consts computed from measured layout at runtime. `#const` must stay a runtime
lookup. `$param`, `${...}` composition, and `$i` are caller-dependent and were never
foldable either, so essentially all resolution remains at runtime by design. The win
from codegen is the removal of parsing, not of resolution.

## The generator

A **host tool**, built and run on the build machine, never cross-compiled.

It needs expat, the component metadata parser, and a tree emitter. It does **not** need
LVGL, widgets, or `apply_cb`, because attribute values are never interpreted at generate
time, for the reasons in "Const folding is not on the table" above. The generator moves
markup into C; it does not evaluate it.

That keeps the tool small and low-risk: it walks the same XML with the same parser
sources and prints C instead of building widgets.

Output: one `.c` per XML file plus one registry `.c` replacing the 300 `register_xml()`
calls in `src/xml_registration.cpp`.

String tables are deduplicated across all 338 files. Attribute names in particular
repeat heavily, so the emitted table is far smaller than the 1.58 MB of source markup.

## Build integration

**Native (`make`):** a pattern rule `ui_xml/%.xml -> build/gen/%.c`, with the generator
built as a host binary. Generated sources are build artifacts, gitignored, never
committed. Regeneration is driven by mtime like any other target.

**ESP-IDF (`firmware/helixscreen-esp32`):** the `helixcore` component already globs
`${REPO_ROOT}/lib/helix-xml/src/xml/*.c` (`components/helixcore/CMakeLists.txt:27`), so
this adds an `add_custom_command` for the generator plus the generated directory in
`SRCS`. When codegen is on, `scripts/esp32_stage_assets.py` drops `ui_xml/` from the frogfs image
entirely.

**Selecting the path:** `LV_USE_XML_RUNTIME_PARSER` in `lv_conf.h`. Off means expat,
`lv_xml_parser.c`, and the metadata handlers compile away, and
`lv_xml_register_component_from_file()` is not available. Dev builds leave it on.

## Development flow is unchanged

Hot reload (`HELIX_HOT_RELOAD`) depends on re-registering components from files at
runtime, which is exactly what codegen removes. So:

- **Dev builds:** runtime parser, hot reload, XML edited live with no rebuild. Unchanged.
- **Release builds:** generated C, no parser, no markup.

Same XML source either way. The XML stays the authoring format, which preserves the
declarative-UI rules in `CLAUDE.md` untouched. Nobody writes or reads generated code.

## Verification

The acceptance gate is a **differential test**, not eyeballing.

A desktop test binary compiles both paths, then for each of the 300 components builds
the tree twice, once through `lv_xml_create()` and once through the generated builder,
and walks both trees comparing:

- widget class and child count at every node
- `lv_obj_get_name()`
- computed coordinates after a forced layout
- a fixed set of style properties per node
- observer count on bound subjects

Any mismatch is a generator bug and fails the build. This is cheap to run, covers every
component including the 31 custom C++ widgets, and it is the only way to be confident
that 338 files came through unchanged. `lv_xml_test.c` already exists in the engine and
is the natural home for the harness.

Beyond that: the existing suite must stay green with `LV_USE_XML_RUNTIME_PARSER=0`, and
the ESP32 build has to boot to the home panel on the K-Touch.

## Phasing

0. **Baseline. Done 2026-08-08**, including Gate A. See the measurements above.
   Nothing further to measure on the native side; what remains is the K-Touch
   flash and PSRAM budget, which decides whether phases 2-5 happen at all.
1. **Attribute resolution refactor.** `lv_xml_resolve()` as a pure function, arena
   ownership, `dropped` bit, with unit tests on the resolver in isolation. Then extract
   `lv_xml_emit_element()` from `view_start_element_handler` on top of it. Both steps
   are pure refactors: the existing suite must be green with no test changes, and a
   screenshot pass over the main panels should be pixel-identical.
2. Host generator: element tree plus component metadata. No build integration yet.
3. Differential test over all 300 components. Iterate until clean.
4. `<repeat>` and `<if>` builder functions.
5. Build integration on both native and ESP-IDF, add `LV_USE_XML_RUNTIME_PARSER=0`,
   re-measure against phase 0.
6. Optional: direct setter calls for widgets whose parsers are pure attribute tables,
   skipping `apply_cb` string dispatch. Per-widget opt-in with fallback, and only if
   phase 5's numbers say the remaining dispatch cost is worth it.

Phases 1 through 3 are independently useful and can land without any build change.
Phase 1 in particular stands on its own: if codegen is abandoned after it, the resolver
is still better than what is there now.

## Risks and non-goals

- **Error messages get worse.** Today a bad component logs an expat line number. The
  generator must emit the source file and line into each element record so runtime
  warnings stay traceable. This is a requirement, not a nicety.
- **Phase 1 touches the hottest path in the engine** and every component depends on it.
  A resolver bug will not look like a resolver bug, it will look like one attribute
  quietly not applying somewhere in 338 files, which is the exact failure mode the
  `""` poisoning already produces today. The resolver unit tests have to land with the
  refactor, not after it.
- **Compile time goes up.** 338 generated translation units is not free. Measure at
  phase 5, and if it hurts, emit one TU per directory rather than per file.
- **A stale generated tree is a new failure mode**, the same class as the existing
  "stale binary, unregistered widget" error. The dependency rules have to be right, and
  CI should build both paths on every PR.
- **Not a goal:** replacing XML as the authoring format, changing the XML dialect, or
  exposing generated code as an API. It is an internal build artifact.
- **Not a goal:** an editor, a preview tool, or anything visual.

## Clean-room note

C export from a UI description is a paid feature of the LVGL Editor. Per the fork's
clean-room rule in `lib/helix-xml/README.md`, nothing here may be derived from LVGL Pro
source or a decompiled Editor. In practice this design has no exposure: it is a codegen
backend over our own parser sources, targeting a callback seam that exists in the MIT
9.4 code, and none of it requires knowing how anyone else did it.
