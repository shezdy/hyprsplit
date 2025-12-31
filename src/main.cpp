#include "globals.hpp"
#include <algorithm>
#include <cstddef>
#include <hyprland/src/includes.hpp>
#include <hyprutils/string/String.hpp>
#include <sstream>
#include <string>

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/history/WorkspaceHistoryTracker.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#undef private

using namespace Hyprutils::String;

std::string getWorkspaceOnCurrentMonitor(const std::string& workspace) {
    if (!Desktop::focusState()->monitor()) {
        Log::logger->log(Log::ERR, "[hyprsplit] no monitor in getWorkspaceOnCurrentMonitor?");
        return workspace;
    }

    int                wsID          = 1;
    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    if (workspace[0] == '+' || workspace[0] == '-') {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace, ((Desktop::focusState()->monitor()->activeWorkspaceID() - 1) % **NUMWORKSPACES) + 1);

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = std::max((int)PLUSMINUSRESULT.value(), 1);

        if (wsID > **NUMWORKSPACES)
            wsID = **NUMWORKSPACES;
    } else if (isNumber(workspace)) {
        wsID = std::max(std::stoi(workspace), 1);
    } else if (workspace[0] == 'r' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), Desktop::focusState()->monitor()->activeWorkspaceID());

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = (int)PLUSMINUSRESULT.value();

        if (wsID <= 0)
            wsID = ((((wsID - 1) % **NUMWORKSPACES) + **NUMWORKSPACES) % **NUMWORKSPACES) + 1;
    } else if (workspace[0] == 'e' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), 0);

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        const int                PLUSMINUSVALUE = (int)PLUSMINUSRESULT.value();

        std::vector<WORKSPACEID> validWSes;
        for (auto const& ws : g_pCompositor->getWorkspaces()) {
            if (ws->m_isSpecialWorkspace || ws->m_monitor != Desktop::focusState()->monitor())
                continue;

            validWSes.push_back(ws->m_id);
        }
        std::ranges::sort(validWSes);

        auto findResult = std::ranges::find(validWSes.begin(), validWSes.end(), Desktop::focusState()->monitor()->activeWorkspaceID());
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
        int i = 0;
        while (++i <= **NUMWORKSPACES) {
            const int  id         = Desktop::focusState()->monitor()->m_id * (**NUMWORKSPACES) + i;
            const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(id);

            if (!PWORKSPACE || (PWORKSPACE->getWindows() == 0))
                return std::to_string(id);
        }

        Log::logger->log(Log::DEBUG, "[hyprsplit] no empty workspace on monitor");
        return std::to_string(Desktop::focusState()->monitor()->activeWorkspaceID());
    } else {
        return workspace;
    }

    if (wsID > **NUMWORKSPACES)
        wsID = ((wsID - 1) % **NUMWORKSPACES) + 1;

    return std::to_string(Desktop::focusState()->monitor()->m_id * (**NUMWORKSPACES) + wsID);
}

void ensureGoodWorkspaces() {
    if (g_pCompositor->m_unsafeState)
        return;

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    static auto* const PERSISTENT    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->m_id == MONITOR_INVALID || m->isMirror())
            continue;

        const int MIN = m->m_id * (**NUMWORKSPACES) + 1;
        const int MAX = (m->m_id + 1) * (**NUMWORKSPACES);

        if (m->activeWorkspaceID() < MIN || m->activeWorkspaceID() > MAX) {
            Log::logger->log(Log::DEBUG, "[hyprsplit] {} {} active workspace {} out of bounds, changing workspace to {}", m->m_name, m->m_id, m->activeWorkspaceID(), MIN);
            auto ws = g_pCompositor->getWorkspaceByID(MIN);

            if (!ws) {
                ws = g_pCompositor->createNewWorkspace(MIN, m->m_id);
            } else if (ws->monitorID() != m->m_id) {
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }

            m->changeWorkspace(ws, false, true, true);
        }
    }

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->m_id == MONITOR_INVALID || m->isMirror())
            continue;

        const int MIN = m->m_id * (**NUMWORKSPACES) + 1;
        const int MAX = (m->m_id + 1) * (**NUMWORKSPACES);

        for (const auto& ws : g_pCompositor->getWorkspacesCopy()) {
            if (!valid(ws))
                continue;

            if (ws->monitorID() != m->m_id && ws->m_id >= MIN && ws->m_id <= MAX) {
                Log::logger->log(Log::DEBUG, "[hyprsplit] workspace {} on monitor {} move to {} {}", ws->m_id, ws->monitorID(), m->m_name, m->m_id);
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }
        }

        if (**PERSISTENT) {
            for (auto i = MIN; i <= MAX; i++) {
                SWorkspaceRule wsRule;
                wsRule.workspaceString         = std::to_string(i);
                wsRule.workspaceId             = i;
                wsRule.workspaceName           = wsRule.workspaceString;
                wsRule.isPersistent            = true;
                wsRule.monitor                 = std::to_string(m->m_id);
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

SDispatchResult focusWorkspace(std::string args) {
    const auto PCURRMONITOR = Desktop::focusState()->monitor();

    if (!PCURRMONITOR) {
        Log::logger->log(Log::ERR, "[hyprsplit] focusWorkspace: monitor doesn't exist");
        return {.success = false, .error = "focusWorkspace: monitor doesn't exist"};
    }

    const int WORKSPACEID = getWorkspaceIDNameFromString(getWorkspaceOnCurrentMonitor(args)).id;

    if (WORKSPACEID == WORKSPACE_INVALID) {
        Log::logger->log(Log::ERR, "[hyprsplit] focusWorkspace: invalid workspace");
        return {.success = false, .error = "focusWorkspace: invalid workspace"};
    }

    auto PWORKSPACE = g_pCompositor->getWorkspaceByID(WORKSPACEID);
    if (!PWORKSPACE) {
        PWORKSPACE = g_pCompositor->createNewWorkspace(WORKSPACEID, PCURRMONITOR->m_id);
        g_pKeybindManager->m_dispatchers["workspace"](PWORKSPACE->getConfigName());
        return {};
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    const int          MIN           = PCURRMONITOR->m_id * (**NUMWORKSPACES) + 1;
    const int          MAX           = (PCURRMONITOR->m_id + 1) * (**NUMWORKSPACES);
    if (PWORKSPACE->monitorID() != PCURRMONITOR->m_id && (WORKSPACEID >= MIN && WORKSPACEID <= MAX)) {
        Log::logger->log(Log::WARN, "[hyprsplit] focusWorkspace: workspace exists but is on the wrong monitor?");
        ensureGoodWorkspaces();
    }
    g_pKeybindManager->m_dispatchers["workspace"](PWORKSPACE->getConfigName());
    return {};
}

SDispatchResult moveToWorkspace(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_dispatchers["movetoworkspace"](args);
    return {};
}

SDispatchResult moveToWorkspaceSilent(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_dispatchers["movetoworkspacesilent"](args);
    return {};
}

SDispatchResult swapActiveWorkspaces(std::string args) {
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

    const auto LAYOUTNAME = g_pLayoutManager->m_layouts[g_pLayoutManager->m_currentLayoutID].first;

    // with known layouts, swap the workspaces between monitors, then fix the layout
    // with an unknown layout (eg from a plugin) do a "dumb" swap by moving the windows between the workspaces.
    if (LAYOUTNAME == "dwindle" || LAYOUTNAME == "master" || LAYOUTNAME == "hy3") {
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
                    *w->m_realPosition = w->m_realPosition->goal() - PMON1->m_position + PMON2->m_position;

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
                    *w->m_realPosition = w->m_realPosition->goal() - PMON2->m_position + PMON1->m_position;

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

        auto  tmpPrevData = workspacePrevDataA;

        workspacePrevDataA.previous     = workspacePrevDataB.previous;
        workspacePrevDataA.previousName = workspacePrevDataB.previousName;
        workspacePrevDataA.previousID   = workspacePrevDataB.previousID;
        workspacePrevDataA.previousMon  = workspacePrevDataB.previousMon;

        workspacePrevDataB.previous     = tmpPrevData.previous;
        workspacePrevDataB.previousName = tmpPrevData.previousName;
        workspacePrevDataB.previousID   = tmpPrevData.previousID;
        workspacePrevDataB.previousMon  = tmpPrevData.previousMon;

        // fix the layout nodes
        if (LAYOUTNAME == "dwindle") {
            const auto LAYOUT = (CHyprDwindleLayout*)g_pLayoutManager->getCurrentLayout();
            for (auto& n : LAYOUT->m_dwindleNodesData) {
                if (n->workspaceID == PWORKSPACEA->m_id)
                    n->workspaceID = PWORKSPACEB->m_id;
                else if (n->workspaceID == PWORKSPACEB->m_id)
                    n->workspaceID = PWORKSPACEA->m_id;
            }
        } else if (LAYOUTNAME == "master") {
            const auto LAYOUT = (CHyprMasterLayout*)g_pLayoutManager->getCurrentLayout();
            for (auto& n : LAYOUT->m_masterNodesData) {
                if (n.workspaceID == PWORKSPACEA->m_id)
                    n.workspaceID = PWORKSPACEB->m_id;
                else if (n.workspaceID == PWORKSPACEB->m_id)
                    n.workspaceID = PWORKSPACEA->m_id;
            }

            const auto WSDATAA = LAYOUT->getMasterWorkspaceData(PWORKSPACEA->m_id);
            const auto WSDATAB = LAYOUT->getMasterWorkspaceData(PWORKSPACEB->m_id);

            WSDATAA->workspaceID = PWORKSPACEB->m_id;
            WSDATAB->workspaceID = PWORKSPACEA->m_id;
        }

        // recalc layout
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(PMON1->m_id);
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(PMON2->m_id);

        g_pDesktopAnimationManager->setFullscreenFadeAnimation(
            PWORKSPACEB, PWORKSPACEB->m_hasFullscreenWindow ? CDesktopAnimationManager::ANIMATION_TYPE_IN : CDesktopAnimationManager::ANIMATION_TYPE_OUT);
        g_pDesktopAnimationManager->setFullscreenFadeAnimation(
            PWORKSPACEA, PWORKSPACEA->m_hasFullscreenWindow ? CDesktopAnimationManager::ANIMATION_TYPE_IN : CDesktopAnimationManager::ANIMATION_TYPE_OUT);

        // instead of moveworkspace events, we should send movewindow events
        for (auto& w : g_pCompositor->m_windows) {
            if (w->workspaceID() == PWORKSPACEA->m_id) {
                g_pEventManager->postEvent(SHyprIPCEvent{"movewindow", std::format("{:x},{}", (uintptr_t)w.get(), PWORKSPACEA->m_name)});
                g_pEventManager->postEvent(SHyprIPCEvent{"movewindowv2", std::format("{:x},{},{}", (uintptr_t)w.get(), PWORKSPACEA->m_id, PWORKSPACEA->m_name)});
                EMIT_HOOK_EVENT("moveWindow", (std::vector<std::any>{w, PWORKSPACEA}));
            } else if (w->workspaceID() == PWORKSPACEB->m_id) {
                g_pEventManager->postEvent(SHyprIPCEvent{"movewindow", std::format("{:x},{}", (uintptr_t)w.get(), PWORKSPACEB->m_name)});
                g_pEventManager->postEvent(SHyprIPCEvent{"movewindowv2", std::format("{:x},{},{}", (uintptr_t)w.get(), PWORKSPACEB->m_id, PWORKSPACEB->m_name)});
                EMIT_HOOK_EVENT("moveWindow", (std::vector<std::any>{w, PWORKSPACEB}));
            }
        }
    } else {
        // unknown layout. move all windows without preserving layout
        std::vector<PHLWINDOW> windowsA;
        std::vector<PHLWINDOW> windowsB;

        for (auto& w : g_pCompositor->m_windows) {
            if (w->workspaceID() == PWORKSPACEA->m_id) {
                windowsA.push_back(w);
            }
            if (w->workspaceID() == PWORKSPACEB->m_id) {
                windowsB.push_back(w);
            }
        }

        for (auto& w : windowsA) {
            g_pCompositor->moveWindowToWorkspaceSafe(w, PWORKSPACEB);
        }
        for (auto& w : windowsB) {
            g_pCompositor->moveWindowToWorkspaceSafe(w, PWORKSPACEA);
        }
    }

    g_pInputManager->refocus();
    return {};
}

SDispatchResult grabRogueWindows(std::string args) {
    const auto PWORKSPACE = Desktop::focusState()->monitor()->m_activeWorkspace;

    if (!PWORKSPACE) {
        Log::logger->log(Log::ERR, "[hyprsplit] no active workspace?");
        return {.success = false, .error = "no active workspace?"};
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    for (auto& w : g_pCompositor->m_windows) {
        if (!w->m_isMapped || w->onSpecialWorkspace())
            continue;

        bool inGoodWorkspace = false;

        for (auto& m : g_pCompositor->m_monitors) {
            const int MIN = m->m_id * (**NUMWORKSPACES) + 1;
            const int MAX = (m->m_id + 1) * (**NUMWORKSPACES);

            if (w->workspaceID() >= MIN && w->workspaceID() <= MAX) {
                inGoodWorkspace = true;
                break;
            }
        }

        if (!inGoodWorkspace) {
            Log::logger->log(Log::DEBUG, "[hyprsplit] moving window {} to workspace {}", w->m_title, PWORKSPACE->m_id);
            const auto args = std::format("{},address:0x{:x}", PWORKSPACE->m_id, (uintptr_t)w.get());
            g_pKeybindManager->m_dispatchers["movetoworkspacesilent"](args);
        }
    }
    return {};
}

void onMonitorAdded(PHLMONITOR pMonitor) {
    Log::logger->log(Log::DEBUG, "[hyprsplit] monitor added {}", pMonitor->m_name);

    ensureGoodWorkspaces();
}

void onMonitorRemoved(PHLMONITOR pMonitor) {
    Log::logger->log(Log::DEBUG, "[hyprsplit] monitor removed {}", pMonitor->m_name);

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    static auto* const PERSISTENT    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();

    if (**PERSISTENT) {
        const int MIN = pMonitor->m_id * (**NUMWORKSPACES) + 1;
        const int MAX = (pMonitor->m_id + 1) * (**NUMWORKSPACES);

        std::erase_if(g_pConfigManager->m_workspaceRules,
                      [&](SWorkspaceRule const& rule) { return rule.layoutopts.contains("hyprsplit") && rule.workspaceId >= MIN && rule.workspaceId <= MAX; });
        g_pConfigManager->ensurePersistentWorkspacesPresent();
    }
}

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

    HyprlandAPI::addDispatcherV2(PHANDLE, "split:workspace", focusWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:movetoworkspace", moveToWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:movetoworkspacesilent", moveToWorkspaceSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:swapactiveworkspaces", swapActiveWorkspaces);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split:grabroguewindows", grabRogueWindows);

    static auto monitorAddedHook =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", [&](void* self, SCallbackInfo& info, std::any data) { onMonitorAdded(std::any_cast<PHLMONITOR>(data)); });
    static auto monitorRemovedHook =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", [&](void* self, SCallbackInfo& info, std::any data) { onMonitorRemoved(std::any_cast<PHLMONITOR>(data)); });
    static auto configReloadedHook =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", [&](void* self, SCallbackInfo& info, std::any data) { ensureGoodWorkspaces(); });
    static auto preConfigReloadSetEnvHook =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "preConfigReload", [&](void* self, SCallbackInfo& info, std::any data) { exportHyprSplitVersionEnv(); });
    static auto configReloadedRemoveEnvHook =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", [&](void* self, SCallbackInfo& info, std::any data) { clearHyprSplitVersionEnv(); });

    HyprlandAPI::reloadConfig();

    Log::logger->log(Log::DEBUG, "[hyprsplit] plugin init");
    return {"hyprsplit", "split monitor workspaces", "shezdy", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(Log::DEBUG, "[hyprsplit] plugin exit");

    static auto* const PERSISTENT = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();
    if (**PERSISTENT) {
        std::erase_if(g_pConfigManager->m_workspaceRules, [](SWorkspaceRule const& rule) { return rule.layoutopts.contains("hyprsplit"); });
        g_pConfigManager->ensurePersistentWorkspacesPresent();
    }
}
