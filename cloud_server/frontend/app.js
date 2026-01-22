// 全局变量
let sourceImage = null;
let processedImageData = null;

// 裁剪相关变量
let cropX = 0;
let cropY = 0;
let imageScale = 1;
let isDragging = false;
let dragStartX = 0;
let dragStartY = 0;

// 当前模式：'image' 或 'text' 或 'mixed'
let currentMode = 'image';

// 文字模式相关变量
let textItems = [];  // [{id, text, x, y, size, color}]
let selectedTextId = null;
let textDragging = false;

// 图文混合模式相关变量
let mixedTextItems = [];  // 混合模式的文字列表
let selectedMixedTextId = null;
let mixedTextDragging = false;
let mixedImageScale = 1;
let mixedCropX = 0;
let mixedCropY = 0;

// API 基础地址（前后端分离时，API通过nginx代理到后端）
const API_BASE = '';

// 初始化 - 由 editor.js 处理主要初始化，这里只做基础设置
// 如果 editor.js 未加载，则执行基础初始化
if (typeof window.editorInitialized === 'undefined') {
    document.addEventListener('DOMContentLoaded', () => {
        // 检查是否是新版界面
        if (document.getElementById('statusText')) {
            // 新版界面，由 editor.js 处理
            return;
        }
        // 旧版界面兼容
        initDropZone();
        initFileInput();
        loadDeviceFromURL();
        updateResolution();
        log('系统初始化完成');
    });
}

// 初始化算法选择功能
document.addEventListener('DOMContentLoaded', () => {
    const algorithmSelect = document.getElementById('algorithmSelect');
    const gradThreshContainer = document.getElementById('gradThreshContainer');
    
    if (algorithmSelect && gradThreshContainer) {
        // 根据选择的算法显示/隐藏梯度阈值输入框
        function updateGradThreshVisibility() {
            if (algorithmSelect.value === 'gradient_blend') {
                gradThreshContainer.style.display = 'block';
            } else {
                gradThreshContainer.style.display = 'none';
            }
        }
        
        // 初始状态
        updateGradThreshVisibility();
        
        // 监听算法选择变化
        algorithmSelect.addEventListener('change', updateGradThreshVisibility);
    }
});

// 从URL参数加载设备ID (旧版兼容)
async function loadDeviceFromURL() {
    const params = new URLSearchParams(window.location.search);
    const deviceId = params.get('deviceId');
    
    if (deviceId) {
        const deviceIdInput = document.getElementById('deviceId');
        if (deviceIdInput) deviceIdInput.value = deviceId;
        
        // 从服务器加载设备信息
    try {
        const response = await fetch(`${API_BASE}/api/devices/list`, {
            headers: {
                ...authHeaders()
            }
        });
            if (response.ok) {
                const result = await response.json();
                if (result.success) {
                    const device = result.devices.find(d => d.deviceId === deviceId);
                    const deviceNameEl = document.getElementById('deviceName');
                    if (deviceNameEl) {
                        deviceNameEl.textContent = device?.deviceName || deviceId;
                    }
                }
            }
        } catch (e) {
            console.log('Failed to load device info');
        }
    }
}

// 日志函数
function log(message, type = 'info') {
    // 优先使用新版状态栏
    const statusText = document.getElementById('statusText');
    const statusBar = document.getElementById('statusBar');
    
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    const text = `${emoji} ${message}`;
    
    if (statusText) {
        statusText.textContent = text;
    } else if (statusBar) {
        statusBar.textContent = `[${timestamp}] ${text}`;
    }
    
    console.log(`[${timestamp}] ${message}`);
}

// 初始化拖拽区域
function initDropZone() {
    const dropZone = document.getElementById('dropZone');
    if (!dropZone) return;
    
    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });
    
    dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('dragover');
    });
    
    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        
        const files = e.dataTransfer.files;
        if (files.length > 0) {
            handleFile(files[0]);
        }
    });
}

// 初始化文件输入
function initFileInput() {
    const fileInput = document.getElementById('fileInput');
    if (!fileInput) return;
    
    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) {
            handleFile(e.target.files[0]);
        }
    });
}

// handleFile 函数已移至文件末尾，支持图片模式和图文混合模式

// 显示原始图片并初始化裁剪框
function displaySourceImage(img) {
    // 新版界面使用 mainCanvas
    const mainCanvas = document.getElementById('mainCanvas');
    const sourceCanvas = document.getElementById('sourceCanvas');
    const canvas = mainCanvas || sourceCanvas;
    
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    
    // 重置缩放
    imageScale = 1;
    const scaleSlider = document.getElementById('scaleSlider');
    const scaleInput = document.getElementById('scaleInput');
    if (scaleSlider) scaleSlider.value = 100;
    if (scaleInput) scaleInput.value = 100;
    
    // 新版界面：保持画布尺寸为目标尺寸，绘制图片
    if (mainCanvas) {
        const width = parseInt(document.getElementById('width').value);
        const height = parseInt(document.getElementById('height').value);
        ctx.fillStyle = 'white';
        ctx.fillRect(0, 0, width, height);
        
        // 自动适应屏幕
        fitToScreen();
    } else {
        // 旧版界面：设置画布大小为图片大小
        canvas.width = img.width;
        canvas.height = img.height;
        ctx.drawImage(img, 0, 0);
        
        canvas.style.display = 'block';
        const sourceImage = document.getElementById('sourceImage');
        if (sourceImage) sourceImage.style.display = 'none';
        
        // 初始化裁剪框
        initCropBox();
    }
}

// 初始化裁剪框
function initCropBox() {
    const canvas = document.getElementById('sourceCanvas');
    const cropBox = document.getElementById('cropBox');
    const targetWidth = parseInt(document.getElementById('width').value);
    const targetHeight = parseInt(document.getElementById('height').value);
    
    // 居中显示裁剪框
    const scaledWidth = targetWidth / imageScale;
    const scaledHeight = targetHeight / imageScale;
    
    cropX = Math.max(0, (canvas.width - scaledWidth) / 2);
    cropY = Math.max(0, (canvas.height - scaledHeight) / 2);
    
    updateCropBox();
    cropBox.style.display = 'block';
    
    // 绑定拖拽事件
    bindCropEvents();
}

// 更新裁剪框位置和大小
function updateCropBox() {
    const canvas = document.getElementById('sourceCanvas');
    const cropBox = document.getElementById('cropBox');
    const container = document.getElementById('cropContainer');
    const targetWidth = parseInt(document.getElementById('width').value);
    const targetHeight = parseInt(document.getElementById('height').value);
    
    // 计算裁剪框在画布上的实际大小
    const scaledWidth = targetWidth / imageScale;
    const scaledHeight = targetHeight / imageScale;
    
    // 限制裁剪框在画布范围内
    cropX = Math.max(0, Math.min(cropX, canvas.width - scaledWidth));
    cropY = Math.max(0, Math.min(cropY, canvas.height - scaledHeight));
    
    // 计算画布在容器中的位置（居中显示）
    const canvasRect = canvas.getBoundingClientRect();
    const containerRect = container.getBoundingClientRect();
    const canvasOffsetX = canvasRect.left - containerRect.left;
    const canvasOffsetY = canvasRect.top - containerRect.top;
    
    // 计算裁剪框在容器中的位置
    const displayScale = canvasRect.width / canvas.width;
    
    cropBox.style.left = (canvasOffsetX + cropX * displayScale) + 'px';
    cropBox.style.top = (canvasOffsetY + cropY * displayScale) + 'px';
    cropBox.style.width = (scaledWidth * displayScale) + 'px';
    cropBox.style.height = (scaledHeight * displayScale) + 'px';
    
    // 更新隐藏的偏移值
    document.getElementById('offsetX').value = Math.round(cropX);
    document.getElementById('offsetY').value = Math.round(cropY);
}

// 绑定裁剪框拖拽事件
function bindCropEvents() {
    const cropBox = document.getElementById('cropBox');
    const container = document.getElementById('cropContainer');
    const canvas = document.getElementById('sourceCanvas');
    
    cropBox.onmousedown = function(e) {
        e.preventDefault();
        isDragging = true;
        dragStartX = e.clientX;
        dragStartY = e.clientY;
        cropBox.style.cursor = 'grabbing';
    };
    
    document.onmousemove = function(e) {
        if (!isDragging) return;
        
        const canvasRect = canvas.getBoundingClientRect();
        const displayScale = canvasRect.width / canvas.width;
        
        const dx = (e.clientX - dragStartX) / displayScale;
        const dy = (e.clientY - dragStartY) / displayScale;
        
        cropX += dx;
        cropY += dy;
        
        dragStartX = e.clientX;
        dragStartY = e.clientY;
        
        updateCropBox();
    };
    
    document.onmouseup = function() {
        isDragging = false;
        cropBox.style.cursor = 'move';
    };
    
    // 触摸事件支持
    cropBox.ontouchstart = function(e) {
        e.preventDefault();
        isDragging = true;
        dragStartX = e.touches[0].clientX;
        dragStartY = e.touches[0].clientY;
    };
    
    document.ontouchmove = function(e) {
        if (!isDragging) return;
        
        const canvasRect = canvas.getBoundingClientRect();
        const displayScale = canvasRect.width / canvas.width;
        
        const dx = (e.touches[0].clientX - dragStartX) / displayScale;
        const dy = (e.touches[0].clientY - dragStartY) / displayScale;
        
        cropX += dx;
        cropY += dy;
        
        dragStartX = e.touches[0].clientX;
        dragStartY = e.touches[0].clientY;
        
        updateCropBox();
    };
    
    document.ontouchend = function() {
        isDragging = false;
    };
}

// 从滑块更新缩放
function updateScaleFromSlider() {
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    imageScale = slider.value / 100;
    input.value = slider.value;
    
    if (sourceImage) {
        updateCropBox();
    }
}

// 从输入框更新缩放
function updateScaleFromInput() {
    const slider = document.getElementById('scaleSlider');
    const input = document.getElementById('scaleInput');
    let value = parseInt(input.value) || 100;
    value = Math.max(10, Math.min(500, value));
    input.value = value;
    slider.value = Math.min(300, value);
    imageScale = value / 100;
    
    if (sourceImage) {
        updateCropBox();
    }
}

// 兼容旧的 updateScale 函数
function updateScale() {
    updateScaleFromSlider();
}

// 重置裁剪
function resetCrop() {
    imageScale = 1;
    document.getElementById('scaleSlider').value = 100;
    document.getElementById('scaleInput').value = 100;
    
    if (sourceImage) {
        initCropBox();
    }
}

// 适应屏幕（自动缩放图片以适应目标尺寸）
function fitToScreen() {
    if (!sourceImage) return;
    
    const targetWidth = parseInt(document.getElementById('width').value);
    const targetHeight = parseInt(document.getElementById('height').value);
    
    // 计算需要的缩放比例，使图片能完全覆盖目标区域
    const scaleX = targetWidth / sourceImage.width;
    const scaleY = targetHeight / sourceImage.height;
    imageScale = Math.max(scaleX, scaleY);
    
    // 更新滑块和输入框
    const sliderValue = Math.round(imageScale * 100);
    document.getElementById('scaleSlider').value = Math.min(300, Math.max(10, sliderValue));
    document.getElementById('scaleInput').value = sliderValue;
    
    // 居中裁剪框
    const scaledWidth = targetWidth / imageScale;
    const scaledHeight = targetHeight / imageScale;
    cropX = Math.max(0, (sourceImage.width - scaledWidth) / 2);
    cropY = Math.max(0, (sourceImage.height - scaledHeight) / 2);
    
    updateCropBox();
    log(`已适应屏幕，缩放: ${sliderValue}%`, 'success');
}

// 全局变量：存储6色处理的4bit数据
window.e6Data4bit = null;

function authHeaders() {
    if (typeof getAuthHeaders === 'function') {
        return getAuthHeaders();
    }
    const token = localStorage.getItem('authToken');
    return token ? { 'Authorization': 'Bearer ' + token } : {};
}

// 处理图片 - 简化版：只调用后端API
// 获取算法参数
function getAlgorithmParams() {
    // 尝试多种方式查找算法选择下拉框
    let algorithmSelect = document.getElementById('algorithmSelect');
    
    // 如果找不到，尝试查找所有select元素
    if (!algorithmSelect) {
        const allSelects = document.querySelectorAll('select');
        console.warn('algorithmSelect 元素未找到，尝试查找所有select元素:', allSelects.length);
        for (let sel of allSelects) {
            if (sel.id === 'algorithmSelect' || sel.getAttribute('id') === 'algorithmSelect') {
                algorithmSelect = sel;
                console.log('找到算法选择下拉框:', sel);
                break;
            }
        }
    }
    
    if (!algorithmSelect) {
        console.error('❌ algorithmSelect 元素未找到，使用默认算法 floyd_steinberg');
        console.error('当前DOM中所有select元素:', Array.from(document.querySelectorAll('select')).map(s => ({id: s.id, value: s.value})));
        return { algorithm: 'floyd_steinberg', gradThresh: 40 };
    }
    
    // 强制读取当前值
    const algorithm = algorithmSelect.value || algorithmSelect.selectedOptions?.[0]?.value || 'floyd_steinberg';
    const gradThreshInput = document.getElementById('gradThreshInput');
    const gradThresh = (algorithm === 'gradient_blend' && gradThreshInput) ? parseInt(gradThreshInput.value) || 40 : 40;
    
    console.log('✅ 获取算法参数:', {
        algorithm: algorithm,
        gradThresh: gradThresh,
        selectValue: algorithmSelect.value,
        selectedIndex: algorithmSelect.selectedIndex,
        options: Array.from(algorithmSelect.options).map(opt => ({value: opt.value, text: opt.text, selected: opt.selected}))
    });
    
    return { algorithm, gradThresh };
}

// 算法名称映射
const algorithmNames = {
    'floyd_steinberg': 'Floyd-Steinberg抖动',
    'gradient_blend': '梯度边界混合',
    'grayscale_color_map': '灰阶与颜色映射'
};

function processImage() {
    // 新版界面：检查当前模式
    if (typeof currentMode !== 'undefined' && currentMode !== 'image') {
        // 处理其他模式（文字、混合、模板）
        processCurrentMode();
        return;
    }
    
    if (!sourceImage) {
        log('请先选择图片', 'error');
        return;
    }
    
    log('开始处理图片...');
    
    // 优先使用 mainCanvas（新版），否则用 sourceCanvas（旧版）
    const mainCanvas = document.getElementById('mainCanvas');
    const sourceCanvas = document.getElementById('sourceCanvas');
    const processedCanvas = document.getElementById('processedCanvas');
    
    if (!processedCanvas) {
        log('找不到处理画布', 'error');
        return;
    }
    
    // 获取参数
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    
    if (width < 3 || height < 3) {
        log('图片尺寸太小', 'error');
        return;
    }
    
    // 设置处理后画布大小
    processedCanvas.width = width;
    processedCanvas.height = height;
    const ctx = processedCanvas.getContext('2d');
    
    // 清空预览画布，显示处理中状态
    ctx.fillStyle = '#f0f0f0';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#999';
    ctx.font = '24px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('处理中...', width / 2, height / 2);
    
    // 创建临时画布用于发送到后端
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    
    // 计算源图像的裁剪区域
    const srcX = cropX;
    const srcY = cropY;
    const srcWidth = width / imageScale;
    const srcHeight = height / imageScale;
    
    // 从源图像绘制到临时画布
    if (mainCanvas && sourceImage) {
        // 新版界面：直接从源图像绘制
        tempCtx.drawImage(sourceImage, srcX, srcY, srcWidth, srcHeight, 0, 0, width, height);
    } else if (sourceCanvas) {
        // 旧版界面：从源画布绘制
        tempCtx.drawImage(sourceCanvas, srcX, srcY, srcWidth, srcHeight, 0, 0, width, height);
    } else {
        log('没有可处理的图像', 'error');
        return;
    }
    
    // 将临时画布转换为 base64 PNG，发送到后端处理
    const imageDataUrl = tempCanvas.toDataURL('image/png');
    const base64Data = imageDataUrl.split(',')[1];
    
    // 获取算法参数
    const { algorithm, gradThresh } = getAlgorithmParams();
    const algorithmName = algorithmNames[algorithm] || algorithm;
    console.log('实际使用的算法:', algorithm, '算法名称:', algorithmName);
    log(`正在调用后端6色算法处理（${algorithmName}）...`);
    
    // 显示进度条
    showProgress('正在处理图像...');
    updateProgress(10);
    
    // 调用后端 API
    fetch(`${API_BASE}/api/epd/process-sixcolor`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            ...authHeaders()
        },
        body: JSON.stringify({
            imageData: base64Data,
            width: width,
            height: height,
            algorithm: algorithm,
            gradThresh: gradThresh
        })
    })
    .then(response => {
        updateProgress(50);
        return response.json();
    })
    .then(result => {
        updateProgress(70);
        if (result.success) {
            // 加载预览图到画布
            const previewImg = new Image();
            previewImg.onload = () => {
                updateProgress(90);
                ctx.clearRect(0, 0, width, height);
                ctx.drawImage(previewImg, 0, 0);
                
                // 保存处理后的图像数据
                processedImageData = ctx.getImageData(0, 0, width, height);
                
                // 保存4bit数据（base64编码）
                window.e6Data4bit = result.data4bit; // 4bit数据（base64）
                
                updateProgress(100);
                setTimeout(() => {
                    hideProgress();
                }, 500);
                
                log(`6色处理完成：已使用${algorithmName}映射到6色调色板`, 'success');
            };
            previewImg.onerror = () => {
                hideProgress();
                log('预览图加载失败', 'error');
            };
            previewImg.src = 'data:image/png;base64,' + result.previewImage;
        } else {
            hideProgress();
            log('处理失败: ' + result.error, 'error');
        }
    })
    .catch(error => {
        hideProgress();
        log('处理失败: ' + error.message, 'error');
        console.error(error);
    });
}

// 编码工具函数
function byteToStr(byte) {
    const low = byte & 0x0F;
    const high = (byte >> 4) & 0x0F;
    return String.fromCharCode(97 + low) + String.fromCharCode(97 + high);
}

function wordToStr(value) {
    const lowByte = value & 0xFF;
    const highByte = (value >> 8) & 0xFF;
    return byteToStr(lowByte) + byteToStr(highByte);
}

// 将像素数组转换为编码字符串
function pixelArrayToDataString(pixelArray) {
    // 将像素数组转为字节数组
    const byteArray = [];
    for (let i = 0; i < pixelArray.length; i += 8) {
        let byte = 0;
        for (let bit = 0; bit < 8 && (i + bit) < pixelArray.length; bit++) {
            if (pixelArray[i + bit] != 0) {
                byte |= (128 >> bit);
            }
        }
        byteArray.push(byte);
    }
    
    // 编码为字符串
    let dataString = '';
    for (let i = 0; i < byteArray.length; i++) {
        dataString += byteToStr(byteArray[i]);
    }
    
    return dataString;
}

// 进度条控制
function showProgress(label = '上传进度') {
    // 新版界面
    const overlay = document.getElementById('progressOverlay');
    if (overlay) {
        overlay.classList.add('show');
        const labelEl = document.getElementById('progressLabel');
        if (labelEl) labelEl.textContent = label;
    }
    // 旧版界面
    const container = document.getElementById('progressContainer');
    if (container) {
        container.style.display = 'block';
        const labelEl = document.getElementById('progressLabel');
        if (labelEl) labelEl.textContent = label;
    }
    updateProgress(0);
}

function updateProgress(percent) {
    const bar = document.getElementById('progressBar');
    if (bar) bar.style.width = percent + '%';
    
    const percentEl = document.getElementById('progressPercent');
    if (percentEl) percentEl.textContent = Math.round(percent) + '%';
}

function hideProgress() {
    const overlay = document.getElementById('progressOverlay');
    if (overlay) overlay.classList.remove('show');
    
    const container = document.getElementById('progressContainer');
    if (container) container.style.display = 'none';
}

// 发送数据到设备（旧版，兼容性保留）
async function sendDataToDevice(deviceId, dataString, label = '上传数据') {
    return sendDataToDeviceInChunks(deviceId, dataString, 1000);
}

// 分批发送数据到设备（支持自定义缓存大小）
async function sendDataToDeviceInChunks(deviceId, dataString, chunkSize = 1000) {
    const totalChunks = Math.ceil(dataString.length / chunkSize);
    console.log(`📦 开始分批发送: 总长度=${dataString.length}, 每批=${chunkSize}, 共${totalChunks}批`);
    
    for (let i = 0; i < dataString.length; i += chunkSize) {
        const chunk = dataString.substring(i, i + chunkSize);
        const chunkWithLength = chunk + wordToStr(chunk.length);
        
        const response = await fetch(`${API_BASE}/api/epd/load`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...authHeaders() },
            body: JSON.stringify({ 
                deviceId, 
                data: chunkWithLength,
                length: chunkWithLength.length
            })
        });
        
        if (!response.ok) {
            throw new Error('数据发送失败: ' + await response.text());
        }
        
        // 更新进度条
        const progress = ((i + chunkSize) / dataString.length) * 100;
        updateProgress(Math.min(progress, 100));
        
        const currentChunk = Math.floor(i / chunkSize) + 1;
        log(`已发送 ${currentChunk}/${totalChunks} 批 (${Math.min(i + chunkSize, dataString.length)}/${dataString.length} 字符)`);
        
        await sleep(100); // 等待ESP32处理
    }
    
    log(`✅ 所有数据已发送完成 (${totalChunks}批)`, 'success');
}

// 上传到设备（简化版：只支持6色处理）
async function uploadToDevice() {
    // 检查是否有处理后的数据
    if (!window.e6Data4bit) {
        log('请先处理图片（点击"处理并预览"）', 'error');
        return;
    }
    
    // 获取deviceId，优先从隐藏input获取，如果没有则从URL参数获取
    let deviceId = '';
    const deviceIdInput = document.getElementById('deviceId');
    if (deviceIdInput) {
        deviceId = deviceIdInput.value.trim();
    }
    
    // 如果还是没有，尝试从URL参数获取
    if (!deviceId) {
        const params = new URLSearchParams(window.location.search);
        deviceId = params.get('deviceId') || '';
    }
    
    // 如果还是没有，尝试从全局变量获取（editor.js设置的）
    if (!deviceId && typeof window.deviceId !== 'undefined') {
        deviceId = window.deviceId;
    }
    
    if (!deviceId) {
        log('请输入设备ID', 'error');
        console.error('❌ deviceId未找到');
        return;
    }
    
    const epdType = 0; // 固定为7.3寸E6
    const width = 800;
    const height = 480;
    
    console.log('📤 下发参数:', { deviceId, epdType, width, height });
    
    try {
        showProgress('初始化中...');
        log('正在初始化墨水屏...');
        
        // 1. 初始化EPD
        const initResponse = await fetch(`${API_BASE}/api/epd/init`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...authHeaders() },
            body: JSON.stringify({ 
                deviceId: deviceId, 
                epdType: Number(epdType) 
            })
        });
        
        if (!initResponse.ok) {
            throw new Error('初始化失败: ' + await initResponse.text());
        }
        
        await sleep(500);
        
        log(`正在上传图像数据 (${width}x${height})...`);
        
        showProgress('上传6色数据（一次下发）...');
        log(`上传6色数据（4bit格式，一次下发）...`);
        
        // 从后端返回的base64数据解码
        const binaryString = atob(window.e6Data4bit);
        const bytes = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
            bytes[i] = binaryString.charCodeAt(i);
        }
        
        // 转换为编码字符串格式（'a'=0, 'b'=1, ..., 'p'=15）
        const sixColorDataString = [];
        for (let i = 0; i < bytes.length; i++) {
            const byte = bytes[i];
            const low = byte & 0x0F;
            const high = (byte >> 4) & 0x0F;
            // 编码为字符串
            sixColorDataString.push(String.fromCharCode(97 + low));
            sixColorDataString.push(String.fromCharCode(97 + high));
        }
        
        const dataString = sixColorDataString.join('');
        console.log(`📊 6色数据: ${dataString.length} 字符 (${width}x${height}, 4bit格式，后端处理)`);
        console.log(`📦 一次发送所有数据 (${dataString.length} 字符)`);
        
        // 一次发送所有数据（不添加长度后缀，因为ESP32直接写入Flash）
        const response = await fetch(`${API_BASE}/api/epd/load`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...authHeaders() },
            body: JSON.stringify({ 
                deviceId, 
                data: dataString,
                length: dataString.length
            })
        });
        
        if (!response.ok) {
            throw new Error('数据发送失败: ' + await response.text());
        }
        
        log(`✅ 数据已发送完成 (${dataString.length} 字符)`, 'success');
        
        // 显示
        showProgress('刷新显示...');
        updateProgress(100);
        log('正在刷新显示...');
        const showResponse = await fetch(`${API_BASE}/api/epd/show`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...authHeaders() },
            body: JSON.stringify({ deviceId })
        });
        
        if (!showResponse.ok) {
            throw new Error('显示命令失败');
        }
        
        hideProgress();
        log('下发完成！', 'success');
        log('请等待30秒刷新...', 'info');

        // 更新顶部“最近下发时间”显示
        const lastUpdateEl = document.getElementById('lastUpdateDisplay');
        if (lastUpdateEl) {
            const now = new Date();
            const timeStr = now.toLocaleString();
            lastUpdateEl.textContent = timeStr;
        }
        
    } catch (error) {
        hideProgress();
        log(`上传失败: ${error.message}`, 'error');
        console.error(error);
    }
}

// 延迟函数
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// 显示设备码到墨水屏
async function showDeviceCode() {
    const deviceId = document.getElementById('deviceId').value.trim();
    if (!deviceId) {
        log('请输入设备ID', 'error');
        return;
    }
    
    try {
        log('正在发送显示设备码命令...');
        
        const response = await fetch(`${API_BASE}/api/epd/show-device-code`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', ...authHeaders() },
            body: JSON.stringify({ deviceId })
        });
        
        if (!response.ok) {
            throw new Error('命令发送失败: ' + await response.text());
        }
        
        log('✅ 设备码显示命令已发送', 'success');
        
    } catch (error) {
        log(`发送失败: ${error.message}`, 'error');
        console.error(error);
    }
}

// EPD型号分辨率映射（固定为7.3" E6）
const EPD_RESOLUTIONS = {
    0: [800, 480],  // 7.3" E6
};

// 根据EPD型号自动设置分辨率（固定为7.3" E6）
function updateResolution() {
    // 固定为7.3寸E6
    document.getElementById('width').value = 800;
    document.getElementById('height').value = 480;
    log(`已设置分辨率: 800x480 (7.3寸E6)`);
}

// 页面加载时初始化分辨率
function updateDimensions() {
    updateResolution();
}

// ==================== 模式切换 ====================

function switchMode(mode) {
    currentMode = mode;
    
    // 更新按钮样式
    const btnImage = document.getElementById('btnImageMode');
    const btnText = document.getElementById('btnTextMode');
    const btnMixed = document.getElementById('btnMixedMode');
    
    btnImage.className = 'btn btn-secondary';
    btnText.className = 'btn btn-secondary';
    btnMixed.className = 'btn btn-secondary';
    
    // 隐藏所有面板
    document.getElementById('imageModePanel').style.display = 'none';
    document.getElementById('textModePanel').style.display = 'none';
    document.getElementById('mixedModePanel').style.display = 'none';
    document.getElementById('imageEditorBox').style.display = 'none';
    document.getElementById('textEditorBox').style.display = 'none';
    document.getElementById('mixedEditorBox').style.display = 'none';
    document.getElementById('dropZone').style.display = 'none';
    
    if (mode === 'image') {
        btnImage.className = 'btn btn-primary';
        document.getElementById('imageModePanel').style.display = 'block';
        document.getElementById('imageEditorBox').style.display = 'block';
        document.getElementById('dropZone').style.display = 'block';
    } else if (mode === 'text') {
        btnText.className = 'btn btn-primary';
        document.getElementById('textModePanel').style.display = 'block';
        document.getElementById('textEditorBox').style.display = 'block';
        
        // 初始化文字画布
        initTextCanvas();
    } else if (mode === 'mixed') {
        btnMixed.className = 'btn btn-primary';
        document.getElementById('mixedModePanel').style.display = 'block';
        document.getElementById('mixedEditorBox').style.display = 'block';
        document.getElementById('dropZone').style.display = 'block';
        
        // 初始化混合画布
        initMixedCanvas();
    }
}

// ==================== 文字模式 ====================

function initTextCanvas() {
    // 支持新版（mainCanvas）和旧版（textCanvas）UI
    const canvas = document.getElementById('mainCanvas') || document.getElementById('textCanvas');
    if (!canvas) return;
    
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    
    canvas.width = width;
    canvas.height = height;
    canvas.style.maxWidth = '100%';
    
    renderTextCanvas();
    
    // 旧版UI需要绑定事件
    if (document.getElementById('textCanvas')) {
        bindTextCanvasEvents();
    }
}

function renderTextCanvas() {
    // 支持新版（mainCanvas）和旧版（textCanvas）UI
    const canvas = document.getElementById('mainCanvas') || document.getElementById('textCanvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;
    
    // 背景颜色
    const bgColor = document.getElementById('textBgColor').value;
    ctx.fillStyle = bgColor;
    ctx.fillRect(0, 0, width, height);
    
    // 绘制所有文字
    textItems.forEach(item => {
        ctx.font = `${item.size}px Arial, sans-serif`;
        ctx.fillStyle = item.color;
        ctx.textBaseline = 'top';
        ctx.fillText(item.text, item.x, item.y);
        
        // 如果被选中，绘制选择框
        if (item.id === selectedTextId) {
            const metrics = ctx.measureText(item.text);
            ctx.strokeStyle = '#667eea';
            ctx.lineWidth = 2;
            ctx.setLineDash([5, 5]);
            ctx.strokeRect(item.x - 5, item.y - 5, metrics.width + 10, item.size + 10);
            ctx.setLineDash([]);
        }
    });
}

function addTextItem() {
    const textInput = document.getElementById('newTextContent');
    if (!textInput) return;
    
    const text = textInput.value.trim();
    if (!text) {
        log('请输入文字内容', 'error');
        return;
    }
    
    const size = parseInt(document.getElementById('newTextSize')?.value) || 48;
    const color = document.getElementById('newTextColor')?.value || 'black';
    const width = parseInt(document.getElementById('width')?.value) || 800;
    const height = parseInt(document.getElementById('height')?.value) || 480;
    
    const item = {
        id: Date.now(),
        text: text,
        x: Math.round((width - text.length * size * 0.6) / 2),  // 居中
        y: Math.round((height - size) / 2),
        size: size,
        color: color
    };
    
    textItems.push(item);
    selectedTextId = item.id;
    
    textInput.value = '';
    
    // 调用渲染函数（新版界面用 renderCanvas，旧版用 renderTextCanvas）
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderTextCanvas === 'function') {
        renderTextCanvas();
    }
    updateTextItemsList();
    log(`已添加文字: "${text}"`, 'success');
}

function updateTextItemsList() {
    // 支持新版和旧版UI
    const container = document.getElementById('textItemsList') || document.getElementById('textList');
    if (!container) return;
    
    if (textItems.length === 0) {
        container.innerHTML = '<div style="padding: 20px; text-align: center; color: #888;">暂无文字</div>';
        return;
    }
    
    container.innerHTML = textItems.map(item => `
        <div class="text-item ${item.id === selectedTextId ? 'selected' : ''}" 
             onclick="selectTextItem(${item.id})"
             style="display: flex; align-items: center; padding: 10px; border-bottom: 1px solid #e2e8f0; cursor: pointer;">
            <span class="color-dot" style="width: 12px; height: 12px; border-radius: 50%; margin-right: 10px; background: ${item.color};"></span>
            <span class="text-content" style="flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">${item.text}</span>
            <span style="font-size: 12px; color: #888; margin-right: 10px;">${item.size}px</span>
            <button class="delete-btn" onclick="event.stopPropagation(); deleteTextItem(${item.id})" 
                    style="background: none; border: none; cursor: pointer; color: #f56565; padding: 4px;">🗑️</button>
        </div>
    `).join('');
}

// 别名，兼容新版UI
function updateTextList() {
    updateTextItemsList();
}

function selectTextItem(id) {
    selectedTextId = id;
    
    // 调用渲染函数
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderTextCanvas === 'function') {
        renderTextCanvas();
    }
    updateTextItemsList();
}

function deleteTextItem(id) {
    textItems = textItems.filter(item => item.id !== id);
    if (selectedTextId === id) {
        selectedTextId = null;
    }
    
    // 调用渲染函数
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderTextCanvas === 'function') {
        renderTextCanvas();
    }
    updateTextItemsList();
    log('已删除文字', 'success');
}

function clearAllText() {
    textItems = [];
    selectedTextId = null;
    
    // 调用渲染函数
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderTextCanvas === 'function') {
        renderTextCanvas();
    }
    updateTextItemsList();
    log('已清空所有文字', 'success');
}

function bindTextCanvasEvents() {
    const canvas = document.getElementById('textCanvas');
    
    canvas.onmousedown = function(e) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;
        
        // 检查点击了哪个文字
        const ctx = canvas.getContext('2d');
        let clickedItem = null;
        
        for (let i = textItems.length - 1; i >= 0; i--) {
            const item = textItems[i];
            ctx.font = `${item.size}px Arial, sans-serif`;
            const metrics = ctx.measureText(item.text);
            
            if (x >= item.x && x <= item.x + metrics.width &&
                y >= item.y && y <= item.y + item.size) {
                clickedItem = item;
                break;
            }
        }
        
        if (clickedItem) {
            selectedTextId = clickedItem.id;
            textDragging = true;
            dragStartX = x - clickedItem.x;
            dragStartY = y - clickedItem.y;
            renderTextCanvas();
            updateTextItemsList();
        } else {
            selectedTextId = null;
            renderTextCanvas();
            updateTextItemsList();
        }
    };
    
    canvas.onmousemove = function(e) {
        if (!textDragging || !selectedTextId) return;
        
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;
        
        const item = textItems.find(t => t.id === selectedTextId);
        if (item) {
            item.x = Math.max(0, Math.min(canvas.width - 50, x - dragStartX));
            item.y = Math.max(0, Math.min(canvas.height - item.size, y - dragStartY));
            renderTextCanvas();
        }
    };
    
    canvas.onmouseup = function() {
        textDragging = false;
    };
    
    canvas.onmouseleave = function() {
        textDragging = false;
    };
}

// 处理当前模式（文字、混合、模板）
function processCurrentMode() {
    if (currentMode === 'text') {
        processTextImage();
    } else if (currentMode === 'mixed') {
        processMixedImage();
    } else if (currentMode === 'template') {
        processTemplateImage();
    }
}

// 处理模板模式 - 简化版：只调用后端API
function processTemplateImage() {
    const mainCanvas = document.getElementById('mainCanvas');
    const processedCanvas = document.getElementById('processedCanvas');
    
    if (!mainCanvas || !processedCanvas) return;
    
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    
    // 设置处理画布大小，先显示处理中状态
    processedCanvas.width = width;
    processedCanvas.height = height;
    const ctx = processedCanvas.getContext('2d');
    
    // 清空预览画布，显示处理中状态
    ctx.fillStyle = '#f0f0f0';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#999';
    ctx.font = '24px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('处理中...', width / 2, height / 2);
    
    // 将主画布内容拷贝到临时变量（用于发送到后端）
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    tempCtx.drawImage(mainCanvas, 0, 0);
    
    // 将临时画布转换为 base64 PNG，发送到后端处理
    const imageDataUrl = tempCanvas.toDataURL('image/png');
    const base64Data = imageDataUrl.split(',')[1];
    
    // 获取算法参数
    const { algorithm, gradThresh } = getAlgorithmParams();
    const algorithmName = algorithmNames[algorithm] || algorithm;
    console.log('实际使用的算法:', algorithm, '算法名称:', algorithmName);
    log(`正在调用后端6色算法处理（${algorithmName}）...`);
    
    // 调用后端 API
    fetch(`${API_BASE}/api/epd/process-sixcolor`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            ...authHeaders()
        },
        body: JSON.stringify({
            imageData: base64Data,
            width: width,
            height: height,
            algorithm: algorithm,
            gradThresh: gradThresh
        })
    })
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            // 加载预览图到画布
            const previewImg = new Image();
            previewImg.onload = () => {
                ctx.clearRect(0, 0, width, height);
                ctx.drawImage(previewImg, 0, 0);
                
                // 保存处理后的图像数据
                processedImageData = ctx.getImageData(0, 0, width, height);
                
                // 保存4bit数据（base64编码）
                window.e6Data4bit = result.data4bit;
                
                const algorithmName = algorithmNames[algorithm] || algorithm;
                log(`6色处理完成：已使用${algorithmName}映射到6色调色板`, 'success');
            };
            previewImg.src = 'data:image/png;base64,' + result.previewImage;
        } else {
            log('处理失败: ' + result.error, 'error');
        }
    })
    .catch(error => {
        log('处理失败: ' + error.message, 'error');
        console.error(error);
    });
}

// 处理文字模式 - 简化版：只调用后端API
function processTextImage() {
    // 新版界面：文字已经直接画在 mainCanvas 上，这里从 mainCanvas 拷贝到 processedCanvas 再做处理
    const mainCanvas = document.getElementById('mainCanvas');
    const processedCanvas = document.getElementById('processedCanvas');
    if (!mainCanvas || !processedCanvas) {
        log('画布未就绪，请稍后重试', 'error');
        return;
    }

    const widthInput = document.getElementById('width');
    const heightInput = document.getElementById('height');
    const width = widthInput ? parseInt(widthInput.value, 10) || mainCanvas.width : mainCanvas.width;
    const height = heightInput ? parseInt(heightInput.value, 10) || mainCanvas.height : mainCanvas.height;

    // 将主画布内容拷贝到处理画布
    processedCanvas.width = width;
    processedCanvas.height = height;
    const ctx = processedCanvas.getContext('2d');
    
    // 先清空预览画布，显示处理中状态
    ctx.fillStyle = '#f0f0f0';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#999';
    ctx.font = '24px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('处理中...', width / 2, height / 2);
    
    // 将主画布内容拷贝到临时变量（用于发送到后端）
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    tempCtx.drawImage(mainCanvas, 0, 0, width, height);
    
    // 将画布转换为 base64 PNG，发送到后端处理
    const imageDataUrl = processedCanvas.toDataURL('image/png');
    const base64Data = imageDataUrl.split(',')[1];
    
    // 获取算法参数
    const { algorithm, gradThresh } = getAlgorithmParams();
    const algorithmName = algorithmNames[algorithm] || algorithm;
    console.log('实际使用的算法:', algorithm, '算法名称:', algorithmName);
    log(`正在调用后端6色算法处理（${algorithmName}）...`);
    
    // 调用后端 API
    fetch(`${API_BASE}/api/epd/process-sixcolor`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            ...authHeaders()
        },
        body: JSON.stringify({
            imageData: base64Data,
            width: width,
            height: height,
            algorithm: algorithm,
            gradThresh: gradThresh
        })
    })
    .then(response => response.json())
    .then(result => {
        if (result.success) {
            // 加载预览图到画布
            const previewImg = new Image();
            previewImg.onload = () => {
                ctx.clearRect(0, 0, width, height);
                ctx.drawImage(previewImg, 0, 0);
                
                // 保存处理后的图像数据
                processedImageData = ctx.getImageData(0, 0, width, height);
                
                // 保存4bit数据（base64编码）
                window.e6Data4bit = result.data4bit;
                
                const algorithmName = algorithmNames[algorithm] || algorithm;
                log(`6色处理完成：已使用${algorithmName}映射到6色调色板`, 'success');
            };
            previewImg.src = 'data:image/png;base64,' + result.previewImage;
        } else {
            log('处理失败: ' + result.error, 'error');
        }
    })
    .catch(error => {
        log('处理失败: ' + error.message, 'error');
        console.error(error);
    });
}

// ==================== 图文混合模式 ====================

function initMixedCanvas() {
    // 支持新版（mainCanvas）和旧版（mixedCanvas）UI
    const canvas = document.getElementById('mainCanvas') || document.getElementById('mixedCanvas');
    if (!canvas) return;
    
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    
    canvas.width = width;
    canvas.height = height;
    // 统一画布样式，确保所有模式下显示一致
    canvas.style.maxWidth = '100%';
    canvas.style.height = 'auto';
    canvas.style.width = 'auto';
    canvas.style.aspectRatio = '800 / 480';
    
    // 重置缩放
    mixedImageScale = 1;
    const scaleSlider = document.getElementById('mixedScaleSlider');
    const scaleInput = document.getElementById('mixedScaleInput');
    if (scaleSlider) scaleSlider.value = 100;
    if (scaleInput) scaleInput.value = 100;
    
    renderMixedCanvas();
    
    // 旧版UI需要绑定事件
    if (document.getElementById('mixedCanvas')) {
        bindMixedCanvasEvents();
    }
}

function renderMixedCanvas() {
    // 支持新版（mainCanvas）和旧版（mixedCanvas）UI
    const canvas = document.getElementById('mainCanvas') || document.getElementById('mixedCanvas');
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;
    
    // 白色背景
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);
    
    // 绘制图片（如果有）
    if (sourceImage) {
        const srcWidth = width / mixedImageScale;
        const srcHeight = height / mixedImageScale;
        
        // 限制裁剪区域在图片范围内
        mixedCropX = Math.max(0, Math.min(mixedCropX, sourceImage.width - srcWidth));
        mixedCropY = Math.max(0, Math.min(mixedCropY, sourceImage.height - srcHeight));
        
        ctx.drawImage(sourceImage, mixedCropX, mixedCropY, srcWidth, srcHeight, 0, 0, width, height);
    }
    
    // 绘制所有文字
    mixedTextItems.forEach(item => {
        ctx.font = `${item.size}px Arial, sans-serif`;
        ctx.fillStyle = item.color;
        ctx.textBaseline = 'top';
        ctx.fillText(item.text, item.x, item.y);
        
        // 如果被选中，绘制选择框
        if (item.id === selectedMixedTextId) {
            const metrics = ctx.measureText(item.text);
            ctx.strokeStyle = '#667eea';
            ctx.lineWidth = 2;
            ctx.setLineDash([5, 5]);
            ctx.strokeRect(item.x - 5, item.y - 5, metrics.width + 10, item.size + 10);
            ctx.setLineDash([]);
        }
    });
}

function updateMixedScale() {
    const slider = document.getElementById('mixedScaleSlider');
    const input = document.getElementById('mixedScaleInput');
    mixedImageScale = slider.value / 100;
    input.value = slider.value;
    
    renderMixedCanvas();
}

function updateMixedScaleFromInput() {
    const slider = document.getElementById('mixedScaleSlider');
    const input = document.getElementById('mixedScaleInput');
    let value = parseInt(input.value) || 100;
    value = Math.max(10, Math.min(500, value));
    input.value = value;
    slider.value = Math.min(300, value);
    mixedImageScale = value / 100;
    
    renderMixedCanvas();
}

function fitMixedToScreen() {
    if (!sourceImage) {
        log('请先选择图片', 'error');
        return;
    }
    
    const canvas = document.getElementById('mixedCanvas');
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    
    // 确保画布尺寸正确
    if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
        canvas.style.maxWidth = '100%';
        bindMixedCanvasEvents();
    }
    
    // 计算需要的缩放比例，使图片能完全覆盖目标区域
    const scaleX = width / sourceImage.width;
    const scaleY = height / sourceImage.height;
    mixedImageScale = Math.max(scaleX, scaleY);
    
    // 更新滑块和输入框
    const sliderValue = Math.round(mixedImageScale * 100);
    document.getElementById('mixedScaleSlider').value = Math.min(300, Math.max(10, sliderValue));
    document.getElementById('mixedScaleInput').value = sliderValue;
    
    // 居中
    const srcWidth = width / mixedImageScale;
    const srcHeight = height / mixedImageScale;
    mixedCropX = Math.max(0, (sourceImage.width - srcWidth) / 2);
    mixedCropY = Math.max(0, (sourceImage.height - srcHeight) / 2);
    
    renderMixedCanvas();
    log(`已适应屏幕，缩放: ${sliderValue}%`, 'success');
}

function addMixedTextItem() {
    const textInput = document.getElementById('mixedTextContent');
    if (!textInput) return;
    
    const text = textInput.value.trim();
    if (!text) {
        log('请输入文字内容', 'error');
        return;
    }
    
    const size = parseInt(document.getElementById('mixedTextSize')?.value) || 36;
    const color = document.getElementById('mixedTextColor')?.value || 'black';
    const width = parseInt(document.getElementById('width')?.value) || 800;
    const height = parseInt(document.getElementById('height')?.value) || 480;
    
    const item = {
        id: Date.now(),
        text: text,
        x: Math.round((width - text.length * size * 0.6) / 2),  // 居中
        y: Math.round((height - size) / 2),
        size: size,
        color: color
    };
    
    mixedTextItems.push(item);
    selectedMixedTextId = item.id;
    
    textInput.value = '';
    
    // 调用渲染函数（新版界面用 renderCanvas，旧版用 renderMixedCanvas）
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderMixedCanvas === 'function') {
        renderMixedCanvas();
    }
    updateMixedTextItemsList();
    log(`已添加文字: "${text}"`, 'success');
}

function updateMixedTextItemsList() {
    const container = document.getElementById('mixedTextItemsList') || document.getElementById('mixedTextList');
    if (!container) return;
    
    if (mixedTextItems.length === 0) {
        container.innerHTML = '<div style="padding: 15px; text-align: center; color: #888;">暂无文字</div>';
        return;
    }
    
    container.innerHTML = mixedTextItems.map(item => `
        <div style="display: flex; align-items: center; padding: 8px; margin-bottom: 5px; 
                    background: ${item.id === selectedMixedTextId ? '#e7f3ff' : '#f8f9fa'}; 
                    border-radius: 5px; cursor: pointer;"
             onclick="selectMixedTextItem(${item.id})">
            <span style="flex: 1; color: ${item.color}; font-weight: bold; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">${item.text}</span>
            <span style="font-size: 12px; color: #888; margin: 0 8px;">${item.size}px</span>
            <button onclick="event.stopPropagation(); deleteMixedTextItem(${item.id})" 
                    style="background: #dc3545; color: white; border: none; border-radius: 3px; 
                           padding: 2px 8px; cursor: pointer;">✕</button>
        </div>
    `).join('');
}

function selectMixedTextItem(id) {
    selectedMixedTextId = id;
    renderMixedCanvas();
    updateMixedTextItemsList();
}

function deleteMixedTextItem(id) {
    mixedTextItems = mixedTextItems.filter(item => item.id !== id);
    if (selectedMixedTextId === id) {
        selectedMixedTextId = null;
    }
    renderMixedCanvas();
    updateMixedTextItemsList();
    log('已删除文字', 'success');
}

function clearMixedText() {
    mixedTextItems = [];
    selectedMixedTextId = null;
    
    // 调用渲染函数
    if (typeof renderCanvas === 'function' && document.getElementById('mainCanvas')) {
        renderCanvas();
    } else if (typeof renderMixedCanvas === 'function') {
        renderMixedCanvas();
    }
    updateMixedTextItemsList();
    log('已清空所有文字', 'success');
}

function bindMixedCanvasEvents() {
    const canvas = document.getElementById('mixedCanvas');
    let lastDragX = 0, lastDragY = 0;
    let isDraggingImage = false;
    
    canvas.onmousedown = function(e) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;
        
        // 检查点击了哪个文字
        const ctx = canvas.getContext('2d');
        let clickedItem = null;
        
        for (let i = mixedTextItems.length - 1; i >= 0; i--) {
            const item = mixedTextItems[i];
            ctx.font = `${item.size}px Arial, sans-serif`;
            const metrics = ctx.measureText(item.text);
            
            if (x >= item.x && x <= item.x + metrics.width &&
                y >= item.y && y <= item.y + item.size) {
                clickedItem = item;
                break;
            }
        }
        
        if (clickedItem) {
            selectedMixedTextId = clickedItem.id;
            mixedTextDragging = true;
            dragStartX = x - clickedItem.x;
            dragStartY = y - clickedItem.y;
            renderMixedCanvas();
            updateMixedTextItemsList();
        } else if (sourceImage) {
            // 如果点击的不是文字，拖动图片
            selectedMixedTextId = null;
            isDraggingImage = true;
            lastDragX = e.clientX;
            lastDragY = e.clientY;
            canvas.style.cursor = 'grabbing';
            renderMixedCanvas();
            updateMixedTextItemsList();
        }
    };
    
    canvas.onmousemove = function(e) {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        
        if (mixedTextDragging && selectedMixedTextId) {
            const x = (e.clientX - rect.left) * scaleX;
            const y = (e.clientY - rect.top) * scaleY;
            
            const item = mixedTextItems.find(t => t.id === selectedMixedTextId);
            if (item) {
                item.x = Math.max(0, Math.min(canvas.width - 50, x - dragStartX));
                item.y = Math.max(0, Math.min(canvas.height - item.size, y - dragStartY));
                renderMixedCanvas();
            }
        } else if (isDraggingImage && sourceImage) {
            const dx = (e.clientX - lastDragX) / (rect.width / canvas.width) / mixedImageScale;
            const dy = (e.clientY - lastDragY) / (rect.height / canvas.height) / mixedImageScale;
            
            mixedCropX -= dx;
            mixedCropY -= dy;
            
            lastDragX = e.clientX;
            lastDragY = e.clientY;
            
            renderMixedCanvas();
        }
    };
    
    canvas.onmouseup = function() {
        mixedTextDragging = false;
        isDraggingImage = false;
        canvas.style.cursor = 'default';
    };
    
    canvas.onmouseleave = function() {
        mixedTextDragging = false;
        isDraggingImage = false;
        canvas.style.cursor = 'default';
    };
}

function processMixedImage() {
    console.log('[processMixedImage] 开始处理图文模式');
    
    // 新版界面：使用 mainCanvas 而不是 mixedCanvas
    const mainCanvas = document.getElementById('mainCanvas');
    const processedCanvas = document.getElementById('processedCanvas');
    const widthEl = document.getElementById('width');
    const heightEl = document.getElementById('height');
    
    if (!widthEl || !heightEl) {
        log('找不到宽度/高度输入框', 'error');
        console.error('[processMixedImage] 找不到宽度/高度输入框');
        return;
    }
    
    const width = parseInt(widthEl.value) || 800;
    const height = parseInt(heightEl.value) || 480;
    
    // 检查是否有处理类型选择器（可能不存在）
    const processTypeEl = document.querySelector('input[name="processType"]:checked');
    const processType = processTypeEl ? processTypeEl.value : 'sixcolor';
    
    if (!mainCanvas) {
        log('找不到主画布', 'error');
        console.error('[processMixedImage] 找不到主画布');
        return;
    }
    
    if (!processedCanvas) {
        log('找不到处理画布', 'error');
        console.error('[processMixedImage] 找不到处理画布');
        return;
    }
    
    console.log('[processMixedImage] 画布尺寸:', width, height);
    
    // 确保主画布尺寸正确
    if (mainCanvas.width !== width || mainCanvas.height !== height) {
        mainCanvas.width = width;
        mainCanvas.height = height;
        // 重新渲染画布
        if (typeof renderCanvas === 'function') {
            renderCanvas();
        }
    }
    
    // 设置处理画布大小，先显示处理中状态
    processedCanvas.width = width;
    processedCanvas.height = height;
    const ctx = processedCanvas.getContext('2d');
    
    // 清空预览画布，显示处理中状态
    ctx.fillStyle = '#f0f0f0';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#999';
    ctx.font = '24px Arial';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('处理中...', width / 2, height / 2);
    
    // 将主画布内容拷贝到临时变量（用于发送到后端）
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    tempCtx.drawImage(mainCanvas, 0, 0);
    
    // 重置数据
    window.e6Data4bit = null;
    
    // 将临时画布转换为 base64 PNG，发送到后端处理
    const imageDataUrl = tempCanvas.toDataURL('image/png');
    const base64Data = imageDataUrl.split(',')[1];
    
    // 获取算法参数
    const { algorithm, gradThresh } = getAlgorithmParams();
    const algorithmName = algorithmNames[algorithm] || algorithm;
    console.log('实际使用的算法:', algorithm, '算法名称:', algorithmName);
    log(`正在调用后端6色算法处理（${algorithmName}）...`);
    
    // 显示进度条
    showProgress('正在处理图像...');
    updateProgress(10);
    
    // 调用后端 API
    fetch(`${API_BASE}/api/epd/process-sixcolor`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            ...authHeaders()
        },
        body: JSON.stringify({
            imageData: base64Data,
            width: width,
            height: height,
            algorithm: algorithm,
            gradThresh: gradThresh
        })
    })
    .then(response => {
        updateProgress(50);
        return response.json();
    })
    .then(result => {
        updateProgress(70);
        if (result.success) {
            // 加载预览图到画布
            const previewImg = new Image();
            previewImg.onload = () => {
                updateProgress(90);
                ctx.clearRect(0, 0, width, height);
                ctx.drawImage(previewImg, 0, 0);
                
                // 保存处理后的图像数据
                processedImageData = ctx.getImageData(0, 0, width, height);
                
                // 保存4bit数据（base64编码）
                window.e6Data4bit = result.data4bit;
                
                updateProgress(100);
                setTimeout(() => {
                    hideProgress();
                }, 500);
                
                const algorithmName = algorithmNames[algorithm] || algorithm;
                log(`6色处理完成：已使用${algorithmName}映射到6色调色板`, 'success');
            };
            previewImg.onerror = () => {
                hideProgress();
                log('预览图加载失败', 'error');
            };
            previewImg.src = 'data:image/png;base64,' + result.previewImage;
        } else {
            hideProgress();
            log('处理失败: ' + result.error, 'error');
        }
    })
    .catch(error => {
        hideProgress();
        log('处理失败: ' + error.message, 'error');
        console.error(error);
    });
}

// 重写 handleFile 以支持混合模式
function handleFile(file) {
    if (!file.type.startsWith('image/')) {
        log('请选择图片文件', 'error');
        return;
    }
    
    log(`加载图片: ${file.name}`);
    
    const reader = new FileReader();
    reader.onload = (e) => {
        const img = new Image();
        img.onload = () => {
            sourceImage = img;
            
            if (currentMode === 'mixed') {
                // 混合模式：直接刷新混合画布
                fitMixedToScreen();  // 自动适应屏幕
                log(`图片加载成功: ${img.width}x${img.height}`, 'success');
            } else {
                // 图片模式：显示裁剪编辑器
                displaySourceImage(img);
                log(`图片加载成功: ${img.width}x${img.height}`, 'success');
            }
        };
        img.src = e.target.result;
    };
    reader.readAsDataURL(file);
}
