# Macro Panel Edit Mode — Design Spec

**Date:** 2026-07-24
**Status:** Approved design, pre-implementation
**Author:** Preston Brown (with Claude)

## Summary

Long-pressing a macro in the macro list enters an **edit mode** where every macro is
shown with a checkbox to its left. A checked box means the macro is **visible** in the
normal list; unchecked means it is **hidden**. A **Save** button appears in the header.
The hidden set is persisted per-printer to `settings.json`. Leaving edit mode without
saving discards the pending changes.

Secondary goal (explicitly requested): convert the macro list rows from the current
C++ create-and-wire loop to **declarative XML** using the new parser features
(`<repeat>`, `${expr}` interpolation, indexed-subject bindings). This surfaces a
**reusable "reclaim-on-close" dynamic subject pool** pattern that is intended for use
elsewhere in the app.

## User story / behavior (approved decisions)

- **Enter edit mode:** long-press any macro row.
- **Control style:** a real **checkbox** to the left of each row (checked = visible).
  Not a `ui_switch` — checkbox semantics fit a "pick selections, then Save" flow.
- **What shows in edit mode:** **every** discovered macro, including the normally-hidden
  `_`-prefixed system macros, each with its own checkbox. This lets the user opt a
  system macro *into* the list.
- **Normal mode list:** only macros **not** in the saved hidden set.
- **First-run seed:** when no hidden-set key exists for the active printer, seed it to
  the currently-discovered `_`-prefixed macros. This exactly reproduces today's "system
  macros hidden by default" behavior — nobody sees a sudden flood of internal macros.
  After that, the stored set is the **sole source of truth** (in-set = hidden, absent =
  visible). This retires the old implicit system-macro filter (and the dead
  `system_toggle_` lookup / `show_system_macros_` path).
- **Save button:** appears in the header only in edit mode; persists the hidden set and
  exits edit mode.
- **Discard on exit:** pressing Back (or otherwise leaving) while in edit mode discards
  the pending changes — only Save persists. Matches the AMS edit overlay.
- **A new `_`-macro added in Klipper later** follows the plain rule (absent from the set
  → visible until the user hides it). Acceptable; retiring the implicit filter is the
  point.

## Architecture

### 1. Reusable pattern: `IndexedSubjectPool` (reclaim-on-close)

**Problem.** Declarative rows via reactive `<repeat count="a_subject">` require each row
to bind by *name* to per-index subjects (`bind_text="macro_name_${i}"`). The parser can
build and wire N rows, but the row **data** must come from C++-owned subjects named
`macro_name_0..N-1`. No existing dynamic pool in the codebase name-registers subjects —
the per-fan/per-sensor/per-extruder pools deliberately avoid the XML name registry and
expose C++ pointer getters instead, precisely because there was **no unregister API**.
A dynamically-sized, name-registered pool that reclaims when a panel closes is new
ground and is worth a reusable abstraction.

**Engine addition (`lib/helix-xml/` — our own submodule; edit in place, push from inside it, bump the pointer here).**
Add `lv_xml_unregister_subject(scope, name)` mirroring `lv_xml_get_subject`
(`lv_xml.c:637`). It walks `scope->subjects_ll`, `lv_ll_remove`s the matching
`lv_xml_subject_t` record, and frees **only** the strdup'd name string:

```c
// Non-owning removal: unlink the registry record + free its name string ONLY.
// The lv_subject_t storage is owned by the caller (the pool) and freed there.
// scope == NULL resolves to the "globals" scope, matching lv_xml_register_subject.
lv_result_t lv_xml_unregister_subject(lv_xml_component_scope_t * scope, const char * name);
```

Ownership contract (critical): the registry entry is **non-owning**. Unregister must not
`lv_subject_deinit` or `lv_free` the subject — that would double-free a pool-owned
subject. This matches how the never-torn-down globals scope already treats
app-registered member subjects as non-owning. Declare in `lv_xml.h` beside line 108.

**Why unregister is mandatory.** `lv_xml_get_subject` returns the stored pointer verbatim
with **no liveness check** (`lv_xml.c:637-658`). If pool storage is freed but the
registry entry lingers, the next XML build that references that name dereferences freed
memory in `lv_subject_add_observer_obj` → UAF. Unregistering on reclaim closes this.

**The helper (`include/helix/xml/indexed_subject_pool.h`, new).**
A grow-only pool of same-typed, name-registered subjects:

```cpp
// Names are "<prefix>_<i>", i in [0, size). Grow-only within its lifetime.
class IndexedSubjectPool {
public:
    enum class Type { Int, String };
    IndexedSubjectPool(std::string prefix, Type type, size_t string_cap = 64);
    ~IndexedSubjectPool();              // calls reclaim()

    void ensure_size(size_t n);         // grow: init + name-register new slots
    void set_int(size_t i, int v);
    void set_string(size_t i, const std::string& v);
    lv_subject_t* at(size_t i);
    size_t size() const;

    void reclaim();                     // unregister + deinit + free every slot (idempotent)
private:
    std::string prefix_;
    Type type_;
    std::vector<std::unique_ptr<lv_subject_t>> subjects_;  // unique_ptr = stable address
    std::vector<std::string> string_backing_;             // string subjects need a buffer
};
```

- **`unique_ptr` is mandatory.** A `std::vector<lv_subject_t>` realloc on growth would
  move the subject and dangle the pointer already handed to the XML registry. Each
  subject is individually heap-allocated (as `PrinterFanState` does,
  `printer_fan_state.cpp:442`), giving stable addresses.
- **`ensure_size` is grow-only.** It appends `n - size()` new subjects,
  `lv_subject_init_*`s each, and `lv_xml_register_subject(nullptr, "<prefix>_<i>", ptr)`.
  Re-registering an existing name is a safe in-place update, so growth never disturbs
  live rows.

**The `reclaim()` sequence — provably UAF-free, order-independent w.r.t. row teardown:**

```
for each slot i in [0, size):
    lv_xml_unregister_subject(nullptr, "<prefix>_<i>");   // 1. future builds can't resolve
    lv_subject_deinit(subjects_[i].get());                // 2. sever observers BOTH ways
    // 3. unique_ptr reset frees storage
subjects_.clear(); string_backing_.clear();
```

Step 2 is the key safety guarantee: `lv_subject_deinit` walks the subject's observer
list and, for each observer, both removes it from the subject **and** detaches the
observer's widget `LV_EVENT_DELETE` unsubscribe hook (`lv_observer.c:565-568`). So it is
safe to call even while bound rows are still alive or async-pending deletion — after
deinit there is no dangling hook for a later async row delete to fire. This is the same
guarantee `AmsState::BackendSlotSubjects::deinit()` (`ams_state.cpp:1114-1131`) and
`PrinterFanState` rely on. `reclaim()` runs on the **main thread** (panel teardown);
LVGL is not thread-safe.

**Not `StaticSubjectRegistry::register_deinit`.** That registry is append-until-shutdown
(`static_subject_registry.h`) — a single registration cannot be withdrawn per-panel. The
pool owns its full lifecycle instead, enabling true reclaim-on-close.

**Reusability.** Any feature needing a variable-length, declaratively-bound row list can
own one `IndexedSubjectPool` per bound field, `ensure_size` on populate, and `reclaim()`
on teardown. This spec's macro panel is the first consumer.

### 2. Declarative row rendering

**Reactive `<repeat>` teardown model (verified, `lv_xml.c`).** On a count-subject change
the repeat does a **full rebuild** (`xml_frag_rebuild`, no per-row diff): it synchronously
reparents the old rows off-tree to a condemned sink on `lv_layer_top()`, then
`lv_obj_delete_async`s the sink (drained by `lv_timer_handler`, **not** UpdateQueue). This
is the same algorithm as `helix::ui::safe_delete_subtree`. Consequences for us:

- The repeat must be the **last/only child of a dedicated container** (rebuilt items are
  appended). Wrap it in its own `lv_obj`.
- Row-local widget state is lost on every count change (irrelevant here — rows are
  stateless).
- Because we free pool subjects only via `reclaim()` (which `lv_subject_deinit`s), the
  async row teardown timing does not create a UAF regardless of ordering.

**Row template (in `macro_panel.xml`):**

```xml
<lv_obj name="rows_container">   <!-- dedicated container; repeat is the only child -->
  <repeat count="macro_row_count">
    <macro_card name_subject="macro_name_${i}"
                visible_subject="macro_visible_${i}"
                row_index="${i}"/>
  </repeat>
</lv_obj>
```

`${i}` splices into each attribute value (`resolve_params`, `lv_xml.c:952`), so each row
self-wires to its indexed subjects and carries its own row index.

**`macro_card.xml` refactor** — accept subject *names* and an index as props so the
component stays reusable and the repeat body is clean:

- `bind_text="$name_subject"` for the macro display name.
- A checkbox child: `<bind_state_if_eq subject="$visible_subject" state="checked" ref_value="1"/>`.
- `<event_cb trigger="clicked" callback="on_macro_row_clicked" user_data="$row_index"/>`.
- Checkbox shown / chevron hidden driven by a global `macro_edit_mode` int subject via
  `bind_flag_if_eq` — each flag on its own widget (checkbox vs chevron), so no
  multi-`bind_flag`-on-one-flag conflict (see memory
  `reference_bind_flag_multi_eq_conflict`).
- `<if>` cannot nest inside `<repeat>` (parser limitation), so edit-mode toggling uses a
  reactive `bind_flag`, not a structural `<if>`. Same visual result.

There is **no shared checkbox component** today. Add a minimal one: an `<icon>` toggled
between a checked-box and empty-box glyph via the `visible_subject` state binding
(reuse the existing curated `check` icon; add an outline-box glyph if needed and run the
icon-font sync workflow — codepoints.h + `make regen-fonts`).

### 3. C++ model & flow (`ui_panel_macros.cpp` / `.h`)

Replaces the current `create_macro_card` / `macro_entries_` / card-pointer identity code.

**State:**
```cpp
std::vector<std::string> all_macros_;        // sorted, INCLUDING _-prefixed
std::set<std::string>    pending_hidden_;     // working copy while editing
std::vector<std::string> displayed_;          // index -> macro name for current mode
bool edit_mode_ = false;
IndexedSubjectPool name_pool_{"macro_name", IndexedSubjectPool::Type::String};
IndexedSubjectPool visible_pool_{"macro_visible", IndexedSubjectPool::Type::Int};
// plus UI_MANAGED_SUBJECT_INT: macro_row_count, macro_edit_mode, macros_edit_save_hidden
```

**Populate (both modes):**
1. Compute `displayed_` — normal mode: macros not in the saved hidden set; edit mode:
   all macros.
2. `name_pool_.ensure_size(displayed_.size())`,
   `visible_pool_.ensure_size(displayed_.size())` (grow-only within the session; the
   high-water mark is the total macro count, reached the first time edit mode opens).
3. For each `i`: `name_pool_.set_string(i, display_name(displayed_[i]))`;
   `visible_pool_.set_int(i, edit_mode_ ? !pending_hidden_.count(displayed_[i]) : 1)`.
   Populate values **before** raising `macro_row_count` to avoid a first-frame flash.
4. `lv_subject_set_int(&macro_row_count, displayed_.size())`.

**Row click (`on_macro_row_clicked`):** recover index via
`atoi((const char*)lv_event_get_user_data(e))`, map `i -> displayed_[i]`.
- Edit mode: toggle membership in `pending_hidden_`, flip `visible_pool_.set_int(i, ...)`,
  enable Save.
- Normal mode: run the macro (existing behavior).

**Enter edit mode (long-press handler):** reuse the home dashboard's long-press
suppression guards (`should_suppress_edit_mode` / `finger_drifted_since_press` from
`ui_panel_home.cpp`) so a hold during a flick-scroll doesn't trip edit mode. Set
`pending_hidden_` = saved hidden set, `edit_mode_ = true`, `macro_edit_mode = 1`, reveal
Save, repopulate.

**Save:** write `pending_hidden_` via `SettingsManager::set_hidden_macros`, `edit_mode_ =
false`, `macro_edit_mode = 0`, hide Save, repopulate (normal mode).

**Back / discard:** drop `pending_hidden_`, exit edit mode, repopulate.

**Teardown (`on_ui_destroyed`):** `name_pool_.reclaim(); visible_pool_.reclaim();` (after
nulling cached widget pointers, matching the existing deferred-rebuild pattern). Reopen
rebuilds the pools sized to the live macro count — true reclaim-on-close.

### 4. Persistence (`SettingsManager`)

Mirror the `console_filter_user_add` vector-of-strings pattern
(`settings_manager.cpp:538-570`), keyed **per-printer**:

```cpp
std::vector<std::string> get_hidden_macros() const;               // key: df() + "macros/hidden"
void set_hidden_macros(const std::vector<std::string>& names);    // set + config->save()
```

`config->df()` returns the active printer's prefix (already ends in `/`). Malformed JSON
→ warn and return empty. First-run seed (§ behavior) is computed in the panel when the
key is absent, then written on first Save.

### 5. Header Save button

Copy the AMS edit overlay pattern (`ui_ams_edit_overlay.cpp` / `ams_edit_overlay.xml`):
register `UI_MANAGED_SUBJECT_INT macros_edit_save_hidden` (init `1` = hidden). On
`macro_panel`'s `<view extends="overlay_panel">` opening tag add:
`hide_action_button="false"`, `action_button_text="Save"`,
`action_button_callback="on_macros_edit_save"`,
`action_button_hidden_subject="macros_edit_save_hidden"`. Optionally gate
`action_button_disabled_subject` on "any change pending". Set the hidden subject to `0`
on entering edit mode, `1` on save/exit.

## Lifetime safety analysis (the crux)

1. **XML bindings carry no `SubjectLifetime` token** — the token skip
   (`ObserverGuard::reset`) protects only C++-registered observers. So the invariant for
   XML-bound subjects is: the subject must be valid (or already `lv_subject_deinit`'d)
   whenever a bound row is deleted.
2. **Within a panel session the pool is grow-only** — `ensure_size` never frees. Every
   count change (normal↔edit) rebinds rows to still-alive subjects. No mid-session free
   → the entire UAF class is avoided by construction.
3. **At close, `reclaim()` is order-independent** — `lv_subject_deinit` severs observers
   bidirectionally, so even async-pending row deletions find nothing dangling.
   `lv_xml_unregister_subject` removes the registry entry so no future build can resolve
   a freed name.
4. **Main-thread only** — populate and reclaim run on the main thread. If a background
   thread could ever re-drive `macro_row_count` mid-teardown (it cannot here — count is
   set only from main-thread UI flow), wrap in `UpdateQueue::scoped_freeze`.

## Testing

Real tests (must fail if the feature is removed; mutation-verify each):

- **Visibility filter** (pure, unit): in-set hidden, absent visible; first-run seed hides
  `_`-prefixed macros. Assert against the value the function actually holds at the call
  site.
- **Toggle mutation:** clicking a row in edit mode flips `pending_hidden_` membership and
  the `visible_pool_` int.
- **Discard on back:** enter edit, toggle, exit via back → saved set unchanged.
- **Save round-trip:** Save writes through `SettingsManager` and reloads identically
  (per-printer key). Drain the UpdateQueue before assertions
  (`drain_queue_for_testing`, see [L048]).
- **`IndexedSubjectPool`** (unit, its own file): `ensure_size` registers resolvable names
  (`lv_xml_get_subject` finds them); grow preserves earlier slots; `reclaim()` leaves
  `lv_xml_get_subject` returning NULL for every name and is idempotent /
  double-reclaim-safe. Run under the native Valgrind harness to prove no leak / no UAF.
- **`lv_xml_unregister_subject`** (engine unit): register → get non-NULL → unregister →
  get NULL; unregister of an absent name returns invalid without touching storage;
  unregister does not free the subject (caller still owns it).
- **XML lint / schema:** after adding the `macro_card` props and any new widget, run
  `make regen-xml-schema` and commit the schema ([L089]); register any new XML component
  in `main.cpp` ([L014]).

## Files touched

- `lib/helix-xml/src/xml/lv_xml.c`, `lv_xml.h` — add `lv_xml_unregister_subject`.
- `include/helix/xml/indexed_subject_pool.h` (+ `src/` impl if not header-only) — new helper.
- `ui_xml/macro_panel.xml` — Save button props, `rows_container` + `<repeat>`.
- `ui_xml/macro_card.xml` — subject-name props, checkbox child, edit-mode bindings.
- New checkbox glyph/icon if needed — `include/ui_icon_codepoints.h` + `make regen-fonts`.
- `src/ui/ui_panel_macros.cpp`, `include/ui_panel_macros.h` — model, modes, pools,
  handlers; remove `create_macro_card`/`macro_entries_`/card-pointer identity and the
  dead `system_toggle_` / `show_system_macros_` path.
- `src/system/settings_manager.cpp`, `include/settings_manager.h` — `get/set_hidden_macros`.
- Tests as above; `xml-linter/schema/schema.json` regen.

## Risks / notes

- **First production user of reactive `<repeat count="subject">`** — only `test_panel.xml`
  exercises it today. Extra care in review + Valgrind.
- **Icon font sync** — a new checkbox glyph requires the full codepoints + `make
  regen-fonts` + rebuild cycle ([L009]); forgetting it renders a blank box.
- **`macro_card` is `extends="lv_button"`** — long_pressed is supported natively; ensure
  child widgets that must not absorb the row click set `clickable="false"
  event_bubble="true"` ([L071]).
- **Classification: MAJOR** — new feature, 8+ files, touches the XML engine and a
  reusable pattern. Worktree + test-first + review per project protocol.
