#include "globals.hpp"

std::string getWorkspaceOnCurrentMonitor(const std::string& workspace) {
    if (!g_pCompositor->m_pLastMonitor) {
        Debug::log(ERR, "[hyprsplit] no monitor in getWorkspaceOnCurrentMonitor?");
        return workspace;
    }

    int                wsID          = 1;
    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    if (isNumber(workspace)) {
        wsID = std::max(std::stoi(workspace), 1);
    } else if (workspace[0] == '+' || workspace[0] == '-') {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace, ((g_pCompositor->m_pLastMonitor->activeWorkspace - 1) % **NUMWORKSPACES) + 1) ;

        if (!PLUSMINUSRESULT.has_value())
            return workspace;

        wsID = std::max((int)PLUSMINUSRESULT.value(), 1);
        
        if (wsID > **NUMWORKSPACES)
            wsID = **NUMWORKSPACES;
    } else if (workspace[0] == 'r' && (workspace[1] == '-' || workspace[1] == '+') && isNumber(workspace.substr(2))) {
        const auto PLUSMINUSRESULT = getPlusMinusKeywordResult(workspace.substr(1), g_pCompositor->m_pLastMonitor->activeWorkspace);

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
            const int  id         = g_pCompositor->m_pLastMonitor->ID * (**NUMWORKSPACES) + i;
            const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(id);

            if (!PWORKSPACE || (g_pCompositor->getWindowsOnWorkspace(id) == 0))
                return std::to_string(i);
        }

        Debug::log(LOG, "[hyprsplit] no empty workspace on monitor");
        return std::to_string(g_pCompositor->m_pLastMonitor->activeWorkspace);
    } else {
        return workspace;
    }

    if (wsID > **NUMWORKSPACES)
        wsID = ((wsID - 1) % **NUMWORKSPACES) + 1;

    return std::to_string(g_pCompositor->m_pLastMonitor->ID * (**NUMWORKSPACES) + wsID);
}

void ensureGoodWorkspaces() {
    if (g_pCompositor->m_bUnsafeState)
        return;

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    for (auto& m : g_pCompositor->m_vMonitors) {
        if (m->szName == "HEADLESS-1")
            continue;

        const int MIN = m->ID * (**NUMWORKSPACES) + 1;
        const int MAX = (m->ID + 1) * (**NUMWORKSPACES);

        if (m->activeWorkspace < MIN || m->activeWorkspace > MAX) {
            Debug::log(LOG, "[hyprsplit] {} {} active workspace {} out of bounds, changing workspace to {}", m->szName, m->ID, m->activeWorkspace, MIN);
            auto ws = g_pCompositor->getWorkspaceByID(MIN);

            if (!ws) {
                ws = g_pCompositor->createNewWorkspace(MIN, m->ID);
            } else if (ws->m_iMonitorID != m->ID) {
                g_pCompositor->moveWorkspaceToMonitor(ws, m.get());
            }

            m->changeWorkspace(ws, false, true, true);
        }

        for (auto& ws : g_pCompositor->m_vWorkspaces) {
            if (ws->m_iMonitorID != m->ID && ws->m_iID >= MIN && ws->m_iID <= MAX) {
                Debug::log(LOG, "[hyprsplit] workspace {} on monitor {} move to {} {}", ws->m_iID, ws->m_iMonitorID, m->szName, m->ID);
                g_pCompositor->moveWorkspaceToMonitor(ws.get(), m.get());
            }
        }
    }
}

void focusWorkspace(std::string args) {
    const auto PCURRMONITOR = g_pCompositor->m_pLastMonitor;

    if (!PCURRMONITOR) {
        Debug::log(ERR, "[hyprsplit] focusWorkspace: monitor doesn't exist");
        return;
    }

    std::string workspaceName;
    const int   WORKSPACEID = getWorkspaceIDFromString(getWorkspaceOnCurrentMonitor(args), workspaceName);

    if (WORKSPACEID == WORKSPACE_INVALID) {
        Debug::log(ERR, "[hyprsplit] focusWorkspace: invalid workspace");
        return;
    }

    auto PWORKSPACE = g_pCompositor->getWorkspaceByID(WORKSPACEID);
    if (!PWORKSPACE) {
        PWORKSPACE = g_pCompositor->createNewWorkspace(WORKSPACEID, PCURRMONITOR->ID);
        g_pKeybindManager->m_mDispatchers["workspace"](PWORKSPACE->getConfigName());
        return;
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();
    const int          MIN           = PCURRMONITOR->ID * (**NUMWORKSPACES) + 1;
    const int          MAX           = (PCURRMONITOR->ID + 1) * (**NUMWORKSPACES);
    if (PWORKSPACE->m_iMonitorID != PCURRMONITOR->ID && (WORKSPACEID >= MIN && WORKSPACEID <= MAX)) {
        Debug::log(WARN, "[hyprsplit] focusWorkspace: workspace exists but is on the wrong monitor?");
        ensureGoodWorkspaces();
    }
    g_pKeybindManager->m_mDispatchers["workspace"](PWORKSPACE->getConfigName());
}

void moveToWorkspace(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_mDispatchers["movetoworkspace"](args);
}

void moveToWorkspaceSilent(std::string args) {
    if (args.contains(','))
        args = getWorkspaceOnCurrentMonitor(args.substr(0, args.find_last_of(','))) + "," + args.substr(args.find_last_of(',') + 1);
    else
        args = getWorkspaceOnCurrentMonitor(args);

    g_pKeybindManager->m_mDispatchers["movetoworkspacesilent"](args);
}

void swapActiveWorkspaces(std::string args) {
    const auto MON1 = args.substr(0, args.find_first_of(' '));
    const auto MON2 = args.substr(args.find_first_of(' ') + 1);

    const auto PMON1 = g_pCompositor->getMonitorFromString(MON1);
    const auto PMON2 = g_pCompositor->getMonitorFromString(MON2);

    if (!PMON1 || !PMON2 || PMON1 == PMON2)
        return;

    const auto PWORKSPACEA = g_pCompositor->getWorkspaceByID(PMON1->activeWorkspace);
    const auto PWORKSPACEB = g_pCompositor->getWorkspaceByID(PMON2->activeWorkspace);

    if (!PWORKSPACEA || !PWORKSPACEB)
        return;

    std::vector<CWindow*> windowsA;
    std::vector<CWindow*> windowsB;

    for (auto& w : g_pCompositor->m_vWindows) {
        if (w->m_iWorkspaceID == PWORKSPACEA->m_iID) {
            windowsA.push_back(w.get());
        }
        if (w->m_iWorkspaceID == PWORKSPACEB->m_iID) {
            windowsB.push_back(w.get());
        }
    }

    for (auto& w : windowsA) {
        g_pCompositor->moveWindowToWorkspaceSafe(w, PWORKSPACEB);
    }
    for (auto& w : windowsB) {
        g_pCompositor->moveWindowToWorkspaceSafe(w, PWORKSPACEA);
    }

    g_pCompositor->updateFullscreenFadeOnWorkspace(PWORKSPACEA);
    g_pCompositor->updateFullscreenFadeOnWorkspace(PWORKSPACEB);
}

void grabRogueWindows(std::string args) {
    const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(g_pCompositor->m_pLastMonitor->activeWorkspace);

    if (!PWORKSPACE) {
        Debug::log(ERR, "[hyprsplit] no active workspace?");
        return;
    }

    static auto* const NUMWORKSPACES = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces")->getDataStaticPtr();

    for (auto& w : g_pCompositor->m_vWindows) {
        if (!w->m_bIsMapped)
            continue;

        bool inGoodWorkspace = false;

        for (auto& m : g_pCompositor->m_vMonitors) {
            const int MIN = m->ID * (**NUMWORKSPACES) + 1;
            const int MAX = (m->ID + 1) * (**NUMWORKSPACES);

            if (w->m_iWorkspaceID >= MIN && w->m_iWorkspaceID <= MAX && w->m_iWorkspaceID > 0) {
                inGoodWorkspace = true;
                break;
            }
        }

        if (!inGoodWorkspace) {
            Debug::log(LOG, "[hyprsplit] moving window {} to workspace {}", w->m_szTitle, PWORKSPACE->m_iID);
            const auto args = std::format("{},address:0x{:x}", PWORKSPACE->m_iID, w.get());
            g_pKeybindManager->m_mDispatchers["movetoworkspacesilent"](args);
        }
    }
}

void onMonitorAdded(std::any data) {
    auto* const PMONITOR = std::any_cast<CMonitor*>(data);
    Debug::log(LOG, "[hyprsplit] monitor added {}", PMONITOR->szName);

    ensureGoodWorkspaces();
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
                                     CColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprsplit] Version mismatch");
    }
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprsplit:num_workspaces", Hyprlang::INT{10});

    HyprlandAPI::addDispatcher(PHANDLE, "split:workspace", focusWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split:movetoworkspace", moveToWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split:movetoworkspacesilent", moveToWorkspaceSilent);
    HyprlandAPI::addDispatcher(PHANDLE, "split:swapactiveworkspaces", swapActiveWorkspaces);
    HyprlandAPI::addDispatcher(PHANDLE, "split:grabroguewindows", grabRogueWindows);

    HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", [&](void* self, SCallbackInfo& info, std::any data) { onMonitorAdded(data); });
    HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", [&](void* self, SCallbackInfo& info, std::any data) { ensureGoodWorkspaces(); });

    HyprlandAPI::reloadConfig();

    Debug::log(LOG, "[hyprsplit] plugin init");
    return {"hyprsplit", "split monitor workspaces", "shezdy", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Debug::log(LOG, "[hyprsplit] plugin exit");
}