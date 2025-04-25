#include "globals.hpp"
#include <hyprland/src/includes.hpp>
#include <hyprutils/string/String.hpp>

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#undef private

using namespace Hyprutils::String;

std::string getWorkspaceOnCurrentMonitor(const std::string& workspace) {
    if (!g_pCompositor->m_lastMonitor) {
        Debug::log(ERR, "[hyprsplit] no monitor in getWorkspaceOnCurrentMonitor?");
        return workspace;
    }

    int                wsID          = 1;
    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    if (workspace[0] == '+' || workspace[0] == '-') {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace, ((g_pCompositor->m_lastMonitor->activeWorkspaceID() - 1) % **NUMWORKSPACES) + 1);

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = std::max((int)PLUSMINUSRESULT.value(), 1);

        if (wsID > **NUMWORKSPACES)
            wsID = **NUMWORKSPACES;
    } else if (isNumber(workspace)) {
        wsID = std::max(std::stoi(workspace), 1);
    } else if (workspace[0] == 'r' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), g_pCompositor->m_lastMonitor->activeWorkspaceID());

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = (int)PLUSMINUSRESULT.value();

        if (wsID <= 0)
            wsID = ((((wsID - 1) % **NUMWORKSPACES) + **NUMWORKSPACES) % **NUMWORKSPACES) + 1;
    } else if (workspace[0] == 'e' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        return "m" + workspace.substr(1);
    } else if (workspace.starts_with("empty")) {
        int i = 0;
        while (++i <= **NUMWORKSPACES) {
            const int  id         = g_pCompositor->m_lastMonitor->ID * (**NUMWORKSPACES) + i;
            const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(id);

            if (!PWORKSPACE || (PWORKSPACE->getWindows() == 0))
                return std::to_string(id);
        }

        Debug::log(LOG, "[hyprsplit] no empty workspace on monitor");
        return std::to_string(g_pCompositor->m_lastMonitor->activeWorkspaceID());
    } else {
        return workspace;
    }

    if (wsID > **NUMWORKSPACES)
        wsID = ((wsID - 1) % **NUMWORKSPACES) + 1;

    return std::to_string(g_pCompositor->m_lastMonitor->ID * (**NUMWORKSPACES) + wsID);
}

void ensureGoodWorkspaces() {
    if (g_pCompositor->m_unsafeState)
        return;

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    static auto* const PERSISTENT    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->ID == MONITOR_INVALID || m->isMirror())
            continue;

        const int MIN = m->ID * (**NUMWORKSPACES) + 1;
        const int MAX = (m->ID + 1) * (**NUMWORKSPACES);

        if (m->activeWorkspaceID() < MIN || m->activeWorkspaceID() > MAX) {
            Debug::log(LOG, "[hyprsplit] {} {} active workspace {} out of bounds, changing workspace to {}", m->szName, m->ID, m->activeWorkspaceID(), MIN);
            auto ws = g_pCompositor->getWorkspaceByID(MIN);

            if (!ws) {
                ws = g_pCompositor->createNewWorkspace(MIN, m->ID);
            } else if (ws->monitorID() != m->ID) {
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }

            m->changeWorkspace(ws, false, true, true);
        }
    }

    for (auto& m : g_pCompositor->m_monitors) {
        if (m->ID == MONITOR_INVALID || m->isMirror())
            continue;

        const int  MIN = m->ID * (**NUMWORKSPACES) + 1;
        const int  MAX = (m->ID + 1) * (**NUMWORKSPACES);

        const auto WSSIZE = g_pCompositor->m_workspaces.size();
        for (size_t i = 0; i < WSSIZE; i++) {
            const auto& ws = g_pCompositor->m_workspaces[i];
            if (!valid(ws))
                continue;

            if (!**PERSISTENT || !g_pCompositor->getMonitorFromID((ws->m_id - 1) / **NUMWORKSPACES))
                ws->m_persistent = false;

            if (ws->monitorID() != m->ID && ws->m_id >= MIN && ws->m_id <= MAX) {
                Debug::log(LOG, "[hyprsplit] workspace {} on monitor {} move to {} {}", ws->m_id, ws->monitorID(), m->szName, m->ID);
                g_pCompositor->moveWorkspaceToMonitor(ws, m);
            }
        }

        if (**PERSISTENT) {
            for (auto i = MIN; i <= MAX; i++) {
                auto ws = g_pCompositor->getWorkspaceByID(i);
                if (!ws)
                    ws = g_pCompositor->createNewWorkspace(i, m->ID);

                ws->m_persistent = true;
            }
        }
    }
}

SDispatchResult focusWorkspace(std::string args) {
    const auto PCURRMONITOR = g_pCompositor->m_lastMonitor;

    if (!PCURRMONITOR) {
        Debug::log(ERR, "[hyprsplit] focusWorkspace: monitor doesn't exist");
        return {.success = false, .error = "focusWorkspace: monitor doesn't exist"};
    }

    const int WORKSPACEID = getWorkspaceIDNameFromString(getWorkspaceOnCurrentMonitor(args)).id;

    if (WORKSPACEID == WORKSPACE_INVALID) {
        Debug::log(ERR, "[hyprsplit] focusWorkspace: invalid workspace");
        return {.success = false, .error = "focusWorkspace: invalid workspace"};
    }

    auto PWORKSPACE = g_pCompositor->getWorkspaceByID(WORKSPACEID);
    if (!PWORKSPACE) {
        PWORKSPACE = g_pCompositor->createNewWorkspace(WORKSPACEID, PCURRMONITOR->ID);
        g_pKeybindManager->m_mDispatchers["workspace"](PWORKSPACE->getConfigName());
        return {};
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    const int          MIN           = PCURRMONITOR->ID * (**NUMWORKSPACES) + 1;
    const int          MAX           = (PCURRMONITOR->ID + 1) * (**NUMWORKSPACES);
    if (PWORKSPACE->monitorID() != PCURRMONITOR->ID && (WORKSPACEID >= MIN && WORKSPACEID <= MAX)) {
        Debug::log(WARN, "[hyprsplit] focusWorkspace: workspace exists but is on the wrong monitor?");
        ensureGoodWorkspaces();
    }
    g_pKeybindManager->m_mDispatchers["workspace"](PWORKSPACE->getConfigName());
    return {};
}

SDispatchResult moveToWorkspace(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_mDispatchers["movetoworkspace"](args);
    return {};
}

SDispatchResult moveToWorkspaceSilent(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_mDispatchers["movetoworkspacesilent"](args);
    return {};
}

SDispatchResult swapActiveWorkspaces(std::string args) {
    const auto MON1 = args.substr(0, args.find_first_of(' '));
    const auto MON2 = args.substr(args.find_first_of(' ') + 1);

    const auto PMON1 = g_pCompositor->getMonitorFromString(MON1);
    const auto PMON2 = g_pCompositor->getMonitorFromString(MON2);

    if (!PMON1 || !PMON2 || PMON1 == PMON2)
        return {};

    const auto PWORKSPACEA = PMON1->activeWorkspace;
    const auto PWORKSPACEB = PMON2->activeWorkspace;

    if (!PWORKSPACEA || !valid(PWORKSPACEA) || !PWORKSPACEB || !valid(PWORKSPACEB))
        return {};

    const auto LAYOUTNAME = g_pLayoutManager->m_vLayouts[g_pLayoutManager->m_iCurrentLayoutID].first;

    // with known layouts, swap the workspaces between monitors, then fix the layout
    // with an unknown layout (eg from a plugin) do a "dumb" swap by moving the windows between the workspaces.
    if (LAYOUTNAME == "dwindle" || LAYOUTNAME == "master" || LAYOUTNAME == "hy3") {
        // proceed as Hyprland normally would (see CCompositor::swapActiveWorkspaces)
        PWORKSPACEA->m_monitor = PMON2;
        PWORKSPACEA->moveToMonitor(PMON2->ID);

        for (auto& w :g_pCompositor->m_windows) {
            if (w->m_pWorkspace == PWORKSPACEA) {
                if (w->m_bPinned) {
                    w->m_pWorkspace = PWORKSPACEB;
                    continue;
                }

                w->m_pMonitor = PMON2;

                // additionally, move floating and fs windows manually
                if (w->m_bIsFloating)
                    *w->m_vRealPosition = w->m_vRealPosition->goal() - PMON1->vecPosition + PMON2->vecPosition;

                if (w->isFullscreen()) {
                    *w->m_vRealPosition = PMON2->vecPosition;
                    *w->m_vRealSize     = PMON2->vecSize;
                }

                w->updateToplevel();
            }
        }

        PWORKSPACEB->m_monitor = PMON1;
        PWORKSPACEB->moveToMonitor(PMON1->ID);

        for (auto& w :g_pCompositor->m_windows) {
            if (w->m_pWorkspace == PWORKSPACEB) {
                if (w->m_bPinned) {
                    w->m_pWorkspace = PWORKSPACEA;
                    continue;
                }

                w->m_pMonitor = PMON1;

                // additionally, move floating and fs windows manually
                if (w->m_bIsFloating)
                    *w->m_vRealPosition = w->m_vRealPosition->goal() - PMON2->vecPosition + PMON1->vecPosition;

                if (w->isFullscreen()) {
                    *w->m_vRealPosition = PMON1->vecPosition;
                    *w->m_vRealSize     = PMON1->vecSize;
                }

                w->updateToplevel();
            }
        }

        PMON1->activeWorkspace = PWORKSPACEB;
        PMON2->activeWorkspace = PWORKSPACEA;

        // swap workspace ids
        const auto TMPID      = PWORKSPACEA->m_id;
        const auto TMPNAME    = PWORKSPACEA->m_name;
        PWORKSPACEA->m_id    = PWORKSPACEB->m_id;
        PWORKSPACEA->m_name = PWORKSPACEB->m_name;
        PWORKSPACEB->m_id    = TMPID;
        PWORKSPACEB->m_name = TMPNAME;

        // swap previous workspaces
        const auto TMPPREV                      = PWORKSPACEA->m_prevWorkspace;
        PWORKSPACEA->m_prevWorkspace           = PWORKSPACEB->m_prevWorkspace;
        PWORKSPACEB->m_prevWorkspace           = TMPPREV;

        // fix the layout nodes
        if (LAYOUTNAME == "dwindle") {
            const auto LAYOUT = (CHyprDwindleLayout*)g_pLayoutManager->getCurrentLayout();
            for (auto& n : LAYOUT->m_lDwindleNodesData) {
                if (n.workspaceID == PWORKSPACEA->m_id)
                    n.workspaceID = PWORKSPACEB->m_id;
                else if (n.workspaceID == PWORKSPACEB->m_id)
                    n.workspaceID = PWORKSPACEA->m_id;
            }
        } else if (LAYOUTNAME == "master") {
            const auto LAYOUT = (CHyprMasterLayout*)g_pLayoutManager->getCurrentLayout();
            for (auto& n : LAYOUT->m_lMasterNodesData) {
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
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(PMON1->ID);
        g_pLayoutManager->getCurrentLayout()->recalculateMonitor(PMON2->ID);

        g_pCompositor->updateFullscreenFadeOnWorkspace(PWORKSPACEA);
        g_pCompositor->updateFullscreenFadeOnWorkspace(PWORKSPACEB);

        // instead of moveworkspace events, we should send movewindow events
        for (auto& w :g_pCompositor->m_windows) {
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

        for (auto& w :g_pCompositor->m_windows) {
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
    const auto PWORKSPACE = g_pCompositor->m_lastMonitor->activeWorkspace;

    if (!PWORKSPACE) {
        Debug::log(ERR, "[hyprsplit] no active workspace?");
        return {.success = false, .error = "no active workspace?"};
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    for (auto& w :g_pCompositor->m_windows) {
        if (!w->m_bIsMapped || w->onSpecialWorkspace())
            continue;

        bool inGoodWorkspace = false;

        for (auto& m : g_pCompositor->m_monitors) {
            const int MIN = m->ID * (**NUMWORKSPACES) + 1;
            const int MAX = (m->ID + 1) * (**NUMWORKSPACES);

            if (w->workspaceID() >= MIN && w->workspaceID() <= MAX) {
                inGoodWorkspace = true;
                break;
            }
        }

        if (!inGoodWorkspace) {
            Debug::log(LOG, "[hyprsplit] moving window {} to workspace {}", w->m_szTitle, PWORKSPACE->m_id);
            const auto args = std::format("{},address:0x{:x}", PWORKSPACE->m_id, (uintptr_t)w.get());
            g_pKeybindManager->m_mDispatchers["movetoworkspacesilent"](args);
        }
    }
    return {};
}

void onMonitorAdded(PHLMONITOR pMonitor) {
    Debug::log(LOG, "[hyprsplit] monitor added {}", pMonitor->szName);

    ensureGoodWorkspaces();
}

void onMonitorRemoved(PHLMONITOR pMonitor) {
    Debug::log(LOG, "[hyprsplit] monitor removed {}", pMonitor->szName);

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    static auto* const PERSISTENT    = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();

    if (**PERSISTENT) {
        const int  MIN = pMonitor->ID * (**NUMWORKSPACES) + 1;
        const int  MAX = (pMonitor->ID + 1) * (**NUMWORKSPACES);

        const auto WSSIZE = g_pCompositor->m_workspaces.size();
        for (size_t i = 0; i < WSSIZE; i++) {
            const auto& ws = g_pCompositor->m_workspaces[i];
            if (!valid(ws))
                continue;

            if (ws->m_id >= MIN && ws->m_id <= MAX)
                ws->m_persistent = false;
        }
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

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH) {
        HyprlandAPI::addNotification(PHANDLE,
                                     "[hyprsplit] Failure in initialization: Version mismatch (headers "
                                     "ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
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

    HyprlandAPI::reloadConfig();

    Debug::log(LOG, "[hyprsplit] plugin init");
    return {"hyprsplit", "split monitor workspaces", "shezdy", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Debug::log(LOG, "[hyprsplit] plugin exit");

    static auto* const PERSISTENT = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:persistent_workspaces")->getDataStaticPtr();
    if (**PERSISTENT) {
        const auto WSSIZE = g_pCompositor->m_workspaces.size();
        for (size_t i = 0; i < WSSIZE; i++) {
            const auto& ws = g_pCompositor->m_workspaces[i];
            if (!valid(ws))
                continue;
            ws->m_persistent = false;
        }
    }
}
