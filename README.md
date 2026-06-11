# hyprsplit
awesome / dwm like workspaces for [hyprland](https://github.com/hyprwm/hyprland) implemented as a lua library to be used from your hyprland config

> [!IMPORTANT]
> The c++ plugin is deprecated and will be replaced by the lua version.
> 
> The  original readme for the plugin is [here](https://github.com/shezdy/hyprsplit/blob/legacy/README.md)

## Installation
Requires Hyprland version >=`v0.55.0`

### Manual
Clone the repo somewhere, the easiest spot might be in your `.config/hypr` directory.

```lua
local hs = require("hyprsplit") -- you might have to change this depending on where "hyprsplit/init.lua" is located
```

### Home-manager

The following snippet of code tries to show how to bring the hyprsplit flake from the flake input.

```nix
inputs = {
  nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  hyprsplit.url = "github:shezdy/hyprsplit";
  ;
}
```

Then, use it in hyprland.

```nix
wayland.windowManager.hyprland = {
  extraLuaFiles = {
    # create a symlink to `.config/hypr/hyprsplit/init.lua`.
    "hyprsplit/init" = {
      autoLoad = false;
      content = builtins.readFile "${hyprsplit.hyprsplitlua}/share/hyprsplit/init.lua";
    };
    # Finally, use it directly in Lua.
    "hyprload" = {
      autoLoad = true;
      content = ''
          local hs = require("hyprsplit")
          -- your code
        '';
    };
  };
};
```

## Usage
> [!NOTE]
> The lua library is new, you might run into bugs.

The library is structured similarly to the hyprland config library `hl`

Set config options using `hs.config()`

Get a config option using `hs.get_config()`

### Options

| name | description | type | default |
|---|---|---|---|
| num_workspaces | Number of workspaces on each monitor | int | 10 |
| persistent_workspaces | if true, will make workspaces on each monitor persistent (they will always exist and will not be destroyed when empty) | bool | false |
| force_monitor_priority | if true, will auto assign workspaces using monitor names in alphabetical order, even when there are no monitor_priorities defined in the config. if false, will automatically assign workspaces based on monitor id as long as no `monitor_priority keywords are used in the config | bool | false |

### Dispatchers
hs.dsp contains:
| Dispatcher | Description |
| ---------- | ----------- |
| `focus({ workspace })` | Replacement for `hl.dsp.focus({ workspace })`. focus a workspace on the current monitor |
| `window.move({ workspace, follow? })` | Replacement for `hl.dsp.window.move({ workspace, follow? })`. move a window to a workspace on the current monitor |
| `workspace.swap_monitors({ monitor1, monitor2 })` | Swaps all windows in active workspaces between two monitors. Does not preserve layout, just moves the windows |
| `grab_rogue_windows()` | Finds all windows that are in invalid workspaces and moves them to the current workspace. Useful when unplugging monitors. |

Some of Hyprland's workspace parameters are treated differently by the plugin's dispatchers:
-  `1`,`2`, or `3`: number on current monitor
-  `+1` or `-1`: relative on current monitor, no looping
-  `r+1` or `r-1`: relative on current monitor, with looping
-  `e+1`, `e-1`: relative on current monitor, excluding empty workspaces, no looping
-  `m+1`, `m-1`: relative on current monitor, excluding empty workspaces, with looping
- `empty`: empty workspace on current monitor

All other workspace params will be treated the same as however Hyprland normally treats them.

### How workspaces are assigned to monitors
By default with no config, monitor ids will be used to determine the workspaces on each monitor.

`hs.monitor_priority()` can be called to reserve workspaces in order for the listed monitors. Monitor name or description can be used.

For example `hs.monitor_priority({"HDMI-A-1", "DP-1"})` with `num_workspaces = 10` will reserve workspaces 1-10 for monitor `HDMI-A-1`, 11-20 for `DP-1`, and additional monitors will be automatically assigned workspaces.

If `force_monitor_priority` is set to true OR if the `monitor_priority` function is called in the config, monitors not explicitly defined in monitor_priority will automatically be assigned workspace priorities based on the alphabetical order of their names (instead of monitor ids). For example if force_monitor_priority=true, `DP-1` would get lower workspaces ids than `HDMI-A-1`, regardless of monitor ids.

### Extras
Take a look in the lua code. Not everything is documented yet but if you want to script something extra there are some functions that may be useful.

### Example Config
```lua
local hs = require("hyprsplit")
hs.config({ num_workspaces = 10 })
for i = 1, 10 do
    local key = i % 10 -- 10 maps to key 0
    hl.bind("SUPER + " .. key, hs.dsp.focus({ workspace = i }))
    hl.bind("SUPER + SHIFT + " .. key, hs.dsp.window.move({ workspace = i, follow = false }))
end

hl.bind("SUPER + " .. "g", hs.dsp.grab_rogue_windows())
hl.bind("SUPER + " .. "d", hs.dsp.workspace.swap_monitors({ monitor1 = "current", monitor2 = "+1" }))

```
