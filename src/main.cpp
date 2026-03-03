#include "globals.hpp"
#include "log.hpp"
#include <algorithm>
#include <cstddef>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/includes.hpp>
#include <hyprutils/string/String.hpp>
#include <sstream>
#include <string>

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/history/WorkspaceHistoryTracker.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/trackpad/gestures/ITrackpadGesture.hpp>
#include <hyprland/src/render/Renderer.hpp>
#undef private

using namespace Hyprutils::String;

struct MonitorWorkspaceMapping {
    std::string monitorIdentifier; // "desc:FooBar" or "DP-1"
    long        min;               // inclusive
    long        max;               // inclusive
};

static std::vector<MonitorWorkspaceMapping> g_workspaceMappings;

// Mirrors Hyprland's monitor matching in CConfigManager::getMonitorRuleFor:
// desc: prefix does substring match against monitor short description,
// otherwise exact match against monitor name.
static bool monitorMatchesIdentifier(const PHLMONITOR& monitor, const std::string& identifier) {
    if (identifier.starts_with("desc:")) {
        const auto desc = identifier.substr(5);
        return monitor->m_shortDescription.find(desc) != std::string::npos;
    }
    return monitor->m_name == identifier;
}

static bool rangeOverlaps(long minA, long maxA, long minB, long maxB) {
    return minA <= maxB && minB <= maxA;
}

static Hyprlang::CParseResult onWorkspaceMapKeyword(const char* command, const char* value) {
    Hyprlang::CParseResult result;
    std::string            val = value;

    // parse "desc:FooBar, 1, 10" or "DP-1, 1, 10"
    // find last two commas to split min and max, everything before is the identifier
    auto lastComma = val.find_last_of(',');
    if (lastComma == std::string::npos) {
        result.setError("workspace_map: expected format '<monitor>, <min>, <max>'");
        return result;
    }

    auto secondLastComma = val.find_last_of(',', lastComma - 1);
    if (secondLastComma == std::string::npos) {
        result.setError("workspace_map: expected format '<monitor>, <min>, <max>'");
        return result;
    }

    auto identifier = trim(val.substr(0, secondLastComma));
    auto minStr     = trim(val.substr(secondLastComma + 1, lastComma - secondLastComma - 1));
    auto maxStr     = trim(val.substr(lastComma + 1));

    if (identifier.empty() || minStr.empty() || maxStr.empty()) {
        result.setError("workspace_map: empty field in '<monitor>, <min>, <max>'");
        return result;
    }

    long min, max;
    try {
        min = std::stol(minStr);
        max = std::stol(maxStr);
    } catch (...) {
        result.setError("workspace_map: min and max must be integers");
        return result;
    }

    if (min < 1) {
        result.setError("workspace_map: min must be >= 1");
        return result;
    }
    if (min > max) {
        result.setError("workspace_map: min must be <= max");
        return result;
    }

    // check for overlap with existing mappings
    for (const auto& mapping : g_workspaceMappings) {
        if (rangeOverlaps(min, max, mapping.min, mapping.max)) {
            result.setError(std::format("workspace_map: range {}-{} overlaps with existing mapping for '{}' ({}-{})", min, max, mapping.monitorIdentifier, mapping.min, mapping.max)
                                .c_str());
            return result;
        }
    }

    g_workspaceMappings.push_back({identifier, min, max});
    hsLog(DEBUG, "workspace_map: '{}' -> {}-{}", identifier, min, max);

    return result;
}

class MonitorRange {
  public:
    long min; // min workspace id on monitor (inclusive)
    long max; // max workspace id on monitor (inclusive)

    MonitorRange(const PHLMONITOR& monitor) {
        // check explicit workspace_map entries first
        for (const auto& mapping : g_workspaceMappings) {
            if (monitorMatchesIdentifier(monitor, mapping.monitorIdentifier)) {
                min = mapping.min;
                max = mapping.max;
                return;
            }
        }

        // fallback: auto-assign a range of num_workspaces that doesn't overlap any explicit mapping.
        // to keep assignment stable, we determine this monitor's fallback index by sorting all
        // unmapped monitors by name and finding our position in that sorted order.
        const auto NUMWORKSPACES = CConfigValue<Hyprlang::INT>("plugin:hyprsplit:num_workspaces");

        std::vector<std::string> unmappedMonitorNames;
        for (const auto& m : g_pCompositor->m_monitors) {
            if (m->m_id == MONITOR_INVALID || m->isMirror())
                continue;

            bool mapped = false;
            for (const auto& mapping : g_workspaceMappings) {
                if (monitorMatchesIdentifier(m, mapping.monitorIdentifier)) {
                    mapped = true;
                    break;
                }
            }
            if (!mapped)
                unmappedMonitorNames.push_back(m->m_name);
        }
        std::ranges::sort(unmappedMonitorNames);

        int fallbackIndex = 0;
        for (size_t i = 0; i < unmappedMonitorNames.size(); i++) {
            if (unmappedMonitorNames[i] == monitor->m_name) {
                fallbackIndex = i;
                break;
            }
        }

        // find the (fallbackIndex+1)th contiguous block of NUMWORKSPACES that doesn't overlap any mapping
        long candidateMin = 1;
        int  found        = 0;
        while (true) {
            long candidateMax     = candidateMin + *NUMWORKSPACES - 1;
            bool overlapsExplicit = false;
            for (const auto& mapping : g_workspaceMappings) {
                if (rangeOverlaps(candidateMin, candidateMax, mapping.min, mapping.max)) {
                    overlapsExplicit = true;
                    // skip past this mapping
                    candidateMin = mapping.max + 1;
                    break;
                }
            }
            if (!overlapsExplicit) {
                if (found == fallbackIndex) {
                    min = candidateMin;
                    max = candidateMax;
                    return;
                }
                found++;
                candidateMin = candidateMax + 1;
            }
        }
    }

    long size() const {
        return max - min + 1;
    }

    bool contains(const long& num) const {
        return num >= min && num <= max;
    }
};

static std::vector<const char*> HYPRSPLIT_VERSION_VARS = {
    "HYPRSPLIT",
};

static void exportHyprSplitVersionEnv() {
    for (const auto& v : HYPRSPLIT_VERSION_VARS) {
        setenv(v, "1", 1);
    }
}

static void clearHyprSplitVersionEnv() {
    for (const auto& v : HYPRSPLIT_VERSION_VARS) {
        unsetenv(v);
    }
}

static std::string getWorkspaceOnCurrentMonitor(const std::string& workspace) {
    if (!Desktop::focusState()->monitor()) {
        hsLog(ERR, "no monitor in getWorkspaceOnCurrentMonitor?");
        return workspace;
    }

    const auto MONITOR  = Desktop::focusState()->monitor();
    const auto RANGE    = MonitorRange(MONITOR);
    const long RANGESIZE = RANGE.size();

    int wsID = 1;

    if (workspace[0] == '+' || workspace[0] == '-') {
        // current workspace as 1-based local index within range
        const long localCurrent    = ((MONITOR->activeWorkspaceID() - RANGE.min) % RANGESIZE) + 1;
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace, localCurrent);

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = std::max((int)PLUSMINUSRESULT.value(), 1);
        wsID = std::min(wsID, (int)RANGESIZE);
    } else if (isNumber(workspace)) {
        wsID = std::max(std::stoi(workspace), 1);
    } else if (workspace[0] == 'r' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), MONITOR->activeWorkspaceID());

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = (int)PLUSMINUSRESULT.value();

        if (wsID <= 0)
            wsID = ((((wsID - 1) % RANGESIZE) + RANGESIZE) % RANGESIZE) + 1;
    } else if (workspace[0] == 'e' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), 0);

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        const int                PLUSMINUSVALUE = (int)PLUSMINUSRESULT.value();

        std::vector<WORKSPACEID> validWSes;
        for (auto const& ws : g_pCompositor->getWorkspaces()) {
            if (ws->m_isSpecialWorkspace || ws->m_monitor != MONITOR)
                continue;

            validWSes.push_back(ws->m_id);
        }
        std::ranges::sort(validWSes);

        auto findResult = std::ranges::find(validWSes.begin(), validWSes.end(), MONITOR->activeWorkspaceID());
        if (findResult == validWSes.end())
            return workspace;
        size_t current = findResult - validWSes.begin();

        int    resultIndex = current + PLUSMINUSVALUE;
        if (resultIndex < 0)
            resultIndex = 0;
        else if ((size_t)resultIndex >= validWSes.size())
            resultIndex = validWSes.size() - 1;
        WORKSPACEID result = validWSes[resultIndex];

        return std::to_string(result);
    } else if (workspace.starts_with("empty")) {
        for (long id = RANGE.min; id <= RANGE.max; id++) {
            const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(id);

            if (!PWORKSPACE || (PWORKSPACE->getWindows() == 0))
                return std::to_string(id);
        }

        hsLog(DEBUG, "no empty workspace on monitor");
        return std::to_string(MONITOR->activeWorkspaceID());
    } else {
        return workspace;
    }

    if (wsID > RANGESIZE)
        wsID = ((wsID - 1) % RANGESIZE) + 1;

    return std::to_string(RANGE.min + wsID - 1);
}

static void ensureGoodWorkspaces() {
    if (g_pCompositor->m_unsafeState)
        return;

    const auto PERSISTENT = CConfigValue<Hyprlang::INT>("plugin:hyprsplit:persistent_workspaces");

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->m_id == MONITOR_INVALID || m->isMirror())
            continue;

        const auto RANGE = MonitorRange(m);

        if (!RANGE.contains(m->activeWorkspaceID())) {
            hsLog(DEBUG, "{} {} active workspace {} out of bounds, changing workspace to {}", m->m_name, m->m_id, m->activeWorkspaceID(), RANGE.min);
            auto ws = g_pCompositor->getWorkspaceByID(RANGE.min);

            if (!ws) {
                ws = g_pCompositor->createNewWorkspace(RANGE.min, m->m_id);
            } else if (ws->monitorID() != m->m_id) {
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }

            m->changeWorkspace(ws, false, true, true);
        }
    }

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->m_id == MONITOR_INVALID || m->isMirror())
            continue;

        const auto RANGE = MonitorRange(m);

        for (const auto& ws : g_pCompositor->getWorkspacesCopy()) {
            if (!valid(ws))
                continue;

            if (ws->monitorID() != m->m_id && RANGE.contains(ws->m_id)) {
                hsLog(DEBUG, "workspace {} on monitor {} move to {} {}", ws->m_id, ws->monitorID(), m->m_name, m->m_id);
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }
        }

        if (*PERSISTENT) {
            for (auto i = RANGE.min; i <= RANGE.max; i++) {
                SWorkspaceRule wsRule;
                wsRule.workspaceString         = std::to_string(i);
                wsRule.workspaceId             = i;
                wsRule.workspaceName           = wsRule.workspaceString;
                wsRule.isPersistent            = true;
                wsRule.monitor                 = m->m_name;
                wsRule.layoutopts["hyprsplit"] = "1";

                const auto IT = std::ranges::find_if(g_pConfigManager->m_workspaceRules,
                                                     [&](const auto& other) { return other.layoutopts.contains("hyprsplit") && other.workspaceId == wsRule.workspaceId; });

                if (IT == g_pConfigManager->m_workspaceRules.end())
                    g_pConfigManager->m_workspaceRules.emplace_back(wsRule);
                else
                    IT->monitor = wsRule.monitor;
                g_pConfigManager->ensurePersistentWorkspacesPresent();
            }
        }
    }
}

static SDispatchResult focusWorkspace(std::string args) {
    const auto PCURRMONITOR = Desktop::focusState()->monitor();

    if (!PCURRMONITOR) {
        hsLog(ERR, "focusWorkspace: monitor doesn't exist");
        return {.success = false, .error = "focusWorkspace: monitor doesn't exist"};
    }

    const int WORKSPACEID = getWorkspaceIDNameFromString(getWorkspaceOnCurrentMonitor(args)).id;

    if (WORKSPACEID == WORKSPACE_INVALID) {
        hsLog(ERR, "focusWorkspace: invalid workspace");
        return {.success = false, .error = "focusWorkspace: invalid workspace"};
    }

    auto PWORKSPACE = g_pCompositor->getWorkspaceByID(WORKSPACEID);
    if (!PWORKSPACE) {
        PWORKSPACE = g_pCompositor->createNewWorkspace(WORKSPACEID, PCURRMONITOR->m_id);
        g_pKeybindManager->m_dispatchers["workspace"](PWORKSPACE->getConfigName());
        return {};
    }

    const auto RANGE = MonitorRange(PCURRMONITOR);
    if (PWORKSPACE->monitorID() != PCURRMONITOR->m_id && (RANGE.contains(WORKSPACEID))) {
        hsLog(WARN, "focusWorkspace: workspace exists but is on the wrong monitor?");
        ensureGoodWorkspaces();
    }
    g_pKeybindManager->m_dispatchers["workspace"](PWORKSPACE->getConfigName());
    return {};
}

static SDispatchResult moveToWorkspace(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_dispatchers["movetoworkspace"](args);
    return {};
}

static SDispatchResult moveToWorkspaceSilent(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_dispatchers["movetoworkspacesilent"](args);
    return {};
}

static SDispatchResult swapActiveWorkspaces(std::string args) {
    const auto MON1 = args.substr(0, args.find_first_of(' '));
    const auto MON2 = args.substr(args.find_first_of(' ') + 1);

    const auto PMON1 = g_pCompositor->getMonitorFromString(MON1);
    const auto PMON2 = g_pCompositor->getMonitorFromString(MON2);

    if (!PMON1 || !PMON2 || PMON1 == PMON2)
        return {};

    const auto PWORKSPACEA = PMON1->m_activeWorkspace;
    const auto PWORKSPACEB = PMON2->m_activeWorkspace;

    if (!PWORKSPACEA || !valid(PWORKSPACEA) || !PWORKSPACEB || !valid(PWORKSPACEB))
        return {};

    // proceed as Hyprland normally would (see CCompositor::swapActiveWorkspaces)
    PWORKSPACEA->m_monitor = PMON2;
    PWORKSPACEA->m_events.monitorChanged.emit();

    for (auto& w : g_pCompositor->m_windows) {
        if (w->m_workspace == PWORKSPACEA) {
            if (w->m_pinned) {
                w->m_workspace = PWORKSPACEB;
                continue;
            }

            w->m_monitor = PMON2;

            // additionally, move floating and fs windows manually
            if (w->m_isFloating)
                w->layoutTarget()->setPositionGlobal(w->layoutTarget()->position().translate(-PMON1->m_position + PMON2->m_position));

            if (w->isFullscreen()) {
                *w->m_realPosition = PMON2->m_position;
                *w->m_realSize     = PMON2->m_size;
            }

            w->updateToplevel();
        }
    }

    PWORKSPACEB->m_monitor = PMON1;
    PWORKSPACEB->m_events.monitorChanged.emit();

    for (auto& w : g_pCompositor->m_windows) {
        if (w->m_workspace == PWORKSPACEB) {
            if (w->m_pinned) {
                w->m_workspace = PWORKSPACEA;
                continue;
            }

            w->m_monitor = PMON1;

            // additionally, move floating and fs windows manually
            if (w->m_isFloating)
                w->layoutTarget()->setPositionGlobal(w->layoutTarget()->position().translate(-PMON2->m_position + PMON1->m_position));

            if (w->isFullscreen()) {
                *w->m_realPosition = PMON1->m_position;
                *w->m_realSize     = PMON1->m_size;
            }

            w->updateToplevel();
        }
    }

    PMON1->m_activeWorkspace = PWORKSPACEB;
    PMON2->m_activeWorkspace = PWORKSPACEA;

    // swap workspace ids
    const auto TMPID    = PWORKSPACEA->m_id;
    const auto TMPNAME  = PWORKSPACEA->m_name;
    PWORKSPACEA->m_id   = PWORKSPACEB->m_id;
    PWORKSPACEA->m_name = PWORKSPACEB->m_name;
    PWORKSPACEB->m_id   = TMPID;
    PWORKSPACEB->m_name = TMPNAME;

    // swap previous workspaces
    Desktop::History::workspaceTracker()->gc();

    auto workspacePrevDataA = Desktop::History::workspaceTracker()->dataFor(PWORKSPACEA);
    auto workspacePrevDataB = Desktop::History::workspaceTracker()->dataFor(PWORKSPACEB);

    auto tmpPrevData = workspacePrevDataA;

    workspacePrevDataA.previous     = workspacePrevDataB.previous;
    workspacePrevDataA.previousName = workspacePrevDataB.previousName;
    workspacePrevDataA.previousID   = workspacePrevDataB.previousID;
    workspacePrevDataA.previousMon  = workspacePrevDataB.previousMon;

    workspacePrevDataB.previous     = tmpPrevData.previous;
    workspacePrevDataB.previousName = tmpPrevData.previousName;
    workspacePrevDataB.previousID   = tmpPrevData.previousID;
    workspacePrevDataB.previousMon  = tmpPrevData.previousMon;

    // swap layouts
    auto workspaceLayoutA = PWORKSPACEA->m_space;
    auto workspaceLayoutB = PWORKSPACEB->m_space;

    auto tmpPrevLayout = workspaceLayoutA;

    workspaceLayoutA = workspaceLayoutB;
    workspaceLayoutB = tmpPrevLayout;

    PWORKSPACEA->m_space->recalculate();
    PWORKSPACEB->m_space->recalculate();

    g_pDesktopAnimationManager->setFullscreenFadeAnimation(
        PWORKSPACEB, PWORKSPACEB->m_hasFullscreenWindow ? CDesktopAnimationManager::ANIMATION_TYPE_IN : CDesktopAnimationManager::ANIMATION_TYPE_OUT);
    g_pDesktopAnimationManager->setFullscreenFadeAnimation(
        PWORKSPACEA, PWORKSPACEA->m_hasFullscreenWindow ? CDesktopAnimationManager::ANIMATION_TYPE_IN : CDesktopAnimationManager::ANIMATION_TYPE_OUT);

    g_pHyprRenderer->damageMonitor(PMON1);
    g_pHyprRenderer->damageMonitor(PMON2);

    g_pInputManager->refocus();

    // instead of moveworkspace events, we should send movewindow events
    for (auto& w : g_pCompositor->m_windows) {
        if (w->workspaceID() == PWORKSPACEA->m_id) {
            g_pEventManager->postEvent(SHyprIPCEvent{"movewindow", std::format("{:x},{}", (uintptr_t)w.get(), PWORKSPACEA->m_name)});
            g_pEventManager->postEvent(SHyprIPCEvent{"movewindowv2", std::format("{:x},{},{}", (uintptr_t)w.get(), PWORKSPACEA->m_id, PWORKSPACEA->m_name)});
            Event::bus()->m_events.window.moveToWorkspace.emit(w, PWORKSPACEA);
        } else if (w->workspaceID() == PWORKSPACEB->m_id) {
            g_pEventManager->postEvent(SHyprIPCEvent{"movewindow", std::format("{:x},{}", (uintptr_t)w.get(), PWORKSPACEB->m_name)});
            g_pEventManager->postEvent(SHyprIPCEvent{"movewindowv2", std::format("{:x},{},{}", (uintptr_t)w.get(), PWORKSPACEB->m_id, PWORKSPACEB->m_name)});
            Event::bus()->m_events.window.moveToWorkspace.emit(w, PWORKSPACEB);
        }
    }

    return {};
}

static SDispatchResult grabRogueWindows(std::string args) {
    const auto PWORKSPACE = Desktop::focusState()->monitor()->m_activeWorkspace;

    if (!PWORKSPACE) {
        hsLog(ERR, "no active workspace?");
        return {.success = false, .error = "no active workspace?"};
    }

    for (auto& w : g_pCompositor->m_windows) {
        if (!w->m_isMapped || w->onSpecialWorkspace())
            continue;

        bool inGoodWorkspace = false;

        for (auto& m : g_pCompositor->m_monitors) {
            const auto RANGE = MonitorRange(m);

            if (RANGE.contains(w->workspaceID())) {
                inGoodWorkspace = true;
                break;
            }
        }

        if (!inGoodWorkspace) {
            hsLog(DEBUG, "moving window {} to workspace {}", w->m_title, PWORKSPACE->m_id);
            const auto args = std::format("{},address:0x{:x}", PWORKSPACE->m_id, (uintptr_t)w.get());
            g_pKeybindManager->m_dispatchers["movetoworkspacesilent"](args);
        }
    }
    return {};
}

static void onMonitorAdded(PHLMONITOR pMonitor) {
    hsLog(DEBUG, "monitor added {}", pMonitor->m_name);

    ensureGoodWorkspaces();
}

static void onMonitorRemoved(PHLMONITOR pMonitor) {
    hsLog(DEBUG, "monitor removed {}", pMonitor->m_name);

    const auto PERSISTENT = CConfigValue<Hyprlang::INT>("plugin:hyprsplit:persistent_workspaces");

    if (*PERSISTENT) {
        const auto RANGE = MonitorRange(pMonitor);

        std::erase_if(g_pConfigManager->m_workspaceRules, [&](SWorkspaceRule const& rule) { return rule.layoutopts.contains("hyprsplit") && RANGE.contains(rule.workspaceId); });
        g_pConfigManager->ensurePersistentWorkspacesPresent();
    }
}

static void onConfigReloaded() {
    clearHyprSplitVersionEnv();
    ensureGoodWorkspaces();
}

static void onConfigPreReloaded() {
    g_workspaceMappings.clear();
    exportHyprSplitVersionEnv();
}

static inline CFunctionHook* g_pWorkspaceSwipeGestureBeginHook = nullptr;
typedef void (*origWorkspaceSwipeGestureBegin)(void*, const ITrackpadGesture::STrackpadGestureBegin& e);
static void hkWorkspaceSwipeGestureBegin(void* thisptr, const ITrackpadGesture::STrackpadGestureBegin& e) {
    hsLog(DEBUG, "hook workspace swipe begin");

    // partial taken from CWorkspaceSwipeGesture::update
    static auto PSWIPEINVR = CConfigValue<Hyprlang::INT>("gestures:workspace_swipe_invert");
    int         dir        = e.direction == TRACKPAD_GESTURE_DIR_LEFT ? -1 : 1;
    if (*PSWIPEINVR)
        dir = -dir;

    auto       m     = Desktop::focusState()->monitor();
    const auto RANGE = MonitorRange(m);

    if (m->activeWorkspaceID() == RANGE.max && dir > 0) {
        hsLog(DEBUG, "blocking workspace swipe begin to right on ws {}", m->activeWorkspaceID());
        return;
    }
    if (m->activeWorkspaceID() == RANGE.min && dir < 0) {
        hsLog(DEBUG, "blocking workspace swipe begin to left on ws {}", m->activeWorkspaceID());
        return;
    }

    hsLog(DEBUG, "calling original workspace swipe begin");
    return (*(origWorkspaceSwipeGestureBegin)g_pWorkspaceSwipeGestureBeginHook->m_original)(thisptr, e);
}

// other plugins can use this to convert a regular hyprland workspace string the correct hyprsplit one
APICALL EXPORT std::string hyprsplitGetWorkspace(const std::string& workspace) {
    return getWorkspaceOnCurrentMonitor(workspace);
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE,
                                     "[hyprsplit] Failure in initialization: Version mismatch (headers "
                                     "ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        HyprlandAPI::addNotification(PHANDLE, std::format("[hyprsplit] compositor hash: {}", HASH), CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        HyprlandAPI::addNotification(PHANDLE, std::format("[hyprsplit] client hash: {}", CLIENT_HASH), CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprsplit] Version mismatch");
    }

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces", Hyprlang::INT{10});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces", Hyprlang::INT{0});
    HyprlandAPI::addConfigKeyword(PHANDLE, "plugin:hyprsplit:workspace_map", onWorkspaceMapKeyword, Hyprlang::SHandlerOptions{});

    HyprlandAPI::addDispatcherV2(PHANDLE, "split:workspace", focusWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:movetoworkspace", moveToWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:movetoworkspacesilent", moveToWorkspaceSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:swapactiveworkspaces", swapActiveWorkspaces);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:grabroguewindows", grabRogueWindows);

    static auto       monitorAddedListener      = Event::bus()->m_events.monitor.added.listen([&](PHLMONITOR m) { onMonitorAdded(m); });
    static auto       monitorRemovedListener    = Event::bus()->m_events.monitor.removed.listen([&](PHLMONITOR m) { onMonitorRemoved(m); });
    static auto       configReloadedListener    = Event::bus()->m_events.config.reloaded.listen([&] { onConfigReloaded(); });
    static auto       configPreReloadedListener = Event::bus()->m_events.config.preReload.listen([&] { onConfigPreReloaded(); });

    static const auto foundBeginFunctions = HyprlandAPI::findFunctionsByName(PHANDLE, "begin");
    for (auto& fun : foundBeginFunctions) {
        if (fun.signature.find("CWorkspaceSwipeGesture") != std::string::npos) {
            g_pWorkspaceSwipeGestureBeginHook = HyprlandAPI::createFunctionHook(PHANDLE, fun.address, (void*)&hkWorkspaceSwipeGestureBegin);
            if (g_pWorkspaceSwipeGestureBeginHook != nullptr && g_pWorkspaceSwipeGestureBeginHook->hook())
                hsLog(DEBUG, "hooked CWorkspaceSwipeGesture::begin", fun.signature);
        }
    }

    HyprlandAPI::reloadConfig();

    hsLog(DEBUG, "plugin init");
    return {"hyprsplit", "split monitor workspaces", "shezdy", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    hsLog(DEBUG, "plugin exit");

    const auto PERSISTENT = CConfigValue<Hyprlang::INT>("plugin:hyprsplit:persistent_workspaces");
    if (*PERSISTENT) {
        std::erase_if(g_pConfigManager->m_workspaceRules, [](SWorkspaceRule const& rule) { return rule.layoutopts.contains("hyprsplit"); });
        g_pConfigManager->ensurePersistentWorkspacesPresent();
    }
}
