<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue';
import { Globe, Users, Copy, Check, Radio, Wifi, ShieldCheck, Loader2 } from 'lucide-vue-next';

const isHosting = ref(false);
const isUpdating = ref(false);
const errorMsg = ref('');
const roomCode = ref('');
const publicEndpoint = ref('');
const targetPort = ref(25565);
const activeConnections = ref([]);
const copied = ref(false);

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
          publicEndpoint.value = res.public_endpoint || 'NAT Reflexive Discovery';
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
      publicEndpoint.value = '';
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
        publicEndpoint.value = status.public_endpoint || '';
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
  await fetchStatus();
  pollTimer = setInterval(fetchStatus, 3000);
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
          <span class="type-badge">P2P MESH</span>
          <span class="version-badge">PORT {{ targetPort }}</span>
        </div>
      </div>

      <div class="server-info">
        <h3>Direct P2P Multiplayer Host</h3>
        <code>{{ isHosting ? publicEndpoint : 'NAT Reflexive Discovery' }}</code>
      </div>

      <div v-if="isHosting" class="lobby-code-section">
        <label class="section-label">LOBBY INVITE CODE</label>
        <div class="code-box">
          <span class="code-text">{{ roomCode }}</span>
          <button class="btn-copy" @click="copyRoomCode">
            <Check v-if="copied" :size="14" class="icon-success" />
            <Copy v-else :size="14" />
            {{ copied ? 'COPIED' : 'COPY' }}
          </button>
        </div>
        <p class="code-hint">Share this code with friends to let them join from their Beacon app.</p>
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
          <span class="peer-endpoint">{{ peer.endpoint || 'Hole-punched' }}</span>
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

.lobby-code-section {
  margin: 20px 0 10px;
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid rgba(255, 255, 255, 0.08);
  padding: 16px 20px;
  border-radius: 18px;
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
  font-size: 1.6rem;
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
