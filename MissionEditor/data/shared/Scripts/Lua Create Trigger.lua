-- Creates a complete trigger graph without manually allocating IDs.
-- Review the event/action field values for the target game or mod.

local created = map.triggers.create {
    name = "Lua timed message",
    house = "Neutral",
    events = {
        { 13, 0, 10 },
    },
    actions = {
        { 11, 0, 0, 0, 0, 0, 0, "Lua trigger executed" },
    },
    create_tag = true,
}

print("Created trigger", created.id)
print("Created tag", created.tag_id)
