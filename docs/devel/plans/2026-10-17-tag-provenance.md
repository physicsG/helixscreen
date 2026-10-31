# Tag provenance

Backend prerequisite for the Edit Spool redesign (design items 9 and 13): every
tag-reading backend says, per field, whether the value a slot shows came from the
spool's tag, and keeps the tag's own values so "Reset to tag" can restore them. Deleted
when the Edit Spool UI ships; the durable text goes to `FILAMENT_SLOT_METADATA.md` and
each backend leaf doc.

## Interface

`include/ams_tag_reading.h` (pure, no backend named):

```cpp
enum class TagField : std::uint8_t { Material, Brand, Product, Color, NozzleTemp, BedTemp, Weight };
struct TagReading {                 // every field optional: unset = the tag did not state it
    std::optional<std::string> material, brand, product_name;
    std::optional<uint32_t> color_rgb;
    std::string multi_color_hexes;  // SlotInfo form, empty for one colour
    std::optional<int> nozzle_temp_min, nozzle_temp_max, bed_temp;
    std::optional<float> total_weight_g;
    bool states(TagField) const; bool empty() const;
};
class TagProvenance { bool from_tag(TagField) const; bool any() const; void set(TagField); };

TagReading tag_reading_from(const Observation& vendor_cache);
TagProvenance tag_provenance(const TagReading& tag, const SlotInfo& shown);
SlotInfo reset_to_tag(const SlotInfo& slot, const TagReading& tag);
std::optional<TagReading> fold_tag_frame(const std::optional<TagReading>& kept,
                                         const TagReading& raw, const TagReading& filed);
```

`AmsBackend` (generic code calls only these):

```cpp
virtual std::optional<helix::ams::TagReading> tag_reading(int slot_index) const; // default nullopt
helix::ams::TagProvenance tag_provenance(int slot_index) const;  // non-virtual
```

Provenance is a value comparison of `get_slot_info()` against the reading: a field is
from the tag when the tag states it and the slot shows that value. A user override that
differs reads as not-from-tag, and `reset_to_tag()` is the edit that puts the tag back.

## Per backend

| Backend | Reading | Kept how | Override survives the next poll |
|---|---|---|---|
| Snapmaker U1 | `filament_detect.info` with a real `CARD_UID`: VendorCache fields + temps + weight + `RGB_1..5`/`COLOR_NUMS` multi-colour | the firmware keeps the tag's entry (edits go to `print_task_config` only) | lane resolve: LocalUser outranks VendorCache; multi-colour written to SlotInfo only while the lane declares no colour |
| CFS | the box's latched record (stock `material_type`/`color_value` code, flat `material`/`brand`/`name`/`color`) + catalog temps | `fold_tag_frame` over raw vs echo-withheld, since edits overwrite the box record | lane resolve + own-write echo guard |
| QIDI Box | the saved RFID ids resolved through the Box tables (material, brand, colour) + profile temps | `fold_tag_frame`, same reason | lane resolve + echo guard |
| ACE | a frame whose `rfid` says identified (`true` / `2`): material, colour, `temp_min`/`temp_max` | per-bay kept reading; dropped when the bay reads no tag or empties | lane resolve |
| AFC | the inline scanner: `scanned_color_rgb` | SlotInfo already keeps it apart from `color_rgb` | the scan never writes `color_rgb` |

Known limits, stated in the leaf docs: a box record the user's write replaced is the
only copy, so after a restart CFS and QIDI report what the box holds (the user's values)
as the reading; and CFS/QIDI cannot tell a tag read from a value typed on the stock
screen, which writes the same record.

## Tests

- `[ams][tag_reading]` (pure): `tag_provenance` per field incl. case-insensitive text,
  colour vs multi-colour, half-stated nozzle pair; `reset_to_tag` restores only stated
  fields, clears `color_name` and `catalog_id` when identity moves; `fold_tag_frame`
  keeps withheld fields, drops unstated ones, returns nullopt when empty.
- Per backend, tagged with its own tag plus `[tag_reading]`: a tag frame gives a reading
  and full provenance; a user edit of colour survives the next tag frame, the reading
  still holds the tag's colour, provenance clears Color only; `reset_to_tag` gives back
  the tag's values. Snapmaker: a card-less filled entry gives no reading; multi-colour
  parses. ACE: `rfid` false/0 gives none. CFS/QIDI: the reading survives our write-back
  echo. AFC: scanner colour is the reading.
