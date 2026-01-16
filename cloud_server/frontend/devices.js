// 全局变量
let devices = [];
let deviceStatus = {};

// API 基础地址（前后端分离时，API通过nginx代理到后端）
const API_BASE = '';

function authHeaders() {
    if (typeof getAuthHeaders === 'function') {
        return getAuthHeaders();
    }
    const token = localStorage.getItem('authToken');
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    // 需要先检查是否已登录
    if (typeof requireAuth === 'function') {
        requireAuth().then(() => {
            loadDevices();
            startPolling();
            log('系统初始化完成');
        });
    } else {
        loadDevices();
        startPolling();
        log('系统初始化完成');
    }
});

// 日志函数
function log(message, type = 'info') {
    const statusBar = document.getElementById('statusBar');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    statusBar.textContent = `[${timestamp}] ${emoji} ${message}`;
    console.log(`[${timestamp}] ${message}`);
}

// 从服务器加载设备列表
async function loadDevices() {
    try {
        const response = await fetch(`${API_BASE}/api/devices/list`, {
            headers: {
                ...authHeaders()
            }
        });
        if (response.ok) {
            const result = await response.json();
            if (result.success) {
                devices = result.devices.map(device => ({
                    id: device.deviceId,
                    name: device.deviceName || device.deviceId,
                    addedAt: device.addedAt ? new Date(device.addedAt).getTime() : Date.now()
                }));
                console.log('已加载设备列表:', devices);
                renderDevices();
            }
        } else {
            log('加载设备列表失败', 'error');
        }
    } catch (error) {
        console.error('加载设备列表错误:', error);
        log('加载设备列表失败: ' + error.message, 'error');
    }
}

// 添加设备
async function addDevice() {
    const deviceIdInput = document.getElementById('newDeviceId');
    const deviceNameInput = document.getElementById('deviceName');
    
    let deviceId = deviceIdInput.value.trim().toUpperCase();
    const deviceName = deviceNameInput.value.trim() || deviceId;
    
    if (!deviceId) {
        log('请输入设备ID或MAC地址', 'error');
        return;
    }
    
    // 去掉可能的分隔符
    deviceId = deviceId.replace(/[-:]/g, '');
    
    // 验证是否为十六进制
    if (!/^[0-9A-F]+$/.test(deviceId)) {
        log('设备ID格式错误，请输入十六进制MAC地址（6位或12位）', 'error');
        return;
    }
    
    // 根据长度验证
    if (deviceId.length === 6) {
        log(`识别为短设备码: ${deviceId}`, 'info');
    } else if (deviceId.length === 12) {
        log(`识别为完整MAC: ${deviceId}`, 'info');
    } else {
        log('设备ID格式错误，请输入6位或12位的MAC地址', 'error');
        return;
    }
    
    try {
        const response = await fetch(`${API_BASE}/api/devices/add`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                ...authHeaders()
            },
            body: JSON.stringify({
                deviceId: deviceId,
                deviceName: deviceName
            })
        });
        
        const result = await response.json();
        
        if (result.success) {
            log(`设备 ${deviceName} 添加成功`, 'success');
            // 清空输入
            deviceIdInput.value = '';
            deviceNameInput.value = '';
            // 重新加载设备列表
            await loadDevices();
        } else {
            log(result.error || '添加设备失败', 'error');
        }
    } catch (error) {
        console.error('添加设备错误:', error);
        log('添加设备失败: ' + error.message, 'error');
    }
}

// 删除设备
async function removeDevice(deviceId) {
    if (!confirm(`确定要删除设备 ${deviceId} 吗？`)) {
        return;
    }
    
    try {
        const response = await fetch(`${API_BASE}/api/devices/${deviceId}`, {
            method: 'DELETE',
            headers: {
                ...authHeaders()
            }
        });
        
        const result = await response.json();
        
        if (result.success) {
            log('设备已删除', 'success');
            delete deviceStatus[deviceId];
            // 重新加载设备列表
            await loadDevices();
        } else {
            log(result.error || '删除设备失败', 'error');
        }
    } catch (error) {
        console.error('删除设备错误:', error);
        log('删除设备失败: ' + error.message, 'error');
    }
}

// 渲染设备列表
function renderDevices() {
    const container = document.getElementById('devicesContainer');
    
    if (devices.length === 0) {
        container.innerHTML = `
            <div class="empty-state">
                <h3>📭 还没有设备</h3>
                <p>点击上方添加设备按钮，输入ESP32的设备ID来添加设备</p>
            </div>
        `;
        return;
    }
    
    container.innerHTML = '<div class="devices-grid"></div>';
    const grid = container.querySelector('.devices-grid');
    
    devices.forEach(device => {
        const status = deviceStatus[device.id] || {};
        const isOnline = status.online && (Date.now() - status.lastSeen < 60000);
        
        const card = document.createElement('div');
        card.className = 'device-card';
        card.onclick = () => openDevice(device.id);
        
        card.innerHTML = `
            <div class="device-status">
                <span class="status-dot ${isOnline ? 'status-online' : 'status-offline'}"></span>
                <span style="color: ${isOnline ? '#28a745' : '#dc3545'}">
                    ${isOnline ? '在线' : '离线'}
                </span>
            </div>
            
            <div class="device-id">${device.name}</div>
            
            <div class="device-info">
                <div class="device-info-item">
                    <span class="device-info-label">设备ID</span>
                    <span class="device-info-value">${device.id}</span>
                </div>
                
                ${isOnline ? `
                    <div class="device-info-item">
                        <span class="device-info-label">IP地址</span>
                        <span class="device-info-value">${status.ip || '-'}</span>
                    </div>
                    
                    <div class="device-info-item">
                        <span class="device-info-label">WiFi信号</span>
                        <span class="device-info-value">${getSignalBars(status.rssi)}</span>
                    </div>
                    
                    <div class="device-info-item">
                        <span class="device-info-label">运行时间</span>
                        <span class="device-info-value">${formatUptime(status.uptime_ms)}</span>
                    </div>
                    
                    <div class="device-info-item">
                        <span class="device-info-label">剩余内存</span>
                        <span class="device-info-value">${formatMemory(status.freeHeap)}</span>
                    </div>
                ` : `
                    <div class="device-info-item">
                        <span class="device-info-label">状态</span>
                        <span class="device-info-value" style="color: #dc3545;">设备离线</span>
                    </div>
                `}
                
                <div class="device-info-item">
                    <span class="device-info-label">添加时间</span>
                    <span class="device-info-value">${formatDate(device.addedAt)}</span>
                </div>
            </div>
            
            <div class="device-actions" onclick="event.stopPropagation()">
                <button class="btn btn-success btn-small" onclick="openDevice('${device.id}')">
                    📱 管理设备
                </button>
                <button class="btn btn-danger btn-small" onclick="removeDevice('${device.id}')">
                    🗑️ 删除
                </button>
            </div>
        `;
        
        grid.appendChild(card);
    });
}

// 打开设备管理页面
function openDevice(deviceId) {
    window.location.href = `control.html?deviceId=${encodeURIComponent(deviceId)}`;
}

// HTTP轮询
function startPolling() {
    // 立即执行一次
    pollDeviceStatus();
    
    // 每5秒轮询一次
    setInterval(pollDeviceStatus, 5000);
}

async function pollDeviceStatus() {
    try {
        const response = await fetch(`${API_BASE}/api/devices`, {
            headers: {
                ...authHeaders()
            }
        });
        if (response.ok) {
            const result = await response.json();
            if (result.success && result.devices) {
                // 更新设备状态
                result.devices.forEach(device => {
                    deviceStatus[device.deviceId] = {
                        online: device.online !== undefined ? device.online : true,
                        rssi: device.rssi,
                        ip: device.ip,
                        uptime_ms: device.uptime_ms,
                        freeHeap: device.freeHeap,
                        lastSeen: device.lastSeen || Date.now()
                    };
                });
                
                renderDevices();
            }
        }
    } catch (e) {
        console.error('轮询失败:', e);
    }
}

// 工具函数：格式化信号强度
function getSignalBars(rssi) {
    if (!rssi) return '-';
    
    let bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -80) bars = 1;
    
    const html = '<span class="signal-strength">';
    let result = html;
    
    for (let i = 1; i <= 4; i++) {
        const height = i * 3 + 5;
        const active = i <= bars ? 'active' : '';
        result += `<span class="signal-bar ${active}" style="height: ${height}px"></span>`;
    }
    
    result += `</span> ${rssi} dBm`;
    return result;
}

// 工具函数：格式化运行时间
function formatUptime(ms) {
    if (!ms) return '-';
    
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}天 ${hours % 24}小时`;
    if (hours > 0) return `${hours}小时 ${minutes % 60}分钟`;
    if (minutes > 0) return `${minutes}分钟`;
    return `${seconds}秒`;
}

// 工具函数：格式化内存
function formatMemory(bytes) {
    if (!bytes) return '-';
    
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// 工具函数：格式化日期
function formatDate(timestamp) {
    const date = new Date(timestamp);
    return date.toLocaleString('zh-CN', {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit'
    });
}

// 处理回车键
document.getElementById('newDeviceId').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        addDevice();
    }
});

document.getElementById('deviceName').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        addDevice();
    }
});

// 退出登录
function logout() {
    if (confirm('确定要退出登录吗？')) {
        if (typeof clearAuth === 'function') {
            clearAuth();
        } else {
            localStorage.removeItem('authToken');
            localStorage.removeItem('authUser');
        }
        window.location.href = 'login.html';
    }
}
