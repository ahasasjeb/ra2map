# Lua Map API

The editor embeds Lua 5.5 and exposes the currently open map through a
sandboxed, transactional API. A script runs against temporary INI and staged
terrain state. If it succeeds, the editor displays a change summary and asks
before applying anything. A syntax error, runtime error, instruction-limit
error, or rejected confirmation discards the staged changes.

Press `F5` or `Ctrl+Enter` to save and run the current script. Press `Ctrl+S`
to save without running it.

## Quick examples

### Create a complete trigger

```lua
local created = map.triggers.create {
    name = "Lua attack",
    house = "Americans",
    events = {
        { 13, 0, 10 }, -- elapsed time
    },
    actions = {
        { 4, 0, 0, 0, 0, 0, 0, "A" },
    },
    create_tag = true,
}

print("Trigger:", created.id, "Tag:", created.tag_id)
```

Event and action entries may be field arrays or raw comma-separated records.
The library writes `Triggers`, `Events`, `Actions`, and optionally `Tags` as
one transaction and allocates IDs that do not collide with other mission
objects.

### Create and edit map objects

```lua
local id = map.objects.units.create {
    house = "Americans",
    type = "MTNK",
    x = 42,
    y = 36,
    direction = 96,
}

map.objects.units.update(id, { strength = 128 })

for _, unit in ipairs(map.objects.units.list()) do
    print(unit.id, unit.house, unit.type, unit.x, unit.y)
end
```

### Edit a tile and overlay

```lua
local cell = map.cells.get(40, 40)
map.cells.set(40, 40, {
    tile = cell.tile,
    subtile = cell.subtile,
    height = math.min(cell.height + 1, 14),
})

map.overlay.set(40, 40, 0x66, 0)
map.terrain.smooth(40, 40)
```

### Read game definitions

```lua
local strength = game.rules.get("MTNK", "Strength", "unknown")
local image = game.art.get("MTNK", "Image", "MTNK")
local display_name = game.csf.get("Name:MTNK", "MTNK")
print(strength, image, display_name)
```

## Runtime

- `map.api_version` is `2`.
- Available standard facilities: Lua base functions, `table`, `string`,
  `math`, and `utf8`.
- `print(...)` appends tab-separated values to the report.
- `require("name")` loads `Scripts/lib/name.lua`. Dotted names map to
  subdirectories, for example `require("mission.ids")` loads
  `Scripts/lib/mission/ids.lua`.
- Arbitrary filesystem, process, network, native-module, dynamic-code,
  coroutine, and debug access remain unavailable. In particular `os`, `io`,
  `package`, `dofile`, `loadfile`, `load`, `coroutine`, and `debug` are absent.
- Each run is limited to 64 MiB of Lua memory and 20 million VM instructions.
- `editor.capabilities` contains the modules supported by the running editor
  variant. `editor.has_capability(name)` tests one capability.

## Raw INI API

The raw API is the compatibility escape hatch for every ordinary map INI
section. The same functions are available under `map.ini`.

| API | Description |
| --- | --- |
| `map.get(section, key[, default])` | Read a value, the default, or `nil`. |
| `map.has(section, key)` | Test whether a key exists. |
| `map.set(section, key, value)` | Stage a string, number, or Boolean value. |
| `map.remove(section, key)` | Remove a key and return whether it existed. |
| `map.sections()` | Return sorted section names. |
| `map.keys(section)` | Return sorted keys. |
| `map.section(section)` | Return a key/value table. |
| `map.replace_section(section, values)` | Replace all keys in a section. |
| `map.clear_section(section)` | Clear a section. |
| `map.remove_section(section)` | Remove a section. |

Section names, keys, and values must be single-line text. Names are limited to
255 bytes and values to 1 MiB. Packed sections, `Terrain`, `Smudge`, and the
`[Map]` geometry/theater keys are protected; use the corresponding high-level
API instead. `PreviewPack` is derived when the map is saved and is not a
script-editable data source.

## Metadata and utilities

`map.info` is a read-only snapshot containing `width`, `height`, `iso_size`,
`theater`, `multiplayer`, `waypoint_count`, `unit_count`, `infantry_count`,
`structure_count`, `aircraft_count`, `terrain_count`, `player_count`,
`house_count`, and `country_count`.

| API | Description |
| --- | --- |
| `map.ids.free()` | Allocate a globally unique eight-character mission ID. |
| `map.ids.free_numeric(section)` | Allocate an unused numeric key. |
| `map.csv.split(text)` | Split a legacy comma-separated record. |
| `map.csv.join(values)` | Join a field array. |
| `map.position.encode(x, y)` | Encode a map-position key. |
| `map.position.decode(value)` | Decode a position into `x, y`. |
| `map.metrics.money()` | Calculate the current overlay resource value. |
| `map.metrics.power(house)` | Calculate power for a house. |

`map.records.get/list/create/update/delete` provides generic record operations
for any ordinary INI section.

## Triggers, tags, and AI

### `map.triggers`

| API | Description |
| --- | --- |
| `create(options)` | Create Trigger/Event/Action records and an optional Tag. |
| `get(id)` | Return the trigger, raw Events/Actions, and referencing Tags. |
| `list()` | Return all triggers. |
| `update(id, changes)` | Update common fields and optionally Events/Actions. |
| `set_events(id, events)` | Replace the event list. |
| `set_actions(id, actions)` | Replace the action list. |
| `clone(id[, options])` | Clone a trigger and normally create a new Tag. |
| `delete(id[, options])` | Remove the complete graph; Tags are removed unless `delete_tags=false`. |

`create` accepts `id`, `raw`/`record`, `fields`, `house`, `attached_trigger`,
`name`, difficulty flags, `events`, `actions`, `create_tag`, and `tag`.

### Other mission graph modules

- `map.tags.get/list/create/update/delete`
- `map.ai_triggers.get/list/create/update/delete`
- `map.ai_trigger_enabling.get/list/set/delete`
- `map.task_forces.get/list/create/update/delete`
- `map.script_types.get/list/create/update/delete`
- `map.team_types.get/list/create/update/delete`

Task forces, script types, and team types manage both their numeric registry
entry and globally named child section. The generic forms are
`map.registries.list/create/delete`.

## Units and structures

The following collections support `get(id)`, `list()`, `create(values)`,
`update(id, changes)`, `move(id, x, y)`, and `delete(id)`:

- `map.objects.units` / `map.objects.vehicles`
- `map.objects.infantry`
- `map.objects.aircraft`
- `map.objects.structures`

Every returned object contains `id`, `raw`, `fields`, and named properties.
The named schemas are:

- Unit: `house`, `type`, `strength`, `y`, `x`, `direction`, `action`, `tag`,
  `flag1` through `flag6`.
- Infantry: `house`, `type`, `strength`, `y`, `x`, `subcell`, `action`,
  `direction`, `tag`, and `flag1` through `flag5`.
- Aircraft: `house`, `type`, `strength`, `y`, `x`, `direction`, `action`,
  `tag`, and `flag1` through `flag4`.
- Structure: `house`, `type`, `strength`, `y`, `x`, `direction`, `tag`, two
  flags, `energy`, `upgrade_count`, `spotlight`, three upgrades, and two final
  flags.

Supplying `raw` or `record` preserves compatibility with mod-specific extended
records. Named creation supplies editor-compatible defaults but scripts should
still validate type IDs through `game.rules` for the target mod.

## Houses, variables, waypoints, and map references

- `map.houses.get/list/create/update/delete`
- `map.countries.get/list/create/update/delete`
- `map.local_variables.get/list/create/update/delete`
- `map.waypoints.get/list/set/delete`
- `map.cell_tags.get/list/set/delete`
- `map.nodes.create/delete`
- `map.tubes.get/list/create/update/delete`

`map.basic`, `map.map_settings`, `map.lighting`, and `map.special_flags` are
section wrappers with `get`, `has`, `set`, `remove`, `values`, `patch`, and
`replace`.

## Cells, overlays, and terrain

### `map.cells`

`map.cells.get(x, y)` returns `tile`, `subtile`, `height`, `map_data`,
`map_data2`, and `random_image`. `map.cells.set(x, y, changes)` stages any
combination of those fields and validates tile/subtile ranges and the maximum
height.

### `map.overlay`

- `get(x, y)` returns `{ type, data }`.
- `set(x, y, type[, data])` stages an overlay cell.
- `clear()` clears both packed overlay planes before later staged writes.

### `map.terrain` and `map.smudges`

- `list()` returns typed objects with index and coordinates.
- `create { type=..., x=..., y=... }` stages an object and returns its index.
- `get(index)`, `update(index, changes)`, and `move(index, x, y)` edit it.
- `delete(index)` stages deletion.
- `map.terrain.smooth(x, y)` performs LAT/shore smoothing on apply.
- `map.terrain.smooth_tiberium(x, y)` smooths resource overlay.
- `map.terrain.create_shore(left, top, right, bottom[, remove_useless])`
- `map.terrain.auto_level()`

Smudges are available only in editor variants compiled with smudge support;
check `editor.has_capability("smudges")`.

### Resize

`map.resize(left, top, width, height)` stages the editor's specialized resize
operation. Width and height must be from 1 through 200. Resize cannot be mixed
with cell, overlay, terrain-object, or terrain-smoothing operations in the same
run because those coordinates would refer to two different map buffers. It may
be combined with ordinary INI changes.

### Theater

`map.set_theater(theater)` validates and stages one of the theater IDs supported
by the running editor variant. It cannot be mixed with resize, cell, overlay,
terrain-object, or terrain-smoothing operations. The map must be reopened after
apply so the editor can reload theater graphics; the saved map data already
contains the new theater value.

## Game databases

| API | Description |
| --- | --- |
| `game.get(source, section, key[, default])` | Read a value. |
| `game.has(source, section, key)` | Test a value. |
| `game.sections(source)` | Enumerate sections. |
| `game.keys(source, section)` | Enumerate keys. |
| `game.section(source, section)` | Read a complete section. |
| `game.rules.get/keys/section` | Rules convenience wrapper. |
| `game.art.get/keys/section` | Art convenience wrapper. |
| `game.csf.get(id[, default])` | Resolve a CSF string. |

Sources are `rules`, `art`, `ai`, `sound`, `tutorial`, `eva`, `theme`, `data`,
`language`, and the current theater's `tiles` database. These APIs are
read-only; map-local rule overrides remain editable through `map.ini`.

## User interaction and editor view

| API | Description |
| --- | --- |
| `ui.message(text[, title])` | Show an information message. |
| `ui.confirm(text[, title])` | Ask Yes/No and return a Boolean. |
| `ui.input(prompt[, title])` | Return entered text or an empty string. |
| `ui.input_integer(prompt[, min, max, title])` | Repeatedly request an integer or return `nil` on cancel. |
| `ui.select(options[, prompt, title])` | Show a list and return the selection. |
| `ui.select_house/country/trigger/tag(...)` | Select a map reference by ID. |
| `editor.focus(x, y)` | Focus a map coordinate. |
| `editor.focus_waypoint(id)` | Focus a waypoint. |
| `editor.redraw()` | Redraw the map view immediately. |

UI and view calls happen immediately and are not map mutations. Map mutations
remain staged until the final confirmation.

`map.require_multiplayer()` and `map.require_singleplayer()` stop a script on
the wrong map type.

## Transaction notes

- Raw INI changes and staged specialized changes are applied together only
  after successful execution and confirmation.
- The confirmation dialog reports changed INI sections, mutation and cell
  counts, terrain operations, and resize parameters.
- `map.info` and live metrics describe the map state at the start of the run;
  they are not automatically recomputed after staged changes.
- Terrain-cell changes enter the normal field-data undo history. Ordinary INI
  entity history is still governed by the editor's existing dialog behavior;
  save a map before running destructive scripts.
- A script can use raw INI records for mod-specific data not yet represented by
  a named schema, without giving up the transaction boundary.

## Legacy `.fscript`

Legacy scripts remain supported. Lua replaces legacy flow, variables, string,
math, input, object, trigger, waypoint, resize, and INI commands with normal
Lua syntax and the modules above. New scripts should use `.lua`.
