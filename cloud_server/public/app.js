// 全局变量
let sourceImage = null;
let processedImageData = null;
const API_BASE = window.location.origin;

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    initDropZone();
    initFileInput();
    loadDeviceFromURL();
    log('系统初始化完成');
});

// 从URL参数加载设备ID
function loadDeviceFromURL() {
    const params = new URLSearchParams(window.location.search);
    const deviceId = params.get('deviceId');
    
    if (deviceId) {
        document.getElementById('deviceId').value = deviceId;
        
        // 加载设备信息
        const devices = JSON.parse(localStorage.getItem('esp32_devices') || '[]');
        const device = devices.find(d => d.id === deviceId);
        
        if (device) {
            document.getElementById('deviceName').textContent = device.name;
        } else {
            document.getElementById('deviceName').textContent = deviceId;
        }
    }
}

// 日志函数
function log(message, type = 'info') {
    const statusBar = document.getElementById('statusBar');
    const timestamp = new Date().toLocaleTimeString();
    const emoji = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
    statusBar.textContent = `[${timestamp}] ${emoji} ${message}`;
    console.log(`[${timestamp}] ${message}`);
}

// 初始化拖拽区域
function initDropZone() {
    const dropZone = document.getElementById('dropZone');
    
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
    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) {
            handleFile(e.target.files[0]);
        }
    });
}

// 处理文件
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
            displaySourceImage(img);
            log(`图片加载成功: ${img.width}x${img.height}`, 'success');
        };
        img.src = e.target.result;
    };
    reader.readAsDataURL(file);
}

// 显示原始图片
function displaySourceImage(img) {
    const canvas = document.getElementById('sourceCanvas');
    const ctx = canvas.getContext('2d');
    
    canvas.width = img.width;
    canvas.height = img.height;
    ctx.drawImage(img, 0, 0);
    
    canvas.style.display = 'block';
    document.getElementById('sourceImage').style.display = 'none';
    
    // 自动设置宽高
    document.getElementById('width').value = img.width;
    document.getElementById('height').value = img.height;
}

// 处理图片
function processImage() {
    if (!sourceImage) {
        log('请先选择图片', 'error');
        return;
    }
    
    log('开始处理图片...');
    
    const sourceCanvas = document.getElementById('sourceCanvas');
    const processedCanvas = document.getElementById('processedCanvas');
    
    // 获取参数
    const offsetX = parseInt(document.getElementById('offsetX').value) || 0;
    const offsetY = parseInt(document.getElementById('offsetY').value) || 0;
    const width = parseInt(document.getElementById('width').value);
    const height = parseInt(document.getElementById('height').value);
    const processType = document.querySelector('input[name="processType"]:checked').value;
    
    if (width < 3 || height < 3) {
        log('图片尺寸太小', 'error');
        return;
    }
    
    // 设置处理后画布大小
    processedCanvas.width = width;
    processedCanvas.height = height;
    const ctx = processedCanvas.getContext('2d');
    
    // 绘制图片
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(sourceCanvas, offsetX, offsetY, width, height, 0, 0, width, height);
    
    // 获取图像数据
    const imageData = ctx.getImageData(0, 0, width, height);
    const data = imageData.data;
    
    // 转换为灰度
    for (let i = 0; i < data.length; i += 4) {
        const gray = data[i] * 0.299 + data[i + 1] * 0.587 + data[i + 2] * 0.114;
        data[i] = data[i + 1] = data[i + 2] = gray;
    }
    
    // 应用不同的处理算法
    if (processType === 'dither_mono' || processType === 'dither_red') {
        // Floyd-Steinberg抖动算法
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const idx = (y * width + x) * 4;
                const oldPixel = data[idx];
                const newPixel = oldPixel < 128 ? 0 : 255;
                const error = oldPixel - newPixel;
                
                data[idx] = data[idx + 1] = data[idx + 2] = newPixel;
                
                // 扩散误差
                if (x + 1 < width) {
                    const i = (y * width + x + 1) * 4;
                    data[i] = data[i + 1] = data[i + 2] = data[i] + error * 7 / 16;
                }
                if (y + 1 < height) {
                    if (x > 0) {
                        const i = ((y + 1) * width + x - 1) * 4;
                        data[i] = data[i + 1] = data[i + 2] = data[i] + error * 3 / 16;
                    }
                    const i = ((y + 1) * width + x) * 4;
                    data[i] = data[i + 1] = data[i + 2] = data[i] + error * 5 / 16;
                    
                    if (x + 1 < width) {
                        const i = ((y + 1) * width + x + 1) * 4;
                        data[i] = data[i + 1] = data[i + 2] = data[i] + error * 1 / 16;
                    }
                }
            }
        }
    } else if (processType === 'level_mono') {
        // Level: mono - 简单黑白阈值
        for (let i = 0; i < data.length; i += 4) {
            const value = data[i] < 128 ? 0 : 255;
            data[i] = data[i + 1] = data[i + 2] = value;
        }
    } else if (processType === 'level_gray') {
        // Level: gray - 灰度（4级灰度）
        for (let i = 0; i < data.length; i += 4) {
            const gray = data[i];
            let value;
            if (gray < 64) value = 0;
            else if (gray < 128) value = 85;
            else if (gray < 192) value = 170;
            else value = 255;
            data[i] = data[i + 1] = data[i + 2] = value;
        }
    }
    
    ctx.putImageData(imageData, 0, 0);
    processedImageData = imageData;
    
    log('图片处理完成', 'success');
}

// 上传到设备
async function uploadToDevice() {
    if (!processedImageData) {
        log('请先处理图片', 'error');
        return;
    }
    
    const deviceId = document.getElementById('deviceId').value.trim();
    if (!deviceId) {
        log('请输入设备ID', 'error');
        return;
    }
    
    const epdType = parseInt(document.getElementById('epdType').value);
    
    try {
        log('正在初始化墨水屏...');
        
        // 1. 初始化EPD
        const initResponse = await fetch(`${API_BASE}/api/epd/init`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ deviceId, epdType })
        });
        
        if (!initResponse.ok) {
            throw new Error('初始化失败: ' + await initResponse.text());
        }
        
        const initResult = await initResponse.json();
        console.log('EPD初始化响应:', initResult);
        
        await sleep(500);
        
        // 2. 转换图像数据为字节数组
        const width = processedImageData.width;
        const height = processedImageData.height;
        const data = processedImageData.data;
        
        log(`正在上传图像数据 (${width}x${height})...`);
        
        // 转换为像素数组（原版格式：0=黑色, 1=白色）
        const pixelArray = [];
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const idx = (y * width + x) * 4;
                const gray = data[idx];
                // 0=黑色, 1=白色（原版格式）
                pixelArray.push(gray < 128 ? 0 : 1);
            }
        }
        
        // 使用原版的u_data函数逻辑将像素数组转为字节（c=0）
        const byteArray = [];
        for (let i = 0; i < pixelArray.length; i += 8) {
            let byte = 0;
            for (let bit = 0; bit < 8 && (i + bit) < pixelArray.length; bit++) {
                // 如果像素不等于0（即不是黑色），设置该位
                if (pixelArray[i + bit] != 0) {
                    byte |= (128 >> bit);
                }
            }
            byteArray.push(byte);
        }
        
        // 编码为字符串格式（原系统格式）
        // 每个字节编码成两个字符：'a'-'p' 表示 0-15
        function byteToStr(byte) {
            const low = byte & 0x0F;
            const high = (byte >> 4) & 0x0F;
            return String.fromCharCode(97 + low) + String.fromCharCode(97 + high);
        }
        
        // 检查是否需要翻转图像（某些屏幕使用EPD_loadAFilp）
        // EPD类型: 22(7.5"V2), 23(7.5"B V2), 24(7.5"B HD), 29(5.83"B V2), 
        //         31(1.54"B V2), 35(2.66"b), 36(5.83"V2), 38(2.7"B V2), 
        //         47(2.9"b V4), 48(13.3"b)
        const needsFlip = [22, 23, 24, 29, 31, 35, 36, 38, 47, 48].includes(epdType);
        
        let dataString = '';
        if (needsFlip) {
            // 倒序编码（从末尾开始）
            for (let i = byteArray.length - 1; i >= 0; i--) {
                dataString += byteToStr(byteArray[i]);
            }
        } else {
            // 正序编码
            for (let i = 0; i < byteArray.length; i++) {
                dataString += byteToStr(byteArray[i]);
            }
        }
        
        // wordToStr: 将16位整数编码为4个字符
        function wordToStr(value) {
            const lowByte = value & 0xFF;
            const highByte = (value >> 8) & 0xFF;
            return byteToStr(lowByte) + byteToStr(highByte);
        }
        
        console.log(`📊 图像统计: ${width}x${height}, 像素数:${pixelArray.length}, 字节数:${byteArray.length}, 编码后:${dataString.length}字符`);
        console.log(`🔤 数据示例(前30字符): ${dataString.substring(0, 30)}`);
        console.log(`🔤 数据示例(后30字符): ${dataString.substring(dataString.length - 30)}`);
        
        // 分块发送数据（字符串格式 + 长度后缀）
        const chunkSize = 1000;  // 1000个字符 = 500个字节
        for (let i = 0; i < dataString.length; i += chunkSize) {
            const chunk = dataString.substring(i, i + chunkSize);
            const progress = Math.round((i / dataString.length) * 100);
            log(`上传进度: ${progress}%`);
            
            // 添加长度后缀（4个字符）
            const chunkWithLength = chunk + wordToStr(chunk.length);
            
            console.log(`📤 发送块 ${Math.floor(i/chunkSize)+1}: 数据${chunk.length}字符 + 长度后缀4字符 = ${chunkWithLength.length}字符`);
            
            const loadResponse = await fetch(`${API_BASE}/api/epd/load`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    deviceId, 
                    data: chunkWithLength,  // 包含长度后缀的字符串
                    length: chunkWithLength.length
                })
            });
            
            if (!loadResponse.ok) {
                throw new Error('数据发送失败: ' + await loadResponse.text());
            }
            
            await sleep(100);
        }
        
        log('上传进度: 100%');
        
        // 3. 显示
        log('正在刷新显示...');
        const showResponse = await fetch(`${API_BASE}/api/epd/show`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ deviceId })
        });
        
        if (!showResponse.ok) {
            throw new Error('显示命令失败: ' + await showResponse.text());
        }
        
        log('上传完成！', 'success');
        
    } catch (error) {
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
            headers: { 'Content-Type': 'application/json' },
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

// 根据EPD型号自动设置分辨率
function updateResolution() {
    const epdType = parseInt(document.getElementById('epdType').value);
    const resolutions = {
        0: [200, 200],   // 1.54"
        1: [200, 200],   // 1.54" B
        3: [122, 250],   // 2.13"
        6: [104, 212],   // 2.13" B
        9: [128, 296],   // 2.9"
        13: [400, 300],  // 4.2"
        14: [400, 300],  // 4.2" B
        16: [600, 448],  // 5.83"
        19: [640, 384],  // 7.5"
        22: [800, 480],  // 7.5" V2 ⭐
        23: [800, 480],  // 7.5" B V2
        26: [880, 528],  // 7.5" HD
    };
    
    if (resolutions[epdType]) {
        document.getElementById('width').value = resolutions[epdType][0];
        document.getElementById('height').value = resolutions[epdType][1];
        log(`已设置分辨率: ${resolutions[epdType][0]}x${resolutions[epdType][1]}`);
    }
}
