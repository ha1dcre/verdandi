-- device slots on macOS: a real grid/arc takes the slot a closed virtual one still holds
-- (core/vport.lua promote_hardware), and quitting the app saves the state (_norns.quit in
-- core/norns.lua, called by matron's w_quit after the event loop).
-- run from the repo root:
--   <lua 5.3> macos/tests/device_slots_test.lua

package.path = "lua/?.lua;lua/core/?.lua;lua/lib/?.lua;" .. package.path

local tmp = os.tmpname()
os.remove(tmp)
os.execute("mkdir -p " .. tmp)

-- stubs for what grid.lua, arc.lua and state.lua touch
tab = require 'tabutil'
inf = math.huge -- system.state writes -inf mix levels; norns has a global inf
util = require 'util'
_path = {code = tmp .. "/code/", data = tmp .. "/", dust = tmp .. "/"}
_norns = {
  grid_rows = function() return 8 end,
  grid_cols = function() return 16 end,
}
local devlist = {}
midi = {update_devices = function() end, vports = {}}
hid = {update_devices = function() end, vports = {}}
for i = 1, 16 do midi.vports[i] = {name = "none"} end
for i = 1, 4 do hid.vports[i] = {name = "none"} end
norns = {script = {clear = function() end, load = function() end}, scripterror = function() end}

grid = require 'core/grid'
arc = require 'core/arc'
norns.state = require 'core/state'
local state = norns.state
-- save_state writes the clock table: give it what core/clock would
state.clock = state.clock or {}
for _, k in ipairs({"source", "tempo", "link_quantum", "link_start_stop_sync", "midi_in",
    "crow_out", "crow_out_div", "crow_in_div"}) do
  state.clock[k] = state.clock[k] or 1
end
state.clock.midi_out = state.clock.midi_out or {}
for i = 1, 16 do state.clock.midi_out[i] = state.clock.midi_out[i] or 0 end

local failures = 0
local function check(what, got, want)
  if got ~= want then
    failures = failures + 1
    print("FAIL " .. what .. ": got '" .. tostring(got) .. "', want '" .. tostring(want) .. "'")
  else
    print("ok   " .. what .. ": " .. tostring(got))
  end
end

local next_id = 1
local function plug(kind, name, serial)
  local id = next_id
  next_id = next_id + 1
  devlist[id] = kind
  if kind == "arc" then _norns.arc.add(id, serial, name, {}) else _norns.grid.add(id, serial, name, {}) end
  return id
end
local function unplug(id)
  if devlist[id] == "arc" then _norns.arc.remove(id) else _norns.grid.remove(id) end
  devlist[id] = nil
end
local function reset(saved)
  for id, _ in pairs(devlist) do unplug(id) end
  for i = 1, 4 do
    grid.vports[i].name = "none"
    arc.vports[i].name = "none"
  end
  local f = io.open(_path.data .. "system.state", "w")
  f:write("-- norns system state\n")
  for k, v in pairs(saved) do f:write(k .. " = '" .. v .. "'\n") end
  f:close()
  state.resumed = false
end
local function slots(vports)
  local t = {}
  for i = 1, 4 do t[i] = vports[i].name end
  return table.concat(t, " | ")
end
local function port_of(vports, name)
  for i = 1, 4 do
    if vports[i].device and vports[i].device.name == name then return i end
  end
  return nil
end

local HW = "monome one"
local HWS = "m07548291"
local VG = "virtual grid"
local VGS = "m0000001"

print("-- 1. the tester: slot 1 'virtual grid' saved, grid already in slot 2, plugged at startup")
reset({["grid.vports[1].name"] = "virtual grid m0000001", ["grid.vports[2].name"] = "monome one m07548291"})
plug("grid", HW, HWS)          -- dev_monitor_scan, before state.resume
state.resume()
check("slots", slots(grid.vports), "monome one m07548291 | virtual grid m0000001 | none | none")
check("hardware on port", port_of(grid.vports, "monome one m07548291"), 1)
local vg = plug("grid", VG, VGS)  -- the virtual grid window opened later
check("virtual opened: port", port_of(grid.vports, "virtual grid m0000001"), 2)
check("hardware stays on", port_of(grid.vports, "monome one m07548291"), 1)
unplug(vg)

print("-- 2. slot 1 'virtual grid' saved, slot 2 none, grid plugged in after startup (tester's log)")
reset({["grid.vports[1].name"] = "virtual grid m0000001"})
state.resume()
plug("grid", HW, HWS)
check("slots", slots(grid.vports), "monome one m07548291 | virtual grid m0000001 | none | none")
check("hardware on port", port_of(grid.vports, "monome one m07548291"), 1)
plug("grid", VG, VGS)
check("virtual opened: port", port_of(grid.vports, "virtual grid m0000001"), 2)

print("-- 3. virtual grid open: it keeps slot 1, hardware goes to slot 2 (norns as before)")
reset({["grid.vports[1].name"] = "virtual grid m0000001"})
state.resume()
plug("grid", VG, VGS)
plug("grid", HW, HWS)
check("slots", slots(grid.vports), "virtual grid m0000001 | monome one m07548291 | none | none")

print("-- 4. two real grids chosen explicitly, one unplugged: nothing moves")
reset({["grid.vports[1].name"] = "monome 128 m1000001", ["grid.vports[2].name"] = "monome one m07548291"})
state.resume()
plug("grid", HW, HWS)
check("slots", slots(grid.vports), "monome 128 m1000001 | monome one m07548291 | none | none")
check("hardware on port", port_of(grid.vports, "monome one m07548291"), 2)

print("-- 5. closed virtual in slot 1, two real grids in 2 and 3: they move up in order")
reset({["grid.vports[1].name"] = "virtual grid m0000001", ["grid.vports[2].name"] = "monome 128 m1000001",
  ["grid.vports[3].name"] = "monome one m07548291"})
plug("grid", HW, HWS)
plug("grid", "monome 128", "m1000001")
state.resume()
check("slots", slots(grid.vports), "monome 128 m1000001 | monome one m07548291 | virtual grid m0000001 | none")

print("-- 6. arc: slot 1 'monome arc virtual' saved, a real arc plugged in")
reset({["arc.vports[1].name"] = "monome arc virtual m0000002"})
state.resume()
plug("arc", "monome arc 4", "m0000123")
check("arc slots", slots(arc.vports), "monome arc 4 m0000123 | monome arc virtual m0000002 | none | none")
plug("arc", "monome arc virtual", "m0000002")
check("virtual arc opened: port", port_of(arc.vports, "monome arc virtual m0000002"), 2)

print("-- 7. quit saves the slots (_norns.quit from core/norns.lua)")
-- take the function as written in norns.lua and run it on the stubs
local src = io.open("lua/core/norns.lua"):read("a")
local body = src:match("\n(_norns%.quit = function%(%).-\nend)\n")
assert(body, "_norns.quit not found in lua/core/norns.lua")
hook = {system_pre_shutdown = function() end}
local cleaned = false
cleanup = function() cleaned = true end
-- quitting before state.resume saves nothing
reset({["grid.vports[1].name"] = "virtual grid m0000001"})
assert(load(body))()
local before = io.open(_path.data .. "system.state"):read("a")
_norns.quit()
check("quit during startup leaves the file alone", io.open(_path.data .. "system.state"):read("a"), before)
check("quit during startup: no cleanup", cleaned, false)
-- after startup: the user picks the grid for slot 1 in SYSTEM > DEVICES, then quits
state.resume()
plug("grid", "monome 128", "m1000001")
grid.vports[1].name = "monome 128 m1000001"   -- menu/devices.lua K3
grid.update_devices()
state.script = tmp .. "/code/awake/awake.lua"
_norns.quit()
check("script cleanup ran", cleaned, true)
local saved = io.open(_path.data .. "system.state"):read("a")
check("saved slot 1", saved:match("grid%.vports%[1%]%.name = '([^']*)'"), "monome 128 m1000001")
check("saved clean_shutdown", saved:match("norns%.state%.clean_shutdown = (%a+)"), "true")
check("saved script", saved:match("norns%.state%.script = '([^']*)'"), tmp .. "/code/awake/awake.lua")
-- the next launch reads it back
for i = 1, 4 do grid.vports[i].name = "none" end
dofile(_path.data .. "system.state")
check("next launch: slot 1", grid.vports[1].name, "monome 128 m1000001")

os.execute("rm -rf " .. tmp)
print(failures == 0 and "device_slots_test: 0 failures" or ("device_slots_test: " .. failures .. " failures"))
os.exit(failures == 0 and 0 or 1)
