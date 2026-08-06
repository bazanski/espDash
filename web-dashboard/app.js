/**
 * espDash - Honda Civic 9th Gen Web Telemetry Dashboard App
 */

// DOM Elements
const connectionMode = document.getElementById('connectionMode');
const wsHost = document.getElementById('wsHost');
const btnConnect = document.getElementById('btnConnect');
const btnRecord = document.getElementById('recordLabel') ? document.getElementById('btnRecord') : null;
const recordDot = document.getElementById('recordDot');
const recordLabel = document.getElementById('recordLabel');
const recordTimer = document.getElementById('recordTimer');
const statusBadge = document.getElementById('statusBadge');
const statusText = document.getElementById('statusText');

// Gauges & Readouts
const tachFillArc = document.getElementById('tachFillArc');
const valRpm = document.getElementById('valRpm');
const shiftAlert = document.getElementById('shiftAlert');

const valSpeed = document.getElementById('valSpeed');
const valGear = document.getElementById('valGear');
const valEngineRunning = document.getElementById('valEngineRunning');

const valWaterTemp = document.getElementById('valWaterTemp');
const fillWaterTemp = document.getElementById('fillWaterTemp');

const valOilTemp = document.getElementById('valOilTemp');
const fillOilTemp = document.getElementById('fillOilTemp');
const overheatAlert = document.getElementById('overheatAlert');

const valBattery = document.getElementById('valBattery');
const valFuel = document.getElementById('valFuel');
const valThrottle = document.getElementById('valThrottle');
const valSteering = document.getElementById('valSteering');
const valBrake = document.getElementById('valBrake');
const valAmbient = document.getElementById('valAmbient');

// Sniffer Table & Gateway Toggle
const snifferTableBody = document.getElementById('snifferTableBody');
const tabToggleModeBtn = document.getElementById('tabToggleModeBtn');

// State Variables
let isConnected = false;
let socket = null;
let serialPort = null;
let serialReader = null;
let gatewayMode = 'PLOT'; // PLOT or RAW

// Recording State Variables
let isRecording = false;
let recordedPackets = [];
let recordStartTime = 0;
let recordTimerInterval = null;

// CAN ID Map for Sniffer Frequency calculation
const canFrameMap = new Map(); // id -> { id, dlc, bytes, count, lastTime, freq }
const MAX_SNIFFER_ROWS = 500;

// Gear Label Mapper
const GEAR_MAP = ['P', 'R', 'N', 'D', 'S', '1', '2', '3', '4', '5', '6'];

// Simulation Variables
let simInterval = null;
let simTime = 0;

// =========================================================================
// CONNECT & DISCONNECT CONTROLLER
// =========================================================================
btnConnect.addEventListener('click', async () => {
    if (isConnected) {
        disconnect();
    } else {
        const mode = connectionMode.value;
        if (mode === 'demo') {
            startDemoSimulation();
        } else if (mode === 'websocket') {
            connectWebSocket();
        } else if (mode === 'webserial') {
            await connectWebSerial();
        }
    }
});

// =========================================================================
// DEMO / TELEMETRY SIMULATION ENGINE
// =========================================================================
function startDemoSimulation() {
    setConnectedState(true);
    statusText.textContent = 'Simulating Telemetry';
    simTime = 0;

    simInterval = setInterval(() => {
        simTime += 0.05;

        // Smooth RPM Sweep (750 to 7400 RPM with gear shifts)
        const cycle = (simTime % 12.0) / 12.0; // 12 second acceleration cycle
        let rpm = 0;
        let gearIdx = 3;
        let speed = 0;

        if (cycle < 0.2) {
            gearIdx = 1; // 1st Gear
            rpm = 1000 + (cycle / 0.2) * 5800; // 1000 - 6800
            speed = (rpm / 6800) * 35;
        } else if (cycle < 0.45) {
            gearIdx = 2; // 2nd Gear
            const progress = (cycle - 0.2) / 0.25;
            rpm = 3200 + progress * 4000; // 3200 - 7200 (Shift Warning!)
            speed = 35 + progress * 35;
        } else if (cycle < 0.75) {
            gearIdx = 3; // 3rd Gear
            const progress = (cycle - 0.45) / 0.30;
            rpm = 3800 + progress * 3400; // 3800 - 7200
            speed = 70 + progress * 40;
        } else {
            gearIdx = 4; // 4th Gear Cruising
            const progress = (cycle - 0.75) / 0.25;
            rpm = 3500 - progress * 1000; // 3500 - 2500
            speed = 110 - progress * 20;
        }

        const throttle = Math.round(Math.max(0, Math.sin(simTime * 1.5)) * 100);
        const waterTemp = 88.0 + 3.0 * Math.sin(simTime * 0.1);
        const oilTemp = 92.0 + 4.0 * Math.sin(simTime * 0.08);
        const batteryV = 13.8 + 0.3 * Math.sin(simTime * 0.5);
        const steering = Math.round(60.0 * Math.sin(simTime * 0.4));
        const brake = Math.round(Math.max(0, Math.sin(simTime * 0.8 + 1.5)) * 30);
        const nowMs = Math.round(simTime * 1000);

        const telemetryData = {
            type: 'telemetry',
            rpm: Math.round(rpm),
            speed: parseFloat(speed.toFixed(1)),
            water_temp: parseFloat(waterTemp.toFixed(1)),
            oil_temp: parseFloat(oilTemp.toFixed(1)),
            battery_v: parseFloat(batteryV.toFixed(2)),
            gear: gearIdx,
            fuel: 76,
            throttle: throttle,
            steering: steering,
            brake: brake,
            ambient: 22,
            timestamp: nowMs
        };

        // Update Gauges
        updateTelemetryUI(telemetryData);

        // Record data if recording active
        if (isRecording) {
            recordedPackets.push(`${new Date().toISOString()},${JSON.stringify(telemetryData)}`);
        }

        // Generate Simulated Raw CAN Frames for Sniffer Table
        const rpmHex1 = ((Math.round(rpm) >> 8) & 0xFF).toString(16).padStart(2, '0').toUpperCase();
        const rpmHex2 = (Math.round(rpm) & 0xFF).toString(16).padStart(2, '0').toUpperCase();
        parseRawCanFrame(`RAW,${nowMs},0x17C,0,8,00,00,${rpmHex1},${rpmHex2},00,00,00,19`);

        const spdHex = Math.round(speed * 2).toString(16).padStart(2, '0').toUpperCase();
        const gearHex = gearIdx.toString(16).padStart(2, '0').toUpperCase();
        parseRawCanFrame(`RAW,${nowMs},0x156,0,5,FF,${spdHex},00,02,${gearHex}`);

        parseRawCanFrame(`RAW,${nowMs},0x1A4,0,8,00,${throttle.toString(16).padStart(2,'0').toUpperCase()},00,00,00,00,00,3A`);
        parseRawCanFrame(`RAW,${nowMs},0x1D0,0,8,00,80,00,00,00,00,00,0A`);
        parseRawCanFrame(`RAW,${nowMs},0x309,0,8,00,8A,00,00,00,00,00,0C`);

    }, 50);
}

function setConnectedState(connected) {
    isConnected = connected;
    if (connected) {
        statusBadge.className = 'status-badge connected';
        statusText.textContent = 'Connected';
        btnConnect.textContent = 'Disconnect';
        btnConnect.className = 'btn btn-danger';
        btnRecord.disabled = false;
    } else {
        statusBadge.className = 'status-badge disconnected';
        statusText.textContent = 'Disconnected';
        btnConnect.textContent = 'Connect';
        btnConnect.className = 'btn btn-primary';
        btnRecord.disabled = true;
        if (isRecording) stopRecording();
    }
}

// =========================================================================
// WEBSOCKET CONNECTION ENGINE
// =========================================================================
function connectWebSocket() {
    let host = wsHost.value.trim();
    if (!host.startsWith('ws://') && !host.startsWith('wss://')) {
        host = 'ws://' + host;
    }

    statusBadge.className = 'status-badge';
    statusText.textContent = 'Connecting...';

    try {
        socket = new WebSocket(host);

        socket.onopen = () => {
            console.log('[WebSocket] Connected to', host);
            setConnectedState(true);
        };

        socket.onmessage = (event) => {
            parseIncomingLine(event.data);
        };

        socket.onclose = () => {
            console.log('[WebSocket] Disconnected');
            setConnectedState(false);
        };

        socket.onerror = (err) => {
            console.error('[WebSocket Error]', err);
            setConnectedState(false);
        };
    } catch (e) {
        alert('WebSocket Connection Error: ' + e.message);
        setConnectedState(false);
    }
}

// =========================================================================
// WEBSERIAL USB CONNECTION ENGINE
// =========================================================================
async function connectWebSerial() {
    if (!('serial' in navigator)) {
        alert('WebSerial is not supported in this browser. Use Chrome or Edge.');
        return;
    }

    try {
        serialPort = await navigator.serial.requestPort();
        await serialPort.open({ baudRate: 115200 });

        setConnectedState(true);

        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = serialPort.readable.pipeTo(textDecoder.writable);
        serialReader = textDecoder.readable.getReader();

        let buffer = '';
        while (true) {
            const { value, done } = await serialReader.read();
            if (done) {
                serialReader.releaseLock();
                break;
            }
            if (value) {
                buffer += value;
                const lines = buffer.split('\n');
                buffer = lines.pop(); // keep partial line
                for (const line of lines) {
                    parseIncomingLine(line);
                }
            }
        }
    } catch (err) {
        console.error('[WebSerial Error]', err);
        setConnectedState(false);
    }
}

function disconnect() {
    if (socket) {
        socket.close();
        socket = null;
    }
    if (serialReader) {
        serialReader.cancel();
        serialReader = null;
    }
    if (serialPort) {
        serialPort.close();
        serialPort = null;
    }
    setConnectedState(false);
}

// Send Command String to Gateway
function sendCommand(cmd) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(cmd + '\n');
    }
}

// =========================================================================
// GATEWAY MODE TOGGLE BUTTON (PLOT / RAW)
// =========================================================================
tabToggleModeBtn.addEventListener('click', () => {
    if (gatewayMode === 'PLOT') {
        gatewayMode = 'RAW';
        sendCommand('MODE:RAW');
        tabToggleModeBtn.textContent = 'Gateway Mode: RAW SNIFFER (Click to switch to PLOT)';
    } else {
        gatewayMode = 'PLOT';
        sendCommand('MODE:PLOT');
        tabToggleModeBtn.textContent = 'Gateway Mode: TELEMETRY PLOT (Click to switch to RAW)';
    }
});

// =========================================================================
// DATA PARSER ENGINE (JSON & RAW CAN)
// =========================================================================
function parseIncomingLine(line) {
    line = line.trim();
    if (!line) return;

    // A. Parse Decoded Telemetry JSON
    if (line.startsWith('{') && line.endsWith('}')) {
        try {
            const data = JSON.parse(line);
            if (data.type === 'telemetry') {
                updateTelemetryUI(data);
            }
        } catch (e) {}
    } 
    // B. Parse Raw CAN Frame Line ("RAW,timestamp,id,rtr,dlc,bytes...")
    else if (line.startsWith('RAW,')) {
        parseRawCanFrame(line);
    }

    // Record data if recording is active
    if (isRecording) {
        recordedPackets.push(`${new Date().toISOString()},${line}`);
    }
}

// =========================================================================
// TELEMETRY UI UPDATER
// =========================================================================
function updateTelemetryUI(data) {
    // 1. Tachometer (RPM)
    const rpm = data.rpm || 0;
    valRpm.textContent = rpm;

    // Arc dashoffset: 377 is total length (0 RPM = 377, 9000 RPM = 0)
    const maxRpm = 9000;
    const rpmClamped = Math.min(Math.max(rpm, 0), maxRpm);
    const dashOffset = 377 - (rpmClamped / maxRpm) * 377;
    tachFillArc.style.strokeDashoffset = dashOffset;

    // Shift Light Alert (> 6800 RPM)
    if (rpm > 6800) {
        shiftAlert.style.display = 'inline-block';
    } else {
        shiftAlert.style.display = 'none';
    }

    // Engine Running Indicator
    if (rpm > 400) {
        valEngineRunning.style.display = 'inline-block';
    } else {
        valEngineRunning.style.display = 'none';
    }

    // 2. Speedometer & Gear
    valSpeed.textContent = (data.speed || 0).toFixed(0);
    const gearIdx = data.gear || 0;
    valGear.textContent = GEAR_MAP[gearIdx] || 'P';

    // 3. Coolant Temp (°C)
    const waterTemp = data.water_temp || 0;
    valWaterTemp.textContent = `${waterTemp.toFixed(1)} °C`;
    const waterPct = Math.min(Math.max(((waterTemp + 20) / 160) * 100, 0), 100);
    fillWaterTemp.style.width = `${waterPct}%`;
    if (waterTemp > 105) {
        fillWaterTemp.className = 'progress-fill warning';
        overheatAlert.style.display = 'inline-block';
    } else {
        fillWaterTemp.className = 'progress-fill';
        overheatAlert.style.display = 'none';
    }

    // 4. Engine Oil Temp (°C)
    const oilTemp = data.oil_temp || 0;
    valOilTemp.textContent = `${oilTemp.toFixed(1)} °C`;
    const oilPct = Math.min(Math.max(((oilTemp + 20) / 160) * 100, 0), 100);
    fillOilTemp.style.width = `${oilPct}%`;
    if (oilTemp > 120) {
        fillOilTemp.className = 'progress-fill warning';
    } else {
        fillOilTemp.className = 'progress-fill';
    }

    // 5. Secondary Grid Readouts
    valBattery.textContent = `${(data.battery_v || 0).toFixed(1)} V`;
    valFuel.textContent = `${data.fuel || 0} %`;
    valThrottle.textContent = `${data.throttle || 0} %`;
    valSteering.textContent = `${data.steering || 0}°`;
    valBrake.textContent = `${data.brake || 0} Bar`;
    valAmbient.textContent = `${data.ambient || 0} °C`;
}

// =========================================================================
// RAW CAN SNIFFER TABLE UPDATER (MEMORY-SAFE RING BUFFER)
// =========================================================================
function parseRawCanFrame(line) {
    // RAW,timestamp,id,rtr,dlc,bytes...
    const parts = line.split(',');
    if (parts.length < 5) return;

    const timestamp = parts[1];
    const canId = parts[2];
    const rtr = parts[3];
    const dlc = parts[4];
    const bytes = parts.slice(5).join(' ');

    const now = Date.now();
    let entry = canFrameMap.get(canId);
    if (!entry) {
        entry = { canId, dlc, bytes, count: 1, lastTime: now, freq: 0, ts: timestamp };
        canFrameMap.set(canId, entry);
    } else {
        entry.count++;
        const dt = (now - entry.lastTime) / 1000.0;
        if (dt > 0.1) {
            entry.freq = (1.0 / dt).toFixed(1);
            entry.lastTime = now;
        }
        entry.dlc = dlc;
        entry.bytes = bytes;
        entry.ts = timestamp;
    }

    renderSnifferTable();
}

function renderSnifferTable() {
    let html = '';
    canFrameMap.forEach((val) => {
        html += `<tr>
            <td>${val.ts}</td>
            <td class="can-id-tag">${val.canId}</td>
            <td>${val.dlc}</td>
            <td class="can-data-tag">${val.bytes}</td>
            <td>${val.freq > 0 ? val.freq + ' Hz' : '--'}</td>
        </tr>`;
    });
    snifferTableBody.innerHTML = html;
}

// =========================================================================
// RAW CAN LOG RECORDING & CSV EXPORT ENGINE
// =========================================================================
btnRecord.addEventListener('click', () => {
    if (!isRecording) {
        startRecording();
    } else {
        stopRecording();
    }
});

function startRecording() {
    isRecording = true;
    recordedPackets = [];
    recordedPackets.push('ISO_Timestamp,Raw_Payload');
    recordStartTime = Date.now();

    btnRecord.className = 'btn btn-record recording';
    recordLabel.textContent = 'Stop & Save';
    recordTimer.style.display = 'inline';

    recordTimerInterval = setInterval(() => {
        const elapsedSec = Math.floor((Date.now() - recordStartTime) / 1000);
        const mins = String(Math.floor(elapsedSec / 60)).padStart(2, '0');
        const secs = String(elapsedSec % 60).padStart(2, '0');
        recordTimer.textContent = `${mins}:${secs} (${recordedPackets.length - 1})`;
    }, 500);
}

function stopRecording() {
    isRecording = false;
    clearInterval(recordTimerInterval);

    btnRecord.className = 'btn btn-record';
    recordLabel.textContent = 'Start Record';
    recordTimer.style.display = 'none';

    if (recordedPackets.length <= 1) {
        alert('No data recorded.');
        return;
    }

    // Generate CSV Blob & Instant Download
    const csvContent = recordedPackets.join('\n');
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);

    const nowStr = new Date().toISOString().replace(/[:.]/g, '-');
    const filename = `espDash_can_log_${nowStr}.csv`;

    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    // Flush Memory
    recordedPackets = [];
    console.log(`[Recording] Exported ${filename} successfully.`);
}
