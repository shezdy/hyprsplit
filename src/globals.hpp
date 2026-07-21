#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

inline HANDLE PHANDLE = nullptr;

// Registered in PLUGIN_INIT via HyprlandAPI::addConfigValueV2. The API only keeps a weak ref to
// them, so the plugin has to own them for as long as it is loaded.
inline SP<Config::Values::CIntValue> g_numWorkspaces;
inline SP<Config::Values::CIntValue> g_persistentWorkspaces;
inline SP<Config::Values::CIntValue> g_forceMonitorPriority;
