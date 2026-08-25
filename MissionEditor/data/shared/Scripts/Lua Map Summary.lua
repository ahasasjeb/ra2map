-- Lua 5.5 map script example
-- This script only reads the current map and writes to the report.

local name = map.get("Basic", "Name", "Unnamed map")
print("Map:", name)
print("Theater:", map.info.theater)
print("Size:", map.info.width, map.info.height)
print("Multiplayer:", map.info.multiplayer)
print("Waypoints:", map.info.waypoint_count)
print("Units:", map.info.unit_count)
print("Infantry:", map.info.infantry_count)
print("Structures:", map.info.structure_count)
print("Aircraft:", map.info.aircraft_count)
print("Terrain objects:", map.info.terrain_count)
print("INI sections:", #map.sections())
