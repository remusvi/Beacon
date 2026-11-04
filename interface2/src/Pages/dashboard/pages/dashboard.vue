<script setup>
import {
    onMounted,
    ref,
    onUnmounted,
    nextTick,
    computed,
    watch,
    reactive,
} from "vue";
import {
    Box,
    Layers,
    FileCode,
    Hammer,
    Download,
    Plus,
    Trash2,
    Users,
    Crown,
    ShieldCheck,
    ShieldAlert,
    LogOut,
    Ban,
    FileText,
    X,
    Check,
    ArrowLeft,
    Terminal,
    Send,
    MoreVertical,
    Server,
} from "lucide-vue-next";

/**
 * --- PERSISTENCE & HISTORY STORAGE ---
 * Bridges Vue's reactive state with browser sessionStorage.
 */
const STORAGE_KEY = "BEACON_SESSION_LOGS";

const saveToHistory = (id, data) => {
    try {
        const history = JSON.parse(sessionStorage.getItem(STORAGE_KEY) || "{}");
        history[id] = data;
        sessionStorage.setItem(STORAGE_KEY, JSON.stringify(history));
    } catch (e) {
        console.error("History Save Error:", e);
    }
};

const loadFromHistory = (id) => {
    try {
        const history = JSON.parse(sessionStorage.getItem(STORAGE_KEY) || "{}");
        return history[id] || [];
    } catch (e) {
        return [];
    }
};

// Initialize the global store window object if it doesn't exist
if (!window.__BEACON_STORE__) {
    window.__BEACON_STORE__ = reactive({
        logs: {},
        activeListeners: new Set(),
    });
}
const store = window.__BEACON_STORE__;

const isLoading = ref(true);
const error = ref(null);
const instances = ref([]);
const currentView = ref("grid");
const activeInstance = ref(null);
const activeMenuId = ref(null);
const isDeploying = ref(false);
const insDragging = ref(false);

// UI Deployment States
const isCreating = ref(false);
const newInstanceName = ref("");
const newInstanceVersion = ref("1.20.1");
const selectedType = ref("Vanilla");
const memoryAlloc = ref(3);
const isOnlineMode = ref(false);
const selectedFile = ref(null);
const selectedPath = ref("");

const serverTypes = [
    {
        id: "Vanilla",
        name: "Vanilla",
        logo: Box,
        desc: "Official Mojang Engine",
    },
    { id: "Fabric", name: "Fabric", logo: Layers, desc: "Lightweight Modding" },
    { id: "Paper", name: "Paper", logo: FileCode, desc: "High-Performance Spigot" },
    { id: "Forge", name: "Forge", logo: Hammer, desc: "Heavy Modding Support" },
];

// Version Utilities
const generateVersions = () => {
    const versions = [];
    for (let i = 1; i >= 0; i--) versions.push(`26.${i}`);
    for (let i = 21; i >= 8; i--) {
        if (i === 8) versions.push("1.8.9");
        else versions.push(`1.${i}.1`);
    }
    return [...new Set(versions)];
};

const allVersions = generateVersions();
const compatibilityMap = {
    Vanilla: allVersions,
    Paper: allVersions.filter((v) => !v.startsWith("26")),
    Fabric: allVersions.filter(
        (v) => parseFloat(v.split(".")[1]) >= 14 || v.startsWith("26"),
    ),
    Forge: allVersions.filter(
        (v) => parseFloat(v.split(".")[1]) >= 12 && !v.startsWith("26"),
    ),
};

const availableVersions = computed(
    () => compatibilityMap[selectedType.value] || [],
);
const isFormValid = computed(
    () => newInstanceName.value.trim().length > 0 && !isDeploying.value,
);

watch(selectedType, (newType) => {
    if (!compatibilityMap[newType].includes(newInstanceVersion.value)) {
        newInstanceVersion.value = compatibilityMap[newType][0];
    }
});

const showImport = () => {
    currentView.value = "import";
};

const logs = computed(() => {
    if (!activeInstance.value) return [];
    return consoleLogs.value.length ? consoleLogs.value : (store.logs[activeInstance.value.id] || []);
});

const logContainer = ref(null);
const selectedPlayer = ref(null);
const serverPlayers = ref([]);
const consoleLogs = ref([]);
const consoleCommand = ref("");
const quickOpName = ref("");
const actionMessage = ref("");
let statsPollInterval = null;

const showFeedback = (msg) => {
    actionMessage.value = msg;
    setTimeout(() => {
        if (actionMessage.value === msg) {
            actionMessage.value = "";
        }
    }, 4500);
};

const refreshServerData = async (id) => {
    if (!id) return;
    try {
        if (window.getServerPlayers) {
            const rawPlayers = await window.getServerPlayers(id.toString());
            const parsed = typeof rawPlayers === "string" ? JSON.parse(rawPlayers) : rawPlayers;
            serverPlayers.value = Array.isArray(parsed) ? parsed : [];
            if (activeInstance.value && activeInstance.value.id === id) {
                activeInstance.value.players = serverPlayers.value;
            }
        }
        if (window.getServerLogs) {
            const rawLogs = await window.getServerLogs(id.toString(), 200);
            const parsed = typeof rawLogs === "string" ? JSON.parse(rawLogs) : rawLogs;
            if (Array.isArray(parsed)) {
                consoleLogs.value = parsed;
                store.logs[id] = parsed;
            }
        }
    } catch (err) {
        console.error("Error refreshing server data:", err);
    }
};

const startStatsPolling = (id) => {
    stopStatsPolling();
    refreshServerData(id);
    statsPollInterval = setInterval(() => {
        if (currentView.value === "stats" && activeInstance.value?.id === id) {
            refreshServerData(id);
        } else {
            stopStatsPolling();
        }
    }, 1500);
};

const stopStatsPolling = () => {
    if (statsPollInterval) {
        clearInterval(statsPollInterval);
        statsPollInterval = null;
    }
};

const handleOpPlayer = async (player) => {
    if (!activeInstance.value || !player) return;
    try {
        if (window.opPlayer) {
            const res = await window.opPlayer(activeInstance.value.id.toString(), player.name);
            const data = typeof res === "string" ? JSON.parse(res) : res;
            if (data.status === "ok") {
                player.isOp = true;
                showFeedback(`Granted Operator status to ${player.name}`);
            } else {
                showFeedback(`Failed to OP: ${data.error || "Unknown error"}`);
            }
        }
        await refreshServerData(activeInstance.value.id);
    } catch (err) {
        showFeedback(`Error: ${err.message || err}`);
    }
};

const handleDeopPlayer = async (player) => {
    if (!activeInstance.value || !player) return;
    try {
        if (window.deopPlayer) {
            const res = await window.deopPlayer(activeInstance.value.id.toString(), player.name);
            const data = typeof res === "string" ? JSON.parse(res) : res;
            if (data.status === "ok") {
                player.isOp = false;
                showFeedback(`Revoked Operator status from ${player.name}`);
            } else {
                showFeedback(`Failed to de-op: ${data.error || "Unknown error"}`);
            }
        }
        await refreshServerData(activeInstance.value.id);
    } catch (err) {
        showFeedback(`Error: ${err.message || err}`);
    }
};

const playerModalOpen = ref(false);
const playerModalType = ref("kick"); // "kick" | "ban"
const playerModalTarget = ref(null);
const playerModalReason = ref("");
const isSubmittingPlayerAction = ref(false);

const handleKickPlayer = (player) => {
    if (!activeInstance.value || !player) return;
    playerModalTarget.value = player;
    playerModalType.value = "kick";
    playerModalReason.value = "Kicked by operator";
    playerModalOpen.value = true;
};

const handleBanPlayer = (player) => {
    if (!activeInstance.value || !player) return;
    playerModalTarget.value = player;
    playerModalType.value = "ban";
    playerModalReason.value = "Banned by operator";
    playerModalOpen.value = true;
};

const cancelPlayerAction = () => {
    if (isSubmittingPlayerAction.value) return;
    playerModalOpen.value = false;
    playerModalTarget.value = null;
    playerModalReason.value = "";
};

const confirmPlayerAction = async () => {
    if (!activeInstance.value || !playerModalTarget.value || isSubmittingPlayerAction.value) return;
    const player = playerModalTarget.value;
    const actionType = playerModalType.value;
    const defaultReason = actionType === "kick" ? "Kicked by operator" : "Banned by operator";
    const reason = playerModalReason.value.trim() || defaultReason;
    const serverId = activeInstance.value.id.toString();

    isSubmittingPlayerAction.value = true;
    try {
        if (actionType === "kick") {
            if (window.kickPlayer) {
                const res = await window.kickPlayer(serverId, player.name, reason);
                const data = typeof res === "string" ? JSON.parse(res) : res;
                if (data.status === "ok") {
                    showFeedback(`Kicked ${player.name} (${reason})`);
                    if (selectedPlayer.value?.name === player.name) {
                        selectedPlayer.value = null;
                    }
                } else {
                    showFeedback(`Failed to kick: ${data.error || "Unknown error"}`);
                }
            }
        } else {
            if (window.banPlayer) {
                const res = await window.banPlayer(serverId, player.name, reason);
                const data = typeof res === "string" ? JSON.parse(res) : res;
                if (data.status === "ok") {
                    showFeedback(`Banned ${player.name} (${reason})`);
                    if (selectedPlayer.value?.name === player.name) {
                        selectedPlayer.value = null;
                    }
                } else {
                    showFeedback(`Failed to ban: ${data.error || "Unknown error"}`);
                }
            }
        }
        playerModalOpen.value = false;
        playerModalTarget.value = null;
        playerModalReason.value = "";
        await refreshServerData(activeInstance.value.id);
    } catch (err) {
        showFeedback(`Error: ${err.message || err}`);
    } finally {
        isSubmittingPlayerAction.value = false;
    }
};

const handleQuickOp = async () => {
    const name = quickOpName.value.trim();
    if (!name || !activeInstance.value) return;
    try {
        if (window.opPlayer) {
            const res = await window.opPlayer(activeInstance.value.id.toString(), name);
            const data = typeof res === "string" ? JSON.parse(res) : res;
            if (data.status === "ok") {
                showFeedback(`Granted Operator status to ${name}`);
                quickOpName.value = "";
            } else {
                showFeedback(`Error: ${data.error || "Could not OP player"}`);
            }
        }
        await refreshServerData(activeInstance.value.id);
    } catch (err) {
        showFeedback(`Error: ${err.message || err}`);
    }
};

const sendConsoleCmd = async () => {
    const cmd = consoleCommand.value.trim();
    if (!cmd || !activeInstance.value) return;
    try {
        if (window.sendServerCommand) {
            await window.sendServerCommand(activeInstance.value.id.toString(), cmd);
            consoleCommand.value = "";
            setTimeout(() => {
                refreshServerData(activeInstance.value.id);
                scrollToBottom();
            }, 300);
        }
    } catch (err) {
        console.error("Send command error:", err);
    }
};

const fetchServers = async () => {
    isLoading.value = true;
    try {
        const freshData = await window.getServers();

        instances.value = freshData.map((newServer) => {
            const existing = instances.value.find((s) => s.id === newServer.id);

            if (existing) {
                return { ...existing, ...newServer };
            }
            return newServer;
        });

        // Also fetch players for running servers
        for (const server of instances.value) {
            if (server.status?.toUpperCase() === "RUNNING" && window.getServerPlayers) {
                try {
                    const raw = await window.getServerPlayers(server.id.toString());
                    const parsed = typeof raw === "string" ? JSON.parse(raw) : raw;
                    server.players = Array.isArray(parsed) ? parsed : [];
                } catch (_) {}
            }
        }

        error.value = null;
    } catch (err) {
        console.error("Fetch Error:", err);
        error.value = "Failed to synchronize cluster state.";
    } finally {
        isLoading.value = false;
    }
};

const scrollToBottom = async () => {
    await nextTick();
    if (logContainer.value) {
        logContainer.value.scrollTop = logContainer.value.scrollHeight;
    }
};

/**
 * 3. UI Actions
 */

const handleDrop = async (e) => {
    insDragging.value = false;
    const files = e.dataTransfer.files;
    if (files.length > 0) {
        console.log(files);
    }
};

const triggerImport = async () => {
    console.log(
        newInstanceName.value,
        newInstanceVersion.value,
        selectedType.value,
        isOnlineMode.value,
        selectedPath.value,
    );
    try {
        const result = await window.electron.serverImport(
            generateUUID(),
            newInstanceName.value,
            newInstanceVersion.value,
            selectedType.value,
            isOnlineMode.value,
            selectedPath.value,
        );
        if (result && !result.error) {
            isCreating.value = false;
            newInstanceName.value = "";
            await fetchServers();
        }
    } finally {
        isDeploying.value = false;
    }
};

// const [fileHandle] = await window.showOpenFilePicker({
//      types: [{
//        description: 'Server Config (JSON)',
//        accept: { 'application/zip': ['.zip'] },
//      }],
//      multiple: false

const updateSelection = (filePath) => {
    const fileName = filePath.split(/[\\/]/).pop() || "";
    selectedFile.value = {
        name: fileName,
        path: filePath,
    };

    selectedPath.value = filePath;

    console.log("Selected File Object:", selectedFile.value);

    // 4. Automatically set the instance name if it's empty
    if (!newInstanceName.value && fileName) {
        newInstanceName.value = fileName.replace(".zip", "");
    }
};

const browseFiles = async () => {
    try {
        // 1. Open the native OS picker via your working Electron bridge
        const filePath = await window.electron.openZipPicker();

        // 2. If the user didn't cancel and a path was returned, pass it to your selection handler
        if (filePath) {
            updateSelection(filePath);
        }
    } catch (err) {
        // Catch-all for unexpected IPC or OS errors
        console.error("Failed to select file:", err);
    }
};

const handleFileDrop = (e) => {
    isDragging.value = false;
    const file = e.dataTransfer.files[0];

    if (file && file.name.endsWith(".zip")) {
        updateSelection(file);
    }
};

const openServerStats = (server) => {
    activeInstance.value = server;
    currentView.value = "stats";
    serverPlayers.value = server.players || [];
    consoleLogs.value = store.logs[server.id] || [];
    selectedPlayer.value = null;
    actionMessage.value = "";
    startStatsPolling(server.id);
    scrollToBottom();
};

const closeMenu = () => {
    stopStatsPolling();
    currentView.value = "grid";
    activeInstance.value = null;
    selectedPlayer.value = null;
    actionMessage.value = "";
};

const toggleStatus = async (server) => {
    if (!server) return;
    try {
        if (server.status?.toUpperCase() === "RUNNING") {
            server.status = "STOPPING";
            if (window.stopServer) {
                await window.stopServer(server.id.toString());
            }
            server.status = "OFFLINE";
        } else {
            server.status = "STARTING";
            if (window.startServer) {
                await window.startServer(server.id.toString());
            }
            server.status = "RUNNING";
        }
        await fetchServers();
    } catch (err) {
        console.error("Toggle server status error:", err);
        server.status = "ERROR";
    }
};

function generateUUID() {
    return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(
        /[xy]/g,
        function (c) {
            const r = (Math.random() * 16) | 0;
            const v = c === "x" ? r : (r & 0x3) | 0x8;
            return v.toString(16);
        },
    );
}

const deployInstance = async () => {
    if (!isFormValid.value) return;
    isDeploying.value = true;
    try {
        const payload = JSON.stringify({
            id: generateUUID(),
            name: newInstanceName.value,
            type: selectedType.value,
            version: newInstanceVersion.value,
            memory: Number(memoryAlloc.value) || 3,
            port: 25565,
            online: isOnlineMode.value,
        });

        const rawResult = await window.addServer(payload);
        const result = typeof rawResult === "string" ? JSON.parse(rawResult) : rawResult;

        if (result && !result.error) {
            isCreating.value = false;
            newInstanceName.value = "";
            await fetchServers();
        } else if (result?.error) {
            alert(result.error);
        }
    } catch (err) {
        console.error("Provisioning error:", err);
        alert("Failed to provision instance: " + (err.message || err));
    } finally {
        isDeploying.value = false;
    }
};

const deleteModalOpen = ref(false);
const serverToDelete = ref(null);
const isDeleting = ref(false);

const requestDeleteInstance = (server) => {
    activeMenuId.value = null;
    serverToDelete.value = server;
    deleteModalOpen.value = true;
};

const cancelDelete = () => {
    if (isDeleting.value) return;
    deleteModalOpen.value = false;
    serverToDelete.value = null;
};

const confirmDeleteInstance = async () => {
    if (!serverToDelete.value) return;
    const target = serverToDelete.value;
    const targetId = target.id;
    isDeleting.value = true;

    try {
        if (activeInstance.value?.id === targetId) {
            stopStatsPolling();
            activeInstance.value = null;
            selectedPlayer.value = null;
            currentView.value = "grid";
        }

        delete store.logs[targetId];
        store.activeListeners.delete(targetId);
        saveToHistory(targetId, null);

        if (window.deleteServer) {
            await window.deleteServer(targetId.toString());
        }

        instances.value = instances.value.filter((s) => s.id !== targetId);
        await fetchServers();

        showFeedback(`Successfully deleted instance "${target.name}"`);
        deleteModalOpen.value = false;
        serverToDelete.value = null;
    } catch (err) {
        console.error("Delete instance error:", err);
        showFeedback(`Failed to delete instance: ${err.message || err}`);
    } finally {
        isDeleting.value = false;
    }
};

const deleteInstance = (id) => {
    const s = instances.value.find((inst) => inst.id === id) || (activeInstance.value?.id === id ? activeInstance.value : { id, name: "Node #" + id });
    requestDeleteInstance(s);
};

const toggleMenu = (id) =>
    (activeMenuId.value = activeMenuId.value === id ? null : id);
const closeMenus = () => (activeMenuId.value = null);

onMounted(async () => {
    if (instances.value.length > 0) {
        isLoading.value = false;
    }

    await fetchServers();
    window.addEventListener("click", closeMenus);
});

onUnmounted(() => {
    stopStatsPolling();
    window.removeEventListener("click", closeMenus);
});
</script>

<template>
    <div class="dashboard-container">
        <transition name="pop">
            <div
                v-if="isCreating"
                class="modal-overlay"
                @click.self="isCreating = false"
            >
                <div class="modal-glass">
                    <span class="section-label">Initialize Node</span>
                    <h2 class="deploy-instance">Deploy New Instance</h2>

                    <div class="input-stack">
                        <label>Node Identity</label>
                        <input
                            v-model="newInstanceName"
                            class="fancy-input"
                            placeholder="e.g. survival-hub-01"
                            autofocus
                        />
                    </div>

                    <div class="input-stack">
                        <label>Server Software</label>
                        <div class="type-grid">
                            <div
                                v-for="type in serverTypes"
                                :key="type.id"
                                class="type-box"
                                :class="{ active: selectedType === type.id }"
                                @click="selectedType = type.id"
                            >
                                <div class="type-logo">
                                    <component :is="type.logo" :size="24" />
                                </div>
                                <div class="type-info">
                                    <span class="type-name">{{
                                        type.name
                                    }}</span>
                                    <span class="type-desc">{{
                                        type.desc
                                    }}</span>
                                </div>
                            </div>
                        </div>
                    </div>

                    <div class="form-row">
                        <div class="input-stack half">
                            <label>Engine Version</label>
                            <select
                                v-model="newInstanceVersion"
                                class="fancy-input"
                            >
                                <option
                                    v-for="v in availableVersions"
                                    :key="v"
                                    :value="v"
                                >
                                    {{ v }}
                                </option>
                            </select>
                        </div>
                        <div class="input-stack half">
                            <label>RAM Allocation(GB)</label>
                            <select v-model="memoryAlloc" class="fancy-input">
                                <option>2</option>
                                <option>3</option>
                                <option>4</option>
                                <option>8</option>
                            </select>
                        </div>
                    </div>

                    <div
                        class="toggle-stack"
                        @click="isOnlineMode = !isOnlineMode"
                    >
                        <div class="toggle-info">
                            <span class="toggle-label">Online Mode</span>
                            <span class="toggle-desc"
                                >Require official Mojang authentication</span
                            >
                        </div>
                        <div
                            class="toggle-switch"
                            :class="{ on: isOnlineMode }"
                        ></div>
                    </div>

                    <div class="modal-actions">
                        <button
                            class="btn-secondary"
                            @click="isCreating = false"
                        >
                            Cancel
                        </button>
                        <button
                            class="primary-btn sparkle-hover"
                            :disabled="!isFormValid"
                            @click="deployInstance"
                        >
                            {{
                                isDeploying
                                    ? "Provisioning..."
                                    : "Start Deployment"
                            }}
                        </button>
                    </div>
                </div>
            </div>
        </transition>

        <div v-if="currentView === 'grid'" class="view-layer">
            <header class="dashboard-header">
                <div class="title-section">
                    <h1>Local Instances</h1>
                    <p>Project Beacon / <span>localhost </span></p>
                </div>
                <div class="header-actions" style="display: flex; gap: 12px">
                    <button class="btn-secondary" @click="showImport">
                        <Download :size="15" class="btn-inline-icon" /> Import Server
                    </button>
                    <button
                        class="primary-btn sparkle-hover"
                        @click="isCreating = true"
                    >
                        <Plus :size="15" class="btn-inline-icon" /> New Instance
                    </button>
                </div>
            </header>

            <div v-if="isLoading" class="status-msg">
                Synchronizing Cluster Data...
            </div>
            <div v-else-if="error" class="status-msg error">{{ error }}</div>

            <div v-else class="server-grid">
                <div
                    v-for="server in instances"
                    :key="server.id"
                    class="card server-card"
                    :class="server.status?.toLowerCase()"
                    @click="openServerStats(server)"
                >
                    <div class="card-inner">
                        <div class="card-head">
                            <div class="status-pill">
                                <span class="dot"></span> {{ server.status }}
                            </div>

                            <div class="context-container" @click.stop>
                                <button
                                    class="btn-dots"
                                    @click="toggleMenu(server.id)"
                                    title="Options"
                                >
                                    <MoreVertical :size="15" />
                                </button>
                                <transition name="pop">
                                    <div
                                        v-if="activeMenuId === server.id"
                                        class="dropdown-menu"
                                    >
                                        <button
                                            class="menu-item delete"
                                            @click="requestDeleteInstance(server)"
                                        >
                                            <Trash2 :size="14" class="icon" /> Delete
                                            Instance
                                        </button>
                                    </div>
                                </transition>
                            </div>

                            <div class="badge-group">
                                <span class="version-badge">{{
                                    server.version
                                }}</span>
                                <span class="type-badge">{{
                                    server.type
                                }}</span>
                            </div>
                        </div>

                        <div class="server-info">
                            <div class="server-title-row">
                                <h3>{{ server.name }}</h3>
                                <span v-if="server.status === 'RUNNING'" class="player-count-badge">
                                    <Users :size="12" class="player-badge-icon" /> {{ server.players?.length || 0 }} online
                                </span>
                            </div>
                            <code>{{ server.address || ('localhost:' + (server.port || 25565)) }}</code>
                        </div>

                        <div class="metrics">
                            <div class="metric-labels">
                                <span>CPU Usage</span
                                ><span>{{ server.cpu_usage || 0 }}%</span>
                            </div>
                            <div class="progress-bg">
                                <div
                                    class="progress-fill"
                                    :style="{
                                        width: (server.cpu_usage || 0) + '%',
                                    }"
                                ></div>
                            </div>
                        </div>

                        <div class="card-actions" @click.stop>
                            <button
                                class="btn-action"
                                :class="server.status?.toLowerCase()"
                                @click="toggleStatus(server)"
                            >
                                {{
                                    server.status === "RUNNING"
                                        ? "STOP"
                                        : "START"
                                }}
                            </button>
                            <button
                                class="btn-secondary"
                                @click="openServerStats(server)"
                            >
                                {{ server.status === "RUNNING" ? "PLAYERS & CONSOLE" : "CONSOLE" }}
                            </button>
                        </div>
                    </div>
                </div>

                <div class="card create-card" @click="isCreating = true">
                    <div class="plus-icon"><Plus :size="32" /></div>
                    <div class="create-text">
                        <h3>Deploy Instance</h3>
                        <p href="#" class="import-text" @click="showImport">
                            Initialize a new node
                        </p>
                    </div>
                </div>
            </div>
        </div>

        <div
            v-if="currentView === 'stats'"
            class="view-layer stats-view-container"
        >
            <header class="dashboard-header">
                <div class="title-section">
                    <h1>Instance Control</h1>
                    <p>
                        Project Beacon /
                        <span class="active-node">{{
                            activeInstance?.name
                        }}</span>
                        <span v-if="activeInstance?.status === 'RUNNING'" class="pill-running">RUNNING</span>
                        <span v-else class="pill-offline">OFFLINE</span>
                    </p>
                </div>

                <div class="header-actions" style="display: flex; gap: 10px; align-items: center;">
                    <button
                        class="btn-danger-outline"
                        @click="requestDeleteInstance(activeInstance)"
                        title="Delete this server instance"
                    >
                        <Trash2 :size="15" /> Delete Server
                    </button>
                    <button class="btn-secondary" @click="closeMenu">
                        <ArrowLeft :size="15" /> Back to Dashboard
                    </button>
                </div>
            </header>

            <div class="stats-layout">
                <div class="terminal-column">
                    <div class="terminal-title-bar">
                        <span class="section-label">Live System Output</span>
                        <button class="btn-clear-term" @click="consoleLogs = []" title="Clear Console">Clear</button>
                    </div>
                    <div class="log-terminal" ref="logContainer">
                        <div
                            v-for="(line, index) in logs"
                            :key="index"
                            class="log-line"
                        >
                            <span class="line-number">{{ index + 1 }}</span>
                            <span class="content">{{ line }}</span>
                        </div>
                        <div v-if="logs.length === 0" class="log-placeholder">
                            Awaiting logs from Minecraft server process...
                        </div>
                    </div>
                    <div class="terminal-cmd-bar">
                        <span class="cmd-prompt">&gt;</span>
                        <input
                            v-model="consoleCommand"
                            @keyup.enter="sendConsoleCmd"
                            placeholder="Type a server command (e.g. op <player>, kick <player>, say <msg>)..."
                            class="cmd-input"
                        />
                        <button
                            class="cmd-send-btn"
                            @click="sendConsoleCmd"
                            :disabled="!consoleCommand.trim()"
                        >
                            <Send :size="14" /> Send
                        </button>
                    </div>
                </div>

                <div class="player-sidebar">
                    <div class="sticky-sidebar-content">
                        <div class="sidebar-header-row">
                            <span class="section-label"
                                >Players Online ({{
                                    serverPlayers.length
                                }})</span
                            >
                            <span v-if="activeInstance?.status === 'RUNNING'" class="live-pulse">LIVE</span>
                        </div>

                        <transition name="pop">
                            <div v-if="actionMessage" class="action-feedback">
                                {{ actionMessage }}
                            </div>
                        </transition>

                        <div v-if="activeInstance?.status === 'RUNNING'" class="quick-op-wrapper">
                            <input
                                v-model="quickOpName"
                                placeholder="Player to OP..."
                                class="quick-op-field"
                                @keyup.enter="handleQuickOp"
                            />
                            <button
                                class="btn-quick-op"
                                @click="handleQuickOp"
                                :disabled="!quickOpName.trim()"
                            >
                                <ShieldCheck :size="14" /> OP
                            </button>
                        </div>

                        <div class="player-list">
                            <div
                                v-for="player in serverPlayers"
                                :key="player.uuid || player.name"
                                class="player-row"
                                :class="{ active: selectedPlayer?.name === player.name, 'is-op': player.isOp }"
                                @click="selectedPlayer = player"
                            >
                                <div class="player-avatar-box">
                                    <img
                                        :src="`https://mc-heads.net/avatar/${player.name}/48`"
                                        class="player-avatar-img"
                                        :alt="player.name"
                                        @error="(e) => { e.target.src = `https://minotar.net/avatar/${player.name}/48`; }"
                                    />
                                    <span v-if="player.isOp" class="crown-badge" title="Server Operator">
                                        <Crown :size="11" />
                                    </span>
                                </div>
                                <div class="player-meta">
                                    <div class="player-title-row">
                                        <span class="player-name">{{ player.name }}</span>
                                        <span v-if="player.isOp" class="op-badge">OP</span>
                                    </div>
                                    <span class="player-ping">{{ player.ping || 20 }}ms ping</span>
                                </div>
                                <span class="player-arrow">›</span>
                            </div>

                            <div
                                v-if="!serverPlayers.length"
                                class="empty-state-mini"
                            >
                                <div class="empty-icon"><Users :size="36" /></div>
                                <div class="empty-title">No players online</div>
                                <p class="empty-desc" v-if="activeInstance?.status === 'RUNNING'">
                                    Connect in Minecraft at <code>{{ activeInstance?.address || ('localhost:' + (activeInstance?.port || 25565)) }}</code>
                                </p>
                                <p class="empty-desc" v-else>
                                    Start server to allow players to join.
                                </p>
                            </div>
                        </div>

                        <transition name="pop">
                            <div
                                v-if="selectedPlayer"
                                class="player-detail-card"
                            >
                                <div class="detail-header">
                                    <div class="detail-profile">
                                        <img
                                            :src="`https://mc-heads.net/avatar/${selectedPlayer.name}/64`"
                                            class="detail-avatar-img"
                                            :alt="selectedPlayer.name"
                                            @error="(e) => { e.target.src = `https://minotar.net/avatar/${selectedPlayer.name}/64`; }"
                                        />
                                        <div class="detail-info">
                                            <div class="detail-name-line">
                                                <h3>{{ selectedPlayer.name }}</h3>
                                                <span v-if="selectedPlayer.isOp" class="op-badge">OP</span>
                                            </div>
                                            <code class="detail-uuid">{{ selectedPlayer.uuid }}</code>
                                        </div>
                                    </div>
                                    <button
                                        @click="selectedPlayer = null"
                                        class="close-btn"
                                    >
                                        <X :size="16" />
                                    </button>
                                </div>

                                <div class="player-actions-group">
                                    <button
                                        v-if="!selectedPlayer.isOp"
                                        class="action-btn give-op"
                                        @click="handleOpPlayer(selectedPlayer)"
                                    >
                                        <ShieldCheck :size="15" /> Give Operator
                                    </button>
                                    <button
                                        v-else
                                        class="action-btn revoke-op"
                                        @click="handleDeopPlayer(selectedPlayer)"
                                    >
                                        <ShieldAlert :size="15" /> Revoke Operator
                                    </button>

                                    <button
                                        class="action-btn kick-player"
                                        @click="handleKickPlayer(selectedPlayer)"
                                    >
                                        <LogOut :size="15" /> Kick Player
                                    </button>

                                    <button
                                        class="action-btn ban-player"
                                        @click="handleBanPlayer(selectedPlayer)"
                                    >
                                        <Ban :size="15" /> Ban Player
                                    </button>
                                </div>
                            </div>
                        </transition>
                    </div>
                </div>
            </div>
        </div>

        <div
            v-if="currentView === 'import'"
            class="view-layer import-view-container modal-overlay"
        >
            <transition name="pop">
                <div class="modal-glass import-body">
                    <header class="modal-header">
                        <h2>Import New Server</h2>
                    </header>
                    <button
                        @click="closeMenu"
                        class="btn-secondary import-view-back"
                    >
                        Back
                    </button>
                    <div class="import-pane">
                        <div
                            class="drop-zone"
                            :class="{ 'is-dragging': isDragging }"
                            @dragover.prevent="isDragging = true"
                            @dragleave.prevent="isDragging = false"
                            @drop.prevent="handleFileDrop"
                        >
                            <div class="drop-content">
                                <FileText :size="36" class="upload-icon" />
                                <p v-if="!selectedFile">
                                    Drag server file here or
                                    <span @click="browseFiles">browse</span>
                                </p>
                                <p v-else>
                                    <strong>Selected:</strong>
                                    {{ selectedFile.name }}
                                </p>
                            </div>
                        </div>

                        <div class="form-group input-stack">
                            <label>Server Alias</label>
                            <input
                                v-model="newInstanceName"
                                class="fancy-input"
                            />
                        </div>

                        <div class="form-row">
                            <div class="input-stack half">
                                <label>Engine Version</label>
                                <select
                                    v-model="newInstanceVersion"
                                    class="fancy-input"
                                >
                                    <option
                                        v-for="v in availableVersions"
                                        :key="v"
                                        :value="v"
                                    >
                                        {{ v }}
                                    </option>
                                </select>
                                <div class="input-stack">
                                    <label>Server Software</label>
                                    <div class="type-grid">
                                        <div
                                            v-for="type in serverTypes"
                                            :key="type.id"
                                            class="type-box"
                                            :class="{
                                                active:
                                                    selectedType === type.id,
                                            }"
                                            @click="selectedType = type.id"
                                        >
                                            <div class="type-logo">
                                                <component :is="type.logo" :size="24" />
                                            </div>
                                            <div class="type-info">
                                                <span class="type-name">{{
                                                    type.name
                                                }}</span>
                                                <span class="type-desc">{{
                                                    type.desc
                                                }}</span>
                                            </div>
                                        </div>
                                    </div>
                                    <div
                                        class="toggle-stack"
                                        @click="isOnlineMode = !isOnlineMode"
                                    >
                                        <div class="toggle-info">
                                            <span class="toggle-label"
                                                >Online Mode</span
                                            >
                                            <span class="toggle-desc"
                                                >Require official Mojang
                                                authentication</span
                                            >
                                        </div>
                                        <div
                                            class="toggle-switch"
                                            :class="{ on: isOnlineMode }"
                                        ></div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                    <button class="primary-btn" @click="triggerImport()">
                        Import Instance
                    </button>
                </div>
            </transition>
        </div>

        <!-- Delete Confirmation Modal -->
        <transition name="pop">
            <div
                v-if="deleteModalOpen"
                class="modal-overlay"
                @click.self="cancelDelete"
            >
                <div class="modal-glass">
                    <span class="section-label">Server Management</span>
                    <h2 class="deploy-instance">Delete Instance</h2>

                    <div class="input-stack">
                        <label>Node Identity</label>
                        <div class="fancy-input server-delete-preview">
                            <div class="delete-instance-info">
                                <span class="delete-instance-name">{{ serverToDelete?.name }}</span>
                                <span class="delete-instance-meta">
                                    {{ serverToDelete?.type }} {{ serverToDelete?.version }} • Port {{ serverToDelete?.port || 25565 }}
                                </span>
                            </div>
                            <span
                                v-if="serverToDelete?.status === 'RUNNING'"
                                class="pill-running"
                            >
                                RUNNING
                            </span>
                            <span v-else class="pill-offline">
                                OFFLINE
                            </span>
                        </div>
                    </div>

                    <div class="input-stack">
                        <label>Confirmation</label>
                        <p class="delete-desc">
                            Are you sure you want to permanently delete this instance? All server files, world data, player inventories, and configuration files will be permanently erased. This action cannot be undone.
                        </p>
                    </div>

                    <div
                        v-if="serverToDelete?.status === 'RUNNING'"
                        class="toggle-stack running-notice"
                    >
                        <div class="toggle-info">
                            <span class="toggle-label">Active Process</span>
                            <span class="toggle-desc">The server is currently running and will be stopped before deletion.</span>
                        </div>
                    </div>

                    <div class="modal-actions">
                        <button
                            class="btn-secondary"
                            @click="cancelDelete"
                            :disabled="isDeleting"
                        >
                            Cancel
                        </button>
                        <button
                            class="primary-btn danger-btn"
                            @click="confirmDeleteInstance"
                            :disabled="isDeleting"
                        >
                            <Trash2 :size="16" />
                            <span>{{ isDeleting ? "Deleting..." : "Delete Instance" }}</span>
                        </button>
                    </div>
                </div>
            </div>
        </transition>

        <!-- Player Moderation (Kick / Ban) Modal -->
        <transition name="pop">
            <div
                v-if="playerModalOpen"
                class="modal-overlay"
                @click.self="cancelPlayerAction"
            >
                <div class="modal-glass">
                    <span class="section-label">Player Moderation</span>
                    <h2 class="deploy-instance">
                        {{ playerModalType === 'kick' ? 'Kick Player' : 'Ban Player' }}
                    </h2>

                    <div class="input-stack">
                        <label>Target Player</label>
                        <div class="fancy-input player-modal-target-preview">
                            <div class="player-modal-identity">
                                <img
                                    :src="`https://mc-heads.net/avatar/${playerModalTarget?.name}/32`"
                                    class="player-modal-avatar"
                                    :alt="playerModalTarget?.name"
                                    @error="(e) => { e.target.src = `https://minotar.net/avatar/${playerModalTarget?.name}/32`; }"
                                />
                                <div class="player-modal-info">
                                    <span class="player-modal-name">{{ playerModalTarget?.name }}</span>
                                    <code class="player-modal-uuid">{{ playerModalTarget?.uuid }}</code>
                                </div>
                            </div>
                            <span v-if="playerModalTarget?.isOp" class="op-badge">OP</span>
                        </div>
                    </div>

                    <div class="input-stack">
                        <label>Reason</label>
                        <input
                            v-model="playerModalReason"
                            class="fancy-input"
                            :placeholder="playerModalType === 'kick' ? 'e.g. Inactivity, rule violation' : 'e.g. Griefing, hacking, unauthorized actions'"
                            @keyup.enter="confirmPlayerAction"
                            autofocus
                        />
                    </div>

                    <div class="toggle-stack running-notice">
                        <div class="toggle-info">
                            <span class="toggle-label">{{ playerModalType === 'kick' ? 'Server Disconnect' : 'Permanent Ban' }}</span>
                            <span class="toggle-desc">
                                {{ playerModalType === 'kick'
                                    ? 'The player will be immediately kicked from the server and may reconnect later.'
                                    : 'The player will be immediately kicked and added to banned-players.json.' }}
                            </span>
                        </div>
                    </div>

                    <div class="modal-actions">
                        <button
                            class="btn-secondary"
                            @click="cancelPlayerAction"
                            :disabled="isSubmittingPlayerAction"
                        >
                            Cancel
                        </button>
                        <button
                            class="primary-btn danger-btn"
                            @click="confirmPlayerAction"
                            :disabled="isSubmittingPlayerAction"
                        >
                            <LogOut v-if="playerModalType === 'kick'" :size="16" />
                            <Ban v-else :size="16" />
                            <span>
                                {{ isSubmittingPlayerAction
                                    ? (playerModalType === 'kick' ? 'Kicking...' : 'Banning...')
                                    : (playerModalType === 'kick' ? 'Kick ' + playerModalTarget?.name : 'Ban ' + playerModalTarget?.name) }}
                            </span>
                        </button>
                    </div>
                </div>
            </div>
        </transition>
    </div>
</template>

<style scoped>
.import-view-back {
    margin: 10px;
}

.import-view-container btn-secondary {
    display: flex;
    align-self: left;
}

.import-view-container primary-btn {
    margin: 10px;
}

.form-group {
    padding: 20px 0px 20px 0px;
    display: flex;
}

.form-group label {
    color: rgba(255, 255, 255, 0.9);
    padding: 10px;
}

.import-body h2 {
    color: rgba(240, 240, 240, 0.9);
}

.drop-zone span {
    color: #409eff;
    text-decoration: underline;
    cursor: pointer;
    font-weight: 600;
}

.drop-zone {
    border: 2px dashed rgba(255, 255, 255, 0.3);
    border-radius: 12px;
    padding: 8px;
    text-align: center;
    transition: all 0.3s ease;
    position: relative;
    background: rgba(255, 255, 255, 0.05);
}

drop-zone.is-dragging {
    background: rgba(64, 158, 255, 0.05);
}

.dashboard-container {
    padding: 40px;
    max-width: 1400px;
    margin: 0 auto;
    color: #1d1d1f;
    font-family:
        -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    min-height: 100vh;
}
.dashboard-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-end;
    margin-bottom: 40px;
    flex-shrink: 0;
}
h1 {
    font-size: 2.5rem;
    font-weight: 900;
    letter-spacing: -1.5px;
    margin: 0;
}
.title-section p {
    color: #86868b;
    margin-top: 4px;
}
.title-section span {
    color: #0071e3;
    font-weight: 700;
}
.active-node {
    color: #32d74b !important;
}

.server-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
    gap: 24px;
}

.context-container {
    position: relative;
}
.btn-dots {
    background: none;
    border: none;
    font-size: 1.2rem;
    color: #86868b;
    cursor: pointer;
    padding: 5px 10px;
    border-radius: 50%;
    transition: 0.2s;
}
.btn-dots:hover {
    background: #f5f5f7;
    color: #1d1d1f;
}
.dropdown-menu {
    position: absolute;
    top: 100%;
    right: 0;
    background: white;
    border-radius: 16px;
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.1);
    border: 1px solid rgba(0, 0, 0, 0.05);
    z-index: 50;
    width: 180px;
    overflow: hidden;
    margin-top: 8px;
}
.menu-item {
    width: 100%;
    border: none;
    background: none;
    padding: 12px 16px;
    text-align: left;
    font-weight: 700;
    font-size: 0.85rem;
    cursor: pointer;
}
.menu-item:hover {
    background: #f5f5f7;
}
.menu-item.delete {
    color: #ff3b30;
}
.menu-item.delete:hover {
    background: #fff1f0;
}

.import-text {
    text-decoration: underline;
}

.card {
    background: rgba(255, 255, 255, 0.05);
    backdrop-filter: blur(25px) saturate(160%);
    -webkit-backdrop-filter: blur(25px) saturate(160%);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-top: 1px solid rgba(255, 255, 255, 0.4);
    border-left: 1px solid rgba(255, 255, 255, 0.2);
    border-radius: 30px;
    padding: 30px;
    box-shadow:
        0 20px 40px rgba(0, 0, 0, 0.3),
        inset 0 0 15px rgba(255, 255, 255, 0.05);
    transition: all 0.4s cubic-bezier(0.165, 0.84, 0.44, 1);
    cursor: pointer;
    overflow: hidden;
    position: relative;
}
.card:hover {
    transform: translateY(-8px);
    box-shadow: 0 30px 60px rgba(0, 0, 0, 0.06);
}

.card-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    position: relative;
}
.status-pill {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 0.75rem;
    font-weight: 800;
    text-transform: uppercase;
    background: #f5f5f7;
    padding: 6px 14px;
    border-radius: 20px;
}
.online .status-pill,
.running .status-pill {
    color: #166534;
    background: #dcfce7;
}
.offline .status-pill,
.exited .status-pill {
    color: #1d1d1f;
    background: #f5f5f7;
}

.dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #86868b;
}
.online .dot,
.running .dot {
    background: #22c55e;
    box-shadow: 0 0 10px #22c55e;
}

.badge-group {
    display: flex;
    gap: 8px;
}
.version-badge,
.type-badge {
    font-size: 0.65rem;
    font-weight: 800;
    color: #86868b;
    background: #f5f5f7;
    padding: 4px 10px;
    border-radius: 8px;
}

.server-info h3 {
    font-size: 1.4rem;
    font-weight: 800;
    margin: 20px 0 5px;
}
.server-info code {
    color: #0071e3;
    font-weight: 600;
    font-size: 0.85rem;
}

.metrics {
    margin: 25px 0;
}
.metric-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.7rem;
    font-weight: 700;
    color: #86868b;
    margin-bottom: 8px;
}

.progress-bg {
    height: 6px;
    background: #f5f5f7;
    border-radius: 10px;
    overflow: hidden;
}
.progress-fill {
    height: 100%;
    background: #0071e3;
    transition: width 0.8s ease;
}

.card-actions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-top: 25px;
}
.btn-action {
    border: none;
    padding: 12px;
    border-radius: 14px;
    font-weight: 800;
    cursor: pointer;
}
.btn-action.exited,
.btn-action.offline {
    background: #0071e3;
    color: white;
}
.btn-action.running,
.btn-action.online {
    background: #ff3b30;
    color: white;
}
.btn-secondary {
    background: #f5f5f7;
    border: none;
    font-weight: 700;
    border-radius: 14px;
    cursor: pointer;
    padding: 12px;
}

.create-card {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    border: 2px dashed #d2d2d7;
    background: transparent;
}
.plus-icon {
    font-size: 2rem;
    color: #d2d2d7;
    margin-bottom: 10px;
}
.create-text {
    text-align: center;
}
.create-text h3 {
    margin: 0;
    font-size: 1.1rem;
}
.create-text p {
    font-size: 0.8rem;
    color: #86868b;
}

.modal-overlay {
    position: fixed;
    inset: 0;
    background: rgba(16, 16, 16, 0.38);
    backdrop-filter: blur(20px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
}

.deploy-instance {
    color: #ffffff;
}

.modal-glass {
    background: rgba(25, 28, 32, 0.51);
    padding: 40px;
    border-radius: 40px;
    width: 100%;
    max-width: 600px;
    box-shadow: 0 40px 100px rgba(0, 0, 0, 0.1);
}
.input-stack {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin: 20px 0;
}
.input-stack label {
    font-size: 0.7rem;
    font-weight: 800;
    color: #e5e5e8;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}
.fancy-input {
    background: rgba(7, 6, 6, 0.18);
    border: none;
    padding: 15px;
    border-radius: 16px;
    font-size: 1rem;
    font-weight: 600;
    color: #fffffc;
    outline: none;
}
.form-row {
    display: flex;
    gap: 15px;
}
.form-row .half {
    flex: 1;
}

.type-grid {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 12px;
    margin-top: 5px;
}
.type-box {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px;
    background: rgba(9, 8, 8, 0.17);
    border-radius: 18px;
    border: 2px solid transparent;
    cursor: pointer;
    transition: 0.2s;
}
.type-box.active {
    border-color: #0071e3;
    background: rgba(16, 15, 15, 0.17);
    box-shadow: 0 10px 20px rgba(0, 113, 227, 0.08);
}
.type-logo {
    font-size: 1.5rem;
}
.type-info {
    display: flex;
    flex-direction: column;
}
.type-name {
    font-weight: 800;
    font-size: 0.9rem;
    color: #fffcfc;
}
.type-desc {
    font-size: 0.7rem;
    color: #86868b;
    font-weight: 500;
}

.toggle-stack {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: rgba(23, 22, 22, 0.16);
    padding: 15px 20px;
    border-radius: 20px;
    cursor: pointer;
    margin-top: 10px;
}
.toggle-info {
    display: flex;
    flex-direction: column;
}
.toggle-label {
    font-size: 0.9rem;
    font-weight: 700;
}
.toggle-desc {
    font-size: 0.7rem;
    color: #86868b;
}
.toggle-switch {
    width: 44px;
    height: 24px;
    background: #d2d2d7;
    border-radius: 20px;
    position: relative;
    transition: 0.3s;
}
.toggle-switch::after {
    content: "";
    position: absolute;
    top: 2px;
    left: 2px;
    width: 20px;
    height: 20px;
    background: white;
    border-radius: 50%;
    transition: 0.3s;
}
.toggle-switch.on {
    background: #32d74b;
}
.toggle-switch.on::after {
    transform: translateX(20px);
}

.modal-actions {
    display: flex;
    gap: 12px;
    justify-content: flex-end;
    margin-top: 30px;
}

.stats-view-container {
    display: flex;
    flex-direction: column;
    height: 100%;
}
.stats-layout {
    display: grid;
    grid-template-columns: 1fr 340px;
    gap: 30px;
    flex-grow: 1;
    min-height: 0;
}
.terminal-column {
    display: flex;
    flex-direction: column;
    gap: 12px;
}
.terminal-title-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.btn-clear-term {
    background: transparent;
    border: 1px solid #484f58;
    color: #8b949e;
    padding: 3px 10px;
    border-radius: 8px;
    font-size: 0.75rem;
    cursor: pointer;
    transition: 0.2s;
}
.btn-clear-term:hover {
    color: #f0f6fc;
    border-color: #8b949e;
}

.log-terminal {
    background: #1d1d1f;
    border-radius: 24px;
    padding: 25px;
    height: calc(100vh - 300px);
    overflow-y: auto;
    overflow-x: hidden;
    color: #32d74b;
    font-family: "JetBrains Mono", monospace;
    font-size: 0.85rem;
    border: 1px solid #30363d;
    box-shadow: inset 0 0 20px rgba(0, 0, 0, 0.5);
    flex-grow: 1;
}
.log-line {
    display: flex;
    gap: 15px;
    margin-bottom: 4px;
    opacity: 0.9;
    word-break: break-all;
    white-space: pre-wrap;
}
.line-number {
    color: #484f58;
    min-width: 35px;
    text-align: right;
    user-select: none;
}
.log-placeholder {
    color: #484f58;
    text-align: center;
    margin-top: 100px;
    font-style: italic;
}

.terminal-cmd-bar {
    display: flex;
    align-items: center;
    gap: 10px;
    background: #161b22;
    border: 1px solid #30363d;
    border-radius: 14px;
    padding: 6px 14px;
}
.cmd-prompt {
    font-family: 'JetBrains Mono', monospace;
    font-weight: 800;
    color: #32d74b;
    user-select: none;
}
.cmd-input {
    flex: 1;
    background: transparent;
    border: none;
    color: #f0f6fc;
    font-family: 'JetBrains Mono', monospace;
    font-size: 0.85rem;
    outline: none;
}
.cmd-input::placeholder {
    color: #484f58;
}
.cmd-send-btn {
    background: #238636;
    color: white;
    border: none;
    border-radius: 8px;
    padding: 6px 14px;
    font-weight: 700;
    font-size: 0.8rem;
    cursor: pointer;
    transition: 0.2s;
}
.cmd-send-btn:hover:not(:disabled) {
    background: #2ea043;
}
.cmd-send-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
}

.player-sidebar {
    position: relative;
}
.sticky-sidebar-content {
    position: sticky;
    top: 20px;
    display: flex;
    flex-direction: column;
    gap: 16px;
}

.sidebar-header-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.live-pulse {
    background: rgba(50, 215, 75, 0.2);
    color: #32d74b;
    border: 1px solid #32d74b;
    font-size: 0.65rem;
    font-weight: 800;
    padding: 2px 8px;
    border-radius: 10px;
    letter-spacing: 1px;
}

.action-feedback {
    background: #1c2128;
    border: 1px solid #388bfd;
    color: #58a6ff;
    padding: 10px 14px;
    border-radius: 12px;
    font-size: 0.8rem;
    font-weight: 600;
}

.quick-op-wrapper {
    display: flex;
    gap: 8px;
}
.quick-op-field {
    flex: 1;
    background: #1c2128;
    border: 1px solid #30363d;
    border-radius: 12px;
    padding: 8px 12px;
    color: #f0f6fc;
    font-size: 0.8rem;
    outline: none;
}
.quick-op-field:focus {
    border-color: #58a6ff;
}
.btn-quick-op {
    background: #f1c40f;
    color: #1a1a1a;
    border: none;
    border-radius: 12px;
    padding: 8px 12px;
    font-weight: 800;
    font-size: 0.78rem;
    cursor: pointer;
    transition: 0.2s;
    white-space: nowrap;
}
.btn-quick-op:hover:not(:disabled) {
    background: #f39c12;
}
.btn-quick-op:disabled {
    opacity: 0.4;
    cursor: not-allowed;
}

.section-label {
    font-family: "JetBrains Mono", monospace;
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 1.5px;
    color: rgba(255, 255, 255, 0.5);
    padding-left: 10px;
}

.player-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    max-height: 380px;
    overflow-y: auto;
    padding-right: 5px;
}
.player-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 14px;
    background: white;
    border-radius: 16px;
    cursor: pointer;
    border: 2px solid transparent;
    transition: 0.2s;
}
.player-row:hover {
    border-color: #0071e3;
    transform: translateX(4px);
}
.player-row.is-op {
    border-left: 4px solid #f1c40f;
}
.player-row.active {
    border-color: #0071e3;
    background: #f0f7ff;
}

.player-avatar-box {
    position: relative;
    width: 36px;
    height: 36px;
    flex-shrink: 0;
}
.player-avatar-img {
    width: 36px;
    height: 36px;
    border-radius: 8px;
    object-fit: cover;
    box-shadow: 0 2px 6px rgba(0,0,0,0.15);
    image-rendering: pixelated;
}
.crown-badge {
    position: absolute;
    top: -6px;
    right: -6px;
    font-size: 0.8rem;
    filter: drop-shadow(0 2px 4px rgba(0,0,0,0.3));
}

.player-meta {
    flex: 1;
    min-width: 0;
}
.player-title-row {
    display: flex;
    align-items: center;
    gap: 6px;
}
.player-name {
    font-weight: 700;
    font-size: 0.9rem;
    color: #1d1d1f;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.op-badge {
    background: #f1c40f;
    color: #1a1a1a;
    font-size: 0.65rem;
    font-weight: 900;
    padding: 1px 6px;
    border-radius: 6px;
    letter-spacing: 0.5px;
}
.player-ping {
    font-size: 0.7rem;
    color: #86868b;
    display: block;
}
.player-arrow {
    font-size: 1.2rem;
    color: #86868b;
    font-weight: 700;
}

.empty-state-mini {
    padding: 25px 15px;
    text-align: center;
    background: rgba(255, 255, 255, 0.05);
    border-radius: 18px;
    border: 1px dashed rgba(255, 255, 255, 0.15);
}
.empty-icon {
    font-size: 2rem;
    margin-bottom: 6px;
}
.empty-title {
    font-weight: 700;
    font-size: 0.9rem;
    color: #f0f6fc;
}
.empty-desc {
    font-size: 0.75rem;
    color: #86868b;
    margin-top: 6px;
}
.empty-desc code {
    color: #32d74b;
    background: rgba(0,0,0,0.3);
    padding: 2px 6px;
    border-radius: 6px;
}

.player-detail-card {
    background: white;
    padding: 18px;
    border-radius: 20px;
    box-shadow: 0 15px 35px rgba(0, 0, 0, 0.1);
    border: 1px solid #f5f5f7;
}
.detail-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    margin-bottom: 12px;
}
.detail-profile {
    display: flex;
    align-items: center;
    gap: 12px;
}
.detail-avatar-img {
    width: 44px;
    height: 44px;
    border-radius: 10px;
    image-rendering: pixelated;
    box-shadow: 0 3px 8px rgba(0,0,0,0.15);
}
.detail-info {
    display: flex;
    flex-direction: column;
    gap: 2px;
}
.detail-name-line {
    display: flex;
    align-items: center;
    gap: 8px;
}
.detail-name-line h3 {
    margin: 0;
    font-size: 1.05rem;
    color: #1d1d1f;
}
.detail-uuid {
    font-size: 0.65rem;
    color: #86868b;
    word-break: break-all;
    max-width: 170px;
    display: block;
}
.close-btn {
    background: none;
    border: none;
    font-size: 1.4rem;
    cursor: pointer;
    color: #86868b;
    line-height: 1;
}

.player-actions-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-top: 12px;
}
.action-btn {
    padding: 9px 12px;
    border-radius: 10px;
    border: none;
    font-weight: 700;
    font-size: 0.8rem;
    cursor: pointer;
    transition: 0.2s;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
}
.action-btn.give-op {
    background: #f1c40f;
    color: #1a1a1a;
}
.action-btn.give-op:hover {
    background: #f39c12;
}
.action-btn.revoke-op {
    background: #6c757d;
    color: white;
}
.action-btn.revoke-op:hover {
    background: #5a6268;
}
.action-btn.kick-player {
    background: #ff9800;
    color: white;
}
.action-btn.kick-player:hover {
    background: #e68a00;
}
.action-btn.ban-player {
    background: #dc3545;
    color: white;
}
.action-btn.ban-player:hover {
    background: #c82333;
}

.server-title-row {
    display: flex;
    align-items: center;
    gap: 10px;
}
.player-count-badge {
    background: rgba(50, 215, 75, 0.15);
    color: #32d74b;
    border: 1px solid rgba(50, 215, 75, 0.3);
    padding: 2px 8px;
    border-radius: 12px;
    font-size: 0.72rem;
    font-weight: 700;
}
.pill-running {
    background: #32d74b;
    color: #0b2210;
    font-size: 0.65rem;
    font-weight: 800;
    padding: 2px 8px;
    border-radius: 10px;
    margin-left: 8px;
    vertical-align: middle;
}
.pill-offline {
    background: #86868b;
    color: white;
    font-size: 0.65rem;
    font-weight: 800;
    padding: 2px 8px;
    border-radius: 10px;
    margin-left: 8px;
    vertical-align: middle;
}

.pop-enter-active {
    transition: all 0.3s cubic-bezier(0.165, 0.84, 0.44, 1);
}
.pop-enter-from {
    opacity: 0;
    transform: scale(0.95);
}
.status-msg {
    padding: 60px;
    text-align: center;
    color: #86868b;
    font-weight: 700;
}
.primary-btn {
    background: #0071e3;
    color: white;
    border: none;
    padding: 14px 24px;
    border-radius: 18px;
    font-weight: 700;
    cursor: pointer;
}
.primary-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

/* --- Delete Confirmation Modal & Danger Zone Styles --- */
.server-delete-preview {
    display: flex;
    align-items: center;
    justify-content: space-between;
}
.delete-instance-info {
    display: flex;
    flex-direction: column;
    gap: 4px;
}
.delete-instance-name {
    font-size: 1rem;
    font-weight: 700;
    color: #fffffc;
}
.delete-instance-meta {
    font-size: 0.75rem;
    color: #86868b;
    font-family: "JetBrains Mono", monospace;
}
.delete-desc {
    font-size: 0.85rem;
    line-height: 1.5;
    color: #86868b;
    margin: 0;
}
.running-notice {
    cursor: default;
    margin-top: 15px;
}
.primary-btn.danger-btn {
    background: #ff3b30;
}
.primary-btn.danger-btn:hover:not(:disabled) {
    background: #e03228;
}
.btn-danger-outline {
    background: #f5f5f7;
    color: #ff3b30;
    border: none;
    font-weight: 700;
    border-radius: 14px;
    cursor: pointer;
    padding: 12px 16px;
    display: flex;
    align-items: center;
    gap: 8px;
    transition: 0.2s;
}
.btn-danger-outline:hover {
    background: #fff1f0;
}

/* Button & Badge icon alignments */
.btn-inline-icon {
    display: inline-flex;
    vertical-align: middle;
    margin-right: 4px;
}
.player-badge-icon {
    display: inline-flex;
    vertical-align: middle;
    margin-right: 4px;
}
.action-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
}
.btn-quick-op {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
}
.cmd-send-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
}
.crown-badge {
    color: #ffd60a;
    display: inline-flex;
    align-items: center;
    justify-content: center;
}

.player-modal-target-preview {
    display: flex;
    align-items: center;
    justify-content: space-between;
}
.player-modal-identity {
    display: flex;
    align-items: center;
    gap: 12px;
}
.player-modal-avatar {
    width: 32px;
    height: 32px;
    border-radius: 8px;
    image-rendering: pixelated;
}
.player-modal-info {
    display: flex;
    flex-direction: column;
    gap: 2px;
}
.player-modal-name {
    font-size: 0.95rem;
    font-weight: 700;
    color: #ffffff;
}
.player-modal-uuid {
    font-size: 0.72rem;
    color: #86868b;
    font-family: "JetBrains Mono", monospace;
}
</style>
