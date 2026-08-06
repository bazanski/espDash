/**
 * espDash - Honda Civic 9th Gen Web Telemetry Dashboard App
 */

// DOM Elements
const connectionMode = document.getElementById('connectionMode');
const wsHost = document.getElementById('wsHost');
const btnConnect = document.getElementById('btnConnect');
const btnRecord = document.getElementById('btnRecord');
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

// Tabs & Containers
const tabPlotBtn = document.getElementById('tabPlotBtn');
const tabSnifferBtn = document.getElementById('tabSnifferBtn');
const tabToggleModeBtn = document.getElementById('tabToggleModeBtn');
const plotContainer = document.getElementById('plotContainer');
const snifferContainer = document.getElementById('snifferContainer');
const snifferTableBody = document.getElementById('snifferTableBody');
const chartCanvas = document.getElementById('telemetryChartCanvas');

// State Variables
let isConnected = false;
let socket = null;
let serialPort = null;
let serialReader = null;
let gatewayMode = 'PLOT'; // PLOT or RAW

// Simulation Variables
let simInterval = null;
let simTime = 0;

// Recording State Variables
let isRecording = false;
let recordedPackets = [];
let recordStartTime = 0;
let recordTimerInterval = null;

// CAN ID Map for Sniffer Frequency calculation
const canFrameMap = new Map();

// Gear Label Mapper
const GEAR_MAP = ['P', 'R', 'N', 'D', 'S', '1', '2', '3', '4', '5', '6'];

// Chart Data History Buffer (Max 100 points)
const chartHistory = [];
const MAX_CHART_POINTS = 100;

// Dynamic SVG Path Arc Length Calculation
let tachArcLength = 360;
if (tachFillArc) {
    try {
        tachArcLength = tachFillArc.getTotalLength();
        tachFillArc.style.strokeDasharray = tachArcLength;
        tachFillArc.style.strokeDashoffset = tachArcLength;
    } catch (e) {}
}

// =========================================================================
// TAB NAVIGATION CONTROLLER
// =========================================================================
tabPlotBtn.addEventListener('click', () => {
    tabPlotBtn.classList.add('active');
    tabSnifferBtn.classList.remove('active');
    plotContainer.style.display = 'block';
    snifferContainer.style.display = 'none';
});

tabSnifferBtn.addEventListener('click', () => {
    tabSnifferBtn.classList.add('active');
    tabPlotBtn.classList.remove('active');
    snifferContainer.style.display = 'block';
    plotContainer.style.display = 'none';
});

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
            gearIdx = 1;
            rpm = 1000 + (cycle / 0.2) * 5800; // 1000 - 6800
            speed = (rpm / 6800) * 35;
        } else if (cycle < 0.45) {
            gearIdx = 2;
            const progress = (cycle - 0.2) / 0.25;
            rpm = 3200 + progress * 4000; // 3200 - 7200 (Shift Warning!)
            speed = 35 + progress * 35;
        } else if (cycle < 0.75) {
            gearIdx = 3;
            const progress = (cycle - 0.45) / 0.30;
            rpm = 3800 + progress * 3400; // 3800 - 7200
            speed = 70 + progress * 40;
        } else {
            gearIdx = 4;
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

        // Wheel speeds simulation (with front wheel spin during hard acceleration)
        let slipOffset = (throttle > 70) ? (throttle - 70) * 0.15 : 0;
        let w_fl = speed + slipOffset;
        let w_fr = speed + slipOffset * 0.9;
        let w_rl = speed;
        let w_rr = speed;

        // Warning flags simulation
        let isBrakeSw = (throttle < 5 && speed > 20 && (simTime % 12 > 9));
        let isTC = (slipOffset > 3.0);
        let isABS = (isBrakeSw && speed > 50);

        const telemetryData = {
            type: 'telemetry',
            rpm: Math.round(rpm),
            speed: parseFloat(speed.toFixed(1)),
            water_temp: parseFloat(waterTemp.toFixed(1)),
            oil_temp: parseFloat(oilTemp.toFixed(1)),
            battery_v: parseFloat(batteryV.toFixed(2)),
            gear: gearIdx,
            fuel: 76,
            throttle: Math.round(throttle),
            steering: Math.round(steering),
            brake: isBrakeSw ? 35 : 0,
            ambient: 22,
            abs: isABS,
            tc: isTC,
            brake_sw: isBrakeSw,
            cel: false,
            vsa_warn: isTC,
            w_fl: parseFloat(w_fl.toFixed(1)),
            w_fr: parseFloat(w_fr.toFixed(1)),
            w_rl: parseFloat(w_rl.toFixed(1)),
            w_rr: parseFloat(w_rr.toFixed(1)),
            timestamp: nowMs
        };

        // Update Gauges & Plot
        updateTelemetryUI(telemetryData);

        // Record data if active
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
                buffer = lines.pop();
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
    if (simInterval) {
        clearInterval(simInterval);
        simInterval = null;
    }
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
    resetUI();
}

function resetUI() {
    updateTelemetryUI({
        rpm: 0,
        speed: 0,
        water_temp: -20,
        oil_temp: -20,
        battery_v: 0,
        gear: 0,
        fuel: 0,
        throttle: 0,
        steering: 0,
        brake: 0,
        ambient: 0
    });
    valWaterTemp.textContent = '-- °C';
    valOilTemp.textContent = '-- °C';
    valAmbient.textContent = '-- °C';
}

function sendCommand(cmd) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(cmd + '\n');
    }
}

// Gateway Mode Toggle Button (PLOT / RAW)
tabToggleModeBtn.addEventListener('click', () => {
    if (gatewayMode === 'PLOT') {
        gatewayMode = 'RAW';
        sendCommand('MODE:RAW');
        tabToggleModeBtn.textContent = 'Gateway Mode: RAW SNIFFER (Click for TELEMETRY)';
    } else {
        gatewayMode = 'PLOT';
        sendCommand('MODE:PLOT');
        tabToggleModeBtn.textContent = 'Gateway Mode: TELEMETRY (Click for RAW)';
    }
});

// =========================================================================
// DATA PARSER ENGINE
// =========================================================================
function parseIncomingLine(line) {
    line = line.trim();
    if (!line) return;

    if (line.startsWith('{') && line.endsWith('}')) {
        try {
            const data = JSON.parse(line);
            if (data.type === 'telemetry') {
                updateTelemetryUI(data);
            }
        } catch (e) {}
    } else if (line.startsWith('RAW,')) {
        parseRawCanFrame(line);
    }

    if (isRecording) {
        recordedPackets.push(`${new Date().toISOString()},${line}`);
    }
}

// =========================================================================
// TELEMETRY UI & EXACT TACHOMETER ALIGNMENT UPDATER
// =========================================================================
function updateTelemetryUI(data) {
    // 1. Tachometer (RPM) & Mathematical Arc Alignment
    const rpm = data.rpm || 0;
    valRpm.textContent = rpm;

    const maxRpm = 9000;
    const rpmClamped = Math.min(Math.max(rpm, 0), maxRpm);

    // Exact dashoffset calculation matching total arc length (0 RPM = full offset, 9000 RPM = 0 offset)
    const dashOffset = tachArcLength - (rpmClamped / maxRpm) * tachArcLength;
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

    // 2. Speedometer & Direct CAN Gear Display
    valSpeed.textContent = (data.speed || 0).toFixed(0);
    const gearRaw = data.gear || 0;
    const DIRECT_GEAR_MAP = {
        0: 'P',
        1: 'P',
        2: 'R',
        3: 'N',
        4: 'D',
        5: 'S',
        6: '1',
        7: '2',
        8: '3',
        9: '4',
        10: '5',
        11: '6'
    };
    valGear.textContent = DIRECT_GEAR_MAP[gearRaw] || String(gearRaw);

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

    // 6. System Warning Badges
    const badgeCEL = document.getElementById('badgeCEL');
    const badgeABS = document.getElementById('badgeABS');
    const badgeTC = document.getElementById('badgeTC');
    const badgeVSA = document.getElementById('badgeVSA');
    const badgeBrakeSw = document.getElementById('badgeBrakeSw');

    if (badgeCEL) badgeCEL.className = data.cel ? 'status-badge status-on-alert' : 'status-badge status-off';
    if (badgeABS) badgeABS.className = data.abs ? 'status-badge status-on-alert' : 'status-badge status-off';
    if (badgeTC) badgeTC.className = data.tc ? 'status-badge status-on-warn' : 'status-badge status-off';
    if (badgeVSA) badgeVSA.className = data.vsa_warn ? 'status-badge status-on-warn' : 'status-badge status-off';
    if (badgeBrakeSw) badgeBrakeSw.className = data.brake_sw ? 'status-badge status-on-info' : 'status-badge status-off';

    // 7. Individual 4-Wheel Speeds (FL, FR, RL, RR)
    const valWheelFL = document.getElementById('valWheelFL');
    const valWheelFR = document.getElementById('valWheelFR');
    const valWheelRL = document.getElementById('valWheelRL');
    const valWheelRR = document.getElementById('valWheelRR');
    const slipStatus = document.getElementById('slipStatus');

    const wFL = data.w_fl !== undefined ? data.w_fl : (data.speed || 0);
    const wFR = data.w_fr !== undefined ? data.w_fr : (data.speed || 0);
    const wRL = data.w_rl !== undefined ? data.w_rl : (data.speed || 0);
    const wRR = data.w_rr !== undefined ? data.w_rr : (data.speed || 0);

    if (valWheelFL) valWheelFL.textContent = wFL.toFixed(1);
    if (valWheelFR) valWheelFR.textContent = wFR.toFixed(1);
    if (valWheelRL) valWheelRL.textContent = wRL.toFixed(1);
    if (valWheelRR) valWheelRR.textContent = wRR.toFixed(1);

    // Detect wheel slip (diff > 5 km/h)
    const speeds = [wFL, wFR, wRL, wRR];
    const maxSpd = Math.max(...speeds);
    const minSpd = Math.min(...speeds);
    if (maxSpd > 10 && (maxSpd - minSpd) > 4.5) {
        if (slipStatus) slipStatus.style.display = 'inline-block';
    } else {
        if (slipStatus) slipStatus.style.display = 'none';
    }

    // 8. Push to Chart History & Render Plot
    chartHistory.push(data);
    if (chartHistory.length > MAX_CHART_POINTS) {
        chartHistory.shift();
    }
    renderTelemetryChart();
}

// =========================================================================
// HTML5 CANVAS LIVE TELEMETRY PLOTTER
// =========================================================================
function renderTelemetryChart() {
    if (!chartCanvas || plotContainer.style.display === 'none') return;

    const ctx = chartCanvas.getContext('2d');
    const width = chartCanvas.parentElement.clientWidth - 16;
    const height = 240;

    chartCanvas.width = width;
    chartCanvas.height = height;

    // Clear Canvas
    ctx.clearRect(0, 0, width, height);

    if (chartHistory.length < 2) return;

    // Draw Grid Lines
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    ctx.lineWidth = 1;
    for (let y = 0; y <= height; y += 40) {
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(width, y);
        ctx.stroke();
    }

    const stepX = width / (MAX_CHART_POINTS - 1);

    // 1. Draw RPM Line (Cyan, 0 - 9000 RPM)
    ctx.beginPath();
    ctx.strokeStyle = '#00f0ff';
    ctx.lineWidth = 2.5;
    chartHistory.forEach((pt, idx) => {
        const x = idx * stepX;
        const normRpm = (pt.rpm || 0) / 9000.0;
        const y = height - normRpm * (height - 20) - 10;
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // 2. Draw Speed Line (White, 0 - 200 km/h)
    ctx.beginPath();
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2.0;
    chartHistory.forEach((pt, idx) => {
        const x = idx * stepX;
        const normSpeed = (pt.speed || 0) / 200.0;
        const y = height - normSpeed * (height - 20) - 10;
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // 3. Draw Coolant Temp Line (Green, 0 - 150 °C)
    ctx.beginPath();
    ctx.strokeStyle = '#00ff88';
    ctx.lineWidth = 1.8;
    chartHistory.forEach((pt, idx) => {
        const x = idx * stepX;
        const normTemp = (pt.water_temp || 0) / 150.0;
        const y = height - normTemp * (height - 20) - 10;
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // 4. Draw Throttle Position Line (Amber, 0 - 100 %)
    ctx.beginPath();
    ctx.strokeStyle = '#ffaa00';
    ctx.lineWidth = 1.5;
    chartHistory.forEach((pt, idx) => {
        const x = idx * stepX;
        const normThr = (pt.throttle || 0) / 100.0;
        const y = height - normThr * (height - 20) - 10;
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });
    ctx.stroke();
}

// =========================================================================
// RAW CAN SNIFFER TABLE UPDATER
// =========================================================================
function parseRawCanFrame(line) {
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
    if (snifferContainer.style.display === 'none') return;
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

    recordedPackets = [];
    console.log(`[Recording] Exported ${filename} successfully.`);
}
