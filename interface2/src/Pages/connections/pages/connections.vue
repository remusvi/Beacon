<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue';
import { Globe, Users, Copy, Check, Radio, Wifi, ShieldCheck, Loader2, Server, AlertCircle } from 'lucide-vue-next';

const isHosting = ref(false);
const isUpdating = ref(false);
const errorMsg = ref('');
const roomCode = ref('');
const lanRoomCode = ref('');
const publicEndpoint = ref('');
const localEndpoint = ref('');
const targetPort = ref(25565);
const upnpActive = ref(false);
const serverListening = ref(false);
const hasRunningServer = ref(false);
const runningServerName = ref('');
const activeConnections = ref([]);
const copied = ref(false);
const copiedLan = ref(false);
const copiedEndpoint = ref(false);
const copiedLocalEndpoint = ref(false);

async function copyRoomCode() {
  if (!roomCode.value) return;
  try {
    await navigator.clipboard.writeText(roomCode.value);
    copied.value = true;
    setTimeout(() => { copied.value = false; }, 2000);
  } catch (err) {
    console.error('Failed to copy:', err);
  }
}

async function copyLanRoomCode() {
  if (!lanRoomCode.value) return;
  try {
    await navigator.clipboard.writeText(lanRoomCode.value);
    copiedLan.value = true;
    setTimeout(() => { copiedLan.value = false; }, 2000);
  } catch (err) {
    console.error('Failed to copy LAN code:', err);
  }
}

async function copyDirectEndpoint() {
  const ep = publicEndpoint.value || localEndpoint.value;
  if (!ep) return;
  try {
    await navigator.clipboard.writeText(ep);
    copiedEndpoint.value = true;
    setTimeout(() => { copiedEndpoint.value = false; }, 2000);
  } catch (err) {
    console.error('Failed to copy endpoint:', err);
  }
}

async function copyLocalEndpoint() {
  if (!localEndpoint.value) return;
  try {
    await navigator.clipboard.writeText(localEndpoint.value);
    copiedLocalEndpoint.value = true;
    setTimeout(() => { copiedLocalEndpoint.value = false; }, 2000);
  } catch (err) {
    console.error('Failed to copy local endpoint:', err);
  }
}

async function fetchServers() {
  try {
    if (window.getServers) {
      const resStr = await window.getServers();
      const servers = typeof resStr === 'string' ? JSON.parse(resStr) : resStr;
      if (Array.isArray(servers)) {
        const running = servers.find(s => s.status === 'RUNNING');
        if (running) {
          hasRunningServer.value = true;
          runningServerName.value = running.name || running.id || 'Minecraft Server';
          if (running.port) {
            targetPort.value = Number(running.port);
          }
        } else {
          hasRunningServer.value = false;
          runningServerName.value = '';
        }
      }
    }
  } catch (err) {
    console.warn('Could not query servers:', err);
  }
}

async function toggleHost() {
  isUpdating.value = true;
  errorMsg.value = '';

  try {
    if (!isHosting.value) {
      if (window.startP2PHost) {
        const resStr = await window.startP2PHost("default", targetPort.value);
        const res = typeof resStr === 'string' ? JSON.parse(resStr) : resStr;
        if (res.error) {
          errorMsg.value = res.error;
        } else {
          isHosting.value = true;
          roomCode.value = res.room_code || '';
          lanRoomCode.value = res.lan_room_code || '';
          publicEndpoint.value = res.public_endpoint || 'NAT Reflexive Discovery';
          localEndpoint.value = res.local_endpoint || '';
          upnpActive.value = !!res.upnp_active;
          serverListening.value = !!res.server_listening;
          if (res.target_port) targetPort.value = res.target_port;
        }
      } else {
        errorMsg.value = "Native P2P host bridge not available.";
      }
    } else {
      if (window.stopP2PHost) {
        await window.stopP2PHost();
      }
      isHosting.value = false;
      roomCode.value = '';
      lanRoomCode.value = '';
      publicEndpoint.value = '';
      localEndpoint.value = '';
      upnpActive.value = false;
      serverListening.value = false;
      activeConnections.value = [];
    }
  } catch (err) {
    console.error("P2P Host Toggle Error:", err);
    errorMsg.value = err.message || "Failed to toggle P2P host.";
  } finally {
    isUpdating.value = false;
  }
}

async function fetchStatus() {
  try {
    if (window.getP2PStatus) {
      const resStr = await window.getP2PStatus();
      const status = typeof resStr === 'string' ? JSON.parse(resStr) : resStr;
      if (status.role === 'host' && status.status === 'active') {
        isHosting.value = true;
        roomCode.value = status.room_code || '';
        lanRoomCode.value = status.lan_room_code || '';
        publicEndpoint.value = status.public_endpoint || '';
        localEndpoint.value = status.local_endpoint || '';
        upnpActive.value = !!status.upnp_active;
        serverListening.value = !!status.server_listening;
        if (status.target_port) targetPort.value = status.target_port;
        activeConnections.value = status.active_connections || [];
      } else if (status.role !== 'host') {
        isHosting.value = false;
      }
    }
  } catch (err) {
    // Silently ignore
  }
}

let pollTimer;
onMounted(async () => {
  await fetchServers();
  await fetchStatus();
  pollTimer = setInterval(async () => {
    await fetchStatus();
    await fetchServers();
  }, 3000);
});

onUnmounted(() => {
  if (pollTimer) clearInterval(pollTimer);
});
</script>

<template>
  <div class="connections-page">
    <header class="page-header">
      <div class="title-section">
        <h1>Peer-to-Peer Network</h1>
        <p>Host direct multiplayer lobbies or bridge connection nodes without port forwarding.</p>
      </div>
    </header>

    <div class="card status-card" :class="{ 'online': isHosting }">
      <div class="card-head">
        <div class="status-pill">
          <span class="dot"></span>
          <span>{{ isHosting ? 'Lobby Active' : 'Offline' }}</span>
        </div>
        <div class="badge-group">
          <span v-if="isHosting" :class="['type-badge', upnpActive ? 'upnp-on' : 'upnp-off']">
            {{ upnpActive ? 'UPNP FORWARDED' : 'LAN / DIRECT' }}
          </span>
          <span class="type-badge">P2P MESH</span>
          <span class="version-badge">PORT {{ targetPort }}</span>
        </div>
      </div>

      <div class="server-info">
        <h3>Direct P2P Multiplayer Host</h3>
        <code>{{ isHosting ? (publicEndpoint + (localEndpoint && localEndpoint !== publicEndpoint ? ' • LAN: ' + localEndpoint : '')) : 'NAT Reflexive Discovery' }}</code>
      </div>

      <!-- Server Running / Warning Status -->
      <div v-if="hasRunningServer && serverListening" class="server-status-banner running">
        <Server :size="14" class="icon-server" />
        <span>Minecraft Server is active & listening: <strong>{{ runningServerName }}</strong> (Port {{ targetPort }})</span>
      </div>
      <div v-else-if="hasRunningServer && !serverListening" class="server-status-banner idle">
        <Loader2 :size="14" class="icon-warn spin" />
        <span>Server <strong>{{ runningServerName }}</strong> is running, but port {{ targetPort }} is not listening yet. Please wait a moment.</span>
      </div>
      <div v-else class="server-status-banner idle">
        <AlertCircle :size="14" class="icon-warn" />
        <span>No server is currently running. Start your server in the Servers tab so friends can join your world.</span>
      </div>

      <!-- Port Configuration (when not hosting) -->
      <div v-if="!isHosting" class="port-config-row">
        <label class="port-label">HOST TARGET PORT:</label>
        <div class="port-input-wrapper">
          <input
            v-model.number="targetPort"
            type="number"
            min="1024"
            max="65535"
            class="port-input"
            placeholder="25565"
          />
          <span class="port-hint">(Default 25565, or enter your "Open to LAN" port)</span>
        </div>
      </div>

      <div v-if="isHosting" class="lobby-code-section">
        <div class="code-grid">
          <div class="code-card">
            <label class="section-label">INTERNET INVITE CODE</label>
            <div class="code-box">
              <span class="code-text">{{ roomCode }}</span>
              <button class="btn-copy" @click="copyRoomCode">
                <Check v-if="copied" :size="14" class="icon-success" />
                <Copy v-else :size="14" />
                {{ copied ? 'COPIED' : 'COPY CODE' }}
              </button>
            </div>
            <p class="code-hint">Share with friends connecting over the internet.</p>
          </div>

          <div v-if="lanRoomCode" class="code-card">
            <label class="section-label">LAN INVITE CODE (SAME WI-FI)</label>
            <div class="code-box">
              <span class="code-text">{{ lanRoomCode }}</span>
              <button class="btn-copy" @click="copyLanRoomCode">
                <Check v-if="copiedLan" :size="14" class="icon-success" />
                <Copy v-else :size="14" />
                {{ copiedLan ? 'COPIED' : 'COPY LAN' }}
              </button>
            </div>
            <p class="code-hint">Share with friends on the same home Wi-Fi or local network.</p>
          </div>
        </div>

        <!-- Direct Connection Endpoints for IP:Port -->
        <div class="direct-endpoint-box">
          <div class="direct-row">
            <span class="direct-label">PUBLIC IP:</span>
            <code class="direct-val">{{ publicEndpoint }}</code>
            <button class="btn-copy-mini" @click="copyDirectEndpoint">
              <Check v-if="copiedEndpoint" :size="12" class="icon-success" />
              <Copy v-else :size="12" />
              {{ copiedEndpoint ? 'COPIED' : 'COPY' }}
            </button>
          </div>
          <div v-if="localEndpoint && localEndpoint !== publicEndpoint" class="direct-row">
            <span class="direct-label">LAN IP:</span>
            <code class="direct-val">{{ localEndpoint }}</code>
            <button class="btn-copy-mini" @click="copyLocalEndpoint">
              <Check v-if="copiedLocalEndpoint" :size="12" class="icon-success" />
              <Copy v-else :size="12" />
              {{ copiedLocalEndpoint ? 'COPIED' : 'COPY' }}
            </button>
          </div>
        </div>

        <div v-if="!upnpActive" class="upnp-notice">
          <AlertCircle :size="13" class="icon-warn" />
          <span>UPnP port forwarding not active on router. Friends on the same Wi-Fi can join instantly using the LAN Code. For friends over the internet, ensure port {{ targetPort }} is forwarded in your router settings.</span>
        </div>
      </div>

      <div v-if="errorMsg" class="error-banner">
        {{ errorMsg }}
      </div>

      <div class="card-actions">
        <button
          @click="toggleHost"
          :disabled="isUpdating"
          :class="['btn-action', isHosting ? 'running' : 'offline']"
        >
          <template v-if="isUpdating">
            <Loader2 :size="16" class="spin" />
            <span>Processing...</span>
          </template>
          <template v-else>
            {{ isHosting ? 'STOP HOSTING' : 'HOST LOBBY' }}
          </template>
        </button>
      </div>
    </div>

    <!-- Active Connected Peers Section -->
    <div class="card peers-card">
      <div class="card-head">
        <h3>Connected Peers</h3>
        <span class="peers-count">{{ activeConnections.length }} active</span>
      </div>

      <div v-if="activeConnections.length > 0" class="peers-list">
        <div v-for="(peer, index) in activeConnections" :key="index" class="peer-row">
          <div class="peer-info">
            <Globe :size="16" class="peer-icon" />
            <span class="peer-id">{{ peer.id || 'Peer ' + (index + 1) }}</span>
          </div>
          <span class="peer-endpoint">{{ peer.endpoint || 'Tunnel Established' }}</span>
          <span class="status-pill mini online">CONNECTED</span>
        </div>
      </div>
      <div v-else class="empty-peers">
        <p>{{ isHosting ? 'Waiting for friends to join your lobby...' : 'Start hosting above to accept player connections.' }}</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.connections-page {
  padding: 40px;
  max-width: 900px;
  margin: 0 auto;
  color: #1d1d1f;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.page-header {
  margin-bottom: 8px;
}

h1 {
  font-size: 2.2rem;
  font-weight: 900;
  letter-spacing: -1px;
  margin: 0;
  color: #ffffff;
}

.title-section p {
  color: #86868b;
  margin-top: 6px;
  font-size: 0.95rem;
}

/* Beacon Liquid Glass Card */
.card {
  background: rgba(255, 255, 255, 0.05);
  backdrop-filter: blur(25px) saturate(160%);
  -webkit-backdrop-filter: blur(25px) saturate(160%);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-top: 1px solid rgba(255, 255, 255, 0.3);
  border-left: 1px solid rgba(255, 255, 255, 0.15);
  border-radius: 28px;
  padding: 30px;
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.3), inset 0 0 15px rgba(255, 255, 255, 0.03);
  transition: all 0.3s ease;
}

.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.card-head h3 {
  margin: 0;
  font-size: 1.15rem;
  font-weight: 800;
  color: #ffffff;
}

.status-pill {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 0.75rem;
  font-weight: 800;
  text-transform: uppercase;
  background: rgba(255, 255, 255, 0.1);
  color: #86868b;
  padding: 6px 14px;
  border-radius: 20px;
}

.online .status-pill {
  color: #166534;
  background: #dcfce7;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #86868b;
}

.online .dot {
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
  background: rgba(255, 255, 255, 0.08);
  padding: 4px 10px;
  border-radius: 8px;
}

.type-badge.upnp-on {
  color: #22c55e;
  background: rgba(34, 197, 94, 0.15);
  border: 1px solid rgba(34, 197, 94, 0.3);
}

.type-badge.upnp-off {
  color: #f59e0b;
  background: rgba(245, 158, 11, 0.15);
  border: 1px solid rgba(245, 158, 11, 0.3);
}

.server-info h3 {
  font-size: 1.35rem;
  font-weight: 800;
  margin: 18px 0 4px;
  color: #ffffff;
}

.server-info code {
  color: #0071e3;
  font-weight: 600;
  font-size: 0.85rem;
}

.server-status-banner {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 14px;
  padding: 10px 14px;
  border-radius: 12px;
  font-size: 0.82rem;
  font-weight: 500;
}

.server-status-banner.running {
  background: rgba(34, 197, 94, 0.1);
  border: 1px solid rgba(34, 197, 94, 0.25);
  color: #86efac;
}

.server-status-banner.idle {
  background: rgba(245, 158, 11, 0.1);
  border: 1px solid rgba(245, 158, 11, 0.25);
  color: #fcd34d;
}

.port-config-row {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 14px;
}

.port-label {
  font-size: 0.72rem;
  font-weight: 800;
  color: #86868b;
  letter-spacing: 0.5px;
}

.port-input-wrapper {
  display: flex;
  align-items: center;
  gap: 12px;
}

.port-input {
  width: 140px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.12);
  padding: 8px 14px;
  border-radius: 12px;
  color: #ffffff;
  font-family: 'SF Mono', Monaco, Consolas, monospace;
  font-size: 0.95rem;
  font-weight: 700;
  outline: none;
  transition: all 0.2s;
}

.port-input:focus {
  border-color: #0071e3;
  box-shadow: 0 0 0 3px rgba(0, 113, 227, 0.3);
}

.port-hint {
  font-size: 0.78rem;
  color: #86868b;
}

.icon-server {
  color: #22c55e;
  flex-shrink: 0;
}

.icon-warn {
  color: #f59e0b;
  flex-shrink: 0;
}

.lobby-code-section {
  margin: 20px 0 10px;
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid rgba(255, 255, 255, 0.08);
  padding: 16px 20px;
  border-radius: 18px;
}

.code-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 16px;
  margin-bottom: 12px;
}

.code-card {
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 14px;
  padding: 12px 16px;
}

.section-label {
  display: block;
  font-size: 0.7rem;
  font-weight: 800;
  color: #86868b;
  margin-bottom: 6px;
  letter-spacing: 0.5px;
}

.code-box {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.code-text {
  font-family: 'SF Mono', Monaco, Consolas, monospace;
  font-size: 1.45rem;
  font-weight: 800;
  letter-spacing: 2px;
  color: #00abf0;
}

.btn-copy {
  display: flex;
  align-items: center;
  gap: 6px;
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.15);
  color: #ffffff;
  padding: 6px 14px;
  border-radius: 10px;
  font-weight: 700;
  font-size: 0.75rem;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-copy:hover {
  background: rgba(255, 255, 255, 0.2);
}

.code-hint {
  font-size: 0.8rem;
  color: #86868b;
  margin: 8px 0 0;
}

.direct-endpoint-box {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 12px;
  padding-top: 10px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}

.direct-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.direct-label {
  font-size: 0.68rem;
  font-weight: 800;
  color: #86868b;
  letter-spacing: 0.5px;
  min-width: 65px;
}

.direct-val {
  font-family: monospace;
  font-size: 0.82rem;
  color: #93c5fd;
  flex: 1;
}

.btn-copy-mini {
  display: flex;
  align-items: center;
  gap: 4px;
  background: rgba(255, 255, 255, 0.08);
  border: 1px solid rgba(255, 255, 255, 0.12);
  color: #ffffff;
  padding: 4px 8px;
  border-radius: 6px;
  font-weight: 700;
  font-size: 0.7rem;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-copy-mini:hover {
  background: rgba(255, 255, 255, 0.18);
}

.upnp-notice {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin-top: 12px;
  padding: 10px 14px;
  background: rgba(245, 158, 11, 0.08);
  border: 1px solid rgba(245, 158, 11, 0.2);
  border-radius: 12px;
  font-size: 0.78rem;
  color: #fcd34d;
  line-height: 1.4;
}

.card-actions {
  margin-top: 24px;
}

.btn-action {
  width: 100%;
  border: none;
  padding: 14px;
  border-radius: 16px;
  font-weight: 800;
  font-size: 0.95rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: all 0.2s;
}

.btn-action.offline {
  background: #0071e3;
  color: white;
}

.btn-action.offline:hover {
  background: #0077ed;
}

.btn-action.running {
  background: #ff3b30;
  color: white;
}

.btn-action.running:hover {
  background: #e02e24;
}

.btn-action:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.error-banner {
  margin-top: 15px;
  background: rgba(255, 59, 48, 0.15);
  border: 1px solid rgba(255, 59, 48, 0.3);
  color: #ff6961;
  padding: 10px 14px;
  border-radius: 12px;
  font-size: 0.85rem;
}

/* Peers Card */
.peers-count {
  font-size: 0.75rem;
  font-weight: 800;
  color: #86868b;
  background: rgba(255, 255, 255, 0.08);
  padding: 4px 10px;
  border-radius: 8px;
}

.peers-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-top: 16px;
}

.peer-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.peer-info {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #ffffff;
  font-weight: 600;
  font-size: 0.9rem;
}

.peer-icon {
  color: #00abf0;
}

.peer-endpoint {
  font-family: monospace;
  font-size: 0.8rem;
  color: #86868b;
}

.status-pill.mini {
  padding: 4px 10px;
  font-size: 0.7rem;
}

.empty-peers {
  text-align: center;
  padding: 24px;
  color: #86868b;
  font-size: 0.9rem;
}

.icon-success {
  color: #22c55e;
}

.spin {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}
</style>