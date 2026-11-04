<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { Copy, Check, Radio, Loader2, ArrowRight, AlertCircle, Wifi, ShieldCheck } from 'lucide-vue-next';

// --- State Management ---
const roomCodeInput = ref('');
const connectionStatus = ref('idle'); // idle | connecting | active | error
const errorMessage = ref('');
const localProxyEndpoint = ref('127.0.0.1:25565');
const connectedRoomCode = ref('');
const copied = ref(false);

// --- Computed UI State ---
const isConnected = computed(() => connectionStatus.value === 'active');
const isConnecting = computed(() => connectionStatus.value === 'connecting');

async function copyLocalEndpoint() {
  try {
    await navigator.clipboard.writeText(localProxyEndpoint.value);
    copied.value = true;
    setTimeout(() => { copied.value = false; }, 2000);
  } catch (err) {
    console.error('Failed to copy:', err);
  }
}

async function handleToggleConnection() {
  if (isConnected.value) {
    return disconnect();
  }

  let code = roomCodeInput.value.trim();
  if (!code) return;

  errorMessage.value = '';
  connectionStatus.value = 'connecting';

  try {
    if (window.joinP2PSession) {
      const resStr = await window.joinP2PSession(code, 25565);
      const res = typeof resStr === 'string' ? JSON.parse(resStr) : resStr;

      if (res.error) {
        errorMessage.value = res.error;
        connectionStatus.value = 'error';
      } else {
        connectionStatus.value = 'active';
        connectedRoomCode.value = res.room_code || code;
        localProxyEndpoint.value = res.local_endpoint || '127.0.0.1:25565';
      }
    } else {
      errorMessage.value = "Native P2P client bridge not available.";
      connectionStatus.value = 'error';
    }
  } catch (err) {
    console.error("P2P Connect Failed:", err);
    errorMessage.value = err.message || "Failed to establish P2P connection.";
    connectionStatus.value = 'error';
  }
}

async function disconnect() {
  try {
    if (window.leaveP2PSession) {
      await window.leaveP2PSession();
    }
  } catch (e) {
    console.error("Disconnect error:", e);
  } finally {
    connectionStatus.value = 'idle';
    connectedRoomCode.value = '';
    errorMessage.value = '';
  }
}

async function syncStatus() {
  try {
    if (window.getP2PStatus) {
      const resStr = await window.getP2PStatus();
      const status = typeof resStr === 'string' ? JSON.parse(resStr) : resStr;
      if (status.role === 'client' && status.status === 'active') {
        connectionStatus.value = 'active';
        connectedRoomCode.value = status.room_code;
        localProxyEndpoint.value = '127.0.0.1:' + (status.local_proxy_port || 25565);
        if (status.error) {
          errorMessage.value = status.error;
        }
      } else if (status.role !== 'client' && isConnected.value) {
        connectionStatus.value = 'idle';
      }
    }
  } catch (e) {}
}

let syncTimer;
onMounted(async () => {
  await syncStatus();
  syncTimer = setInterval(syncStatus, 3000);
});

onUnmounted(() => {
  if (syncTimer) clearInterval(syncTimer);
});
</script>

<template>
  <div class="connect-page">
    <header class="page-header">
      <div class="title-section">
        <h1>Connect to Lobby</h1>
        <p>Join a friend's hosted Minecraft server directly over P2P mesh.</p>
      </div>
    </header>

    <div class="card" :class="{ 'online': isConnected }">
      <div class="card-head">
        <div class="status-pill">
          <span class="dot"></span>
          <span>{{ isConnected ? 'Connected' : (isConnecting ? 'Bridging...' : 'Offline') }}</span>
        </div>
        <div class="badge-group">
          <span class="type-badge">P2P CLIENT</span>
          <span class="version-badge">{{ isConnected ? 'BRIDGE ACTIVE' : 'READY' }}</span>
        </div>
      </div>

      <div class="server-info">
        <h3>Join Remote Session</h3>
        <code>{{ isConnected ? 'Tunnel established to ' + connectedRoomCode : 'Enter a 4-character invite code or direct IP address below' }}</code>
      </div>

      <div class="input-stack">
        <label>FRIEND'S LOBBY CODE OR IP</label>
        <div class="input-row">
          <input
            v-model="roomCodeInput"
            placeholder="e.g. BCN-4821 or 192.168.1.100:25565"
            class="fancy-input"
            :disabled="isConnected || isConnecting"
            @keyup.enter="handleToggleConnection"
          />
          <button
            @click="handleToggleConnection"
            :disabled="isConnecting || (!roomCodeInput && !isConnected)"
            :class="['btn-action-inline', isConnected ? 'running' : 'offline']"
          >
            <template v-if="isConnecting">
              <Loader2 :size="16" class="spin" />
            </template>
            <template v-else>
              {{ isConnected ? 'LEAVE' : 'CONNECT' }}
            </template>
          </button>
        </div>
        <p class="input-hint">Tip: Works with 4-digit codes (e.g. 4821), BCN-XXXX codes, or direct IP:port.</p>
      </div>

      <!-- Connection Diagnostic Error Banner -->
      <div v-if="errorMessage" class="error-diagnostic-banner">
        <div class="diag-header">
          <AlertCircle :size="16" class="icon-error" />
          <span>Remote Host Connection Warning</span>
        </div>
        <p class="diag-text">{{ errorMessage }}</p>
        <div class="diag-hints">
          <span class="hint-title">Diagnostic Checklist:</span>
          <ul>
            <li>Ensure the host has actively started their Minecraft server.</li>
            <li>If playing on the same local Wi-Fi or LAN, connect directly using the host's LAN IP.</li>
            <li>If playing over the internet, verify UPnP is enabled on the host router or forward port 25565.</li>
          </ul>
        </div>
      </div>

      <!-- Connection Success Box -->
      <div v-if="isConnected" class="success-panel">
        <div class="success-head">
          <span class="success-title">Local Bridge Endpoint</span>
          <span class="status-pill mini online">Direct Proxy Ready</span>
        </div>

        <div class="address-box">
          <span class="address-text">{{ localProxyEndpoint }}</span>
          <button class="btn-copy" @click="copyLocalEndpoint">
            <Check v-if="copied" :size="14" class="icon-success" />
            <Copy v-else :size="14" />
            {{ copied ? 'COPIED' : 'COPY ADDRESS' }}
          </button>
        </div>

        <div class="steps-box">
          <div class="step-item">
            <span class="step-badge">1</span>
            <span>Open standard Minecraft &rarr; <strong>Multiplayer</strong></span>
          </div>
          <div class="step-item">
            <span class="step-badge">2</span>
            <span>Click <strong>Direct Connection</strong></span>
          </div>
          <div class="step-item">
            <span class="step-badge">3</span>
            <span>Paste <code>{{ localProxyEndpoint }}</code> and Join Server!</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.connect-page {
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

/* Input Stack */
.input-stack {
  margin-top: 24px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.input-stack label {
  font-size: 0.75rem;
  font-weight: 800;
  color: #86868b;
  letter-spacing: 0.5px;
}

.input-row {
  display: flex;
  gap: 12px;
}

.fancy-input {
  flex: 1;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid rgba(255, 255, 255, 0.12);
  padding: 14px 18px;
  border-radius: 14px;
  color: #ffffff;
  font-size: 1rem;
  font-weight: 600;
  outline: none;
  transition: all 0.2s;
}

.fancy-input:focus {
  border-color: #0071e3;
  box-shadow: 0 0 0 3px rgba(0, 113, 227, 0.3);
}

.fancy-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-action-inline {
  border: none;
  padding: 0 28px;
  border-radius: 14px;
  font-weight: 800;
  font-size: 0.9rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: all 0.2s;
}

.btn-action-inline.offline {
  background: #0071e3;
  color: white;
}

.btn-action-inline.offline:hover {
  background: #0077ed;
}

.btn-action-inline.running {
  background: #ff3b30;
  color: white;
}

.btn-action-inline.running:hover {
  background: #e02e24;
}

.btn-action-inline:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.input-hint {
  color: #86868b;
  font-size: 0.75rem;
  margin: 2px 0 0;
}

/* Diagnostic Error Banner */
.error-diagnostic-banner {
  margin-top: 20px;
  background: rgba(239, 68, 68, 0.12);
  border: 1px solid rgba(239, 68, 68, 0.3);
  border-radius: 16px;
  padding: 16px 20px;
  color: #fca5a5;
  font-size: 0.85rem;
}

.diag-header {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 800;
  color: #ef4444;
  margin-bottom: 6px;
  font-size: 0.9rem;
}

.icon-error {
  color: #ef4444;
  flex-shrink: 0;
}

.diag-text {
  margin: 0 0 10px;
  color: #fecaca;
  line-height: 1.4;
  font-family: monospace;
  font-size: 0.82rem;
  background: rgba(0, 0, 0, 0.25);
  padding: 8px 12px;
  border-radius: 8px;
}

.diag-hints {
  font-size: 0.8rem;
  color: #d1d5db;
}

.hint-title {
  font-weight: 700;
  color: #f3f4f6;
  display: block;
  margin-bottom: 4px;
}

.diag-hints ul {
  margin: 0;
  padding-left: 20px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

/* Success Panel */
.success-panel {
  margin-top: 24px;
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid rgba(34, 197, 94, 0.2);
  border-radius: 20px;
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.success-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.success-title {
  font-size: 0.95rem;
  font-weight: 800;
  color: #ffffff;
}

.address-box {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: rgba(255, 255, 255, 0.05);
  padding: 12px 18px;
  border-radius: 14px;
  border: 1px solid rgba(255, 255, 255, 0.08);
}

.address-text {
  font-family: 'SF Mono', Monaco, Consolas, monospace;
  font-size: 1.25rem;
  font-weight: 800;
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

.steps-box {
  display: flex;
  flex-direction: column;
  gap: 8px;
  background: rgba(255, 255, 255, 0.02);
  padding: 12px 16px;
  border-radius: 14px;
  font-size: 0.85rem;
  color: #c7c7cc;
}

.step-item {
  display: flex;
  align-items: center;
  gap: 10px;
}

.step-badge {
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #0071e3;
  color: white;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.7rem;
  font-weight: 800;
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