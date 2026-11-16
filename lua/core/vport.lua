--
local vport = {}

function vport.wrap_method(method)
  return function(self, ...)
    if self.device then self.device[method](self.device, ...) end
  end
end

function vport.get_unique_device_name(name, devices)
  local names = {}

  for _, device in pairs(devices) do
    names[device.name] = true
  end

  local result = name
  local suffix = 2

  while names[result] ~= nil do
    result = name.." "..suffix
    suffix = suffix + 1
  end

  return result
end

-- macOS (Verdandi): the virtual grid and arc are windows, not hardware. their names stay
-- saved in a slot after the window is closed, which pushed a real grid or arc into a later
-- slot, and most scripts only use slot 1. so: a slot holding a virtual device that is not
-- connected goes to the first connected real device in a later slot (or in none); the virtual
-- name moves to the slot the real one had (or the first free one), so the window, opened
-- again, still finds a slot. real devices keep their order among themselves.
-- names as matron's sdl_bridge.cc plugs them in, plus the serial (Grid.new / Arc.new).
local virtual_prefixes = { "virtual grid ", "monome arc virtual " }

function vport.is_virtual_name(name)
  for _, prefix in ipairs(virtual_prefixes) do
    if string.sub(name, 1, #prefix) == prefix then return true end
  end
  return false
end

function vport.promote_hardware(vports, devices)
  local connected = {}
  for _, device in pairs(devices) do
    connected[device.name] = true
  end
  local function slot_of(name)
    for i = 1, #vports do
      if vports[i].name == name then return i end
    end
    return nil
  end
  for i = 1, #vports do
    local held = vports[i].name
    if vport.is_virtual_name(held) and not connected[held] then
      -- the real device in the earliest later slot; one in no slot comes after those
      local best, best_slot
      for _, device in pairs(devices) do
        if not vport.is_virtual_name(device.name) then
          local j = slot_of(device.name)
          if j == nil or j > i then
            local key = j or (#vports + 1)
            if best == nil or key < best_slot
              or (key == best_slot and device.name < best.name) then
              best, best_slot = device, key
            end
          end
        end
      end
      if best then
        local j = slot_of(best.name)
        vports[i].name = best.name
        if j then
          vports[j].name = held
        else
          local free = slot_of("none")
          if free then vports[free].name = held end
        end
      end
    end
  end
end

return vport
