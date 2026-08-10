/**
 * espDash UI Builder Main Application Engine
 * 1-to-1 Pixel-Accurate Screen Simulator & Interactive Layout Editor
 */

document.addEventListener('DOMContentLoaded', () => {
    // DEVICE PRESETS CONFIGURATION
    const DEVICE_PRESETS = {
        'xiao-round-240': { name: 'Seeed XIAO ESP32-S3 Round', width: 240, height: 240, shape: 'round', rotation: 0 },
        'esp32-s3-lcd-314': { name: 'ESP32-S3-LCD-3.14', width: 480, height: 272, shape: 'rect', rotation: 0 },
        'waveshare-316': { name: 'Waveshare 3.16" LCD', width: 320, height: 240, shape: 'rect', rotation: 0 },
        'waveshare-147': { name: 'Waveshare 1.47" LCD', width: 172, height: 320, shape: 'rect', rotation: 0 },
        'amoled-466': { name: 'Waveshare 1.32" AMOLED Round', width: 466, height: 466, shape: 'round', rotation: 0 },
        'oled-128x64': { name: 'Generic OLED Mono', width: 128, height: 64, shape: 'rect', rotation: 0 }
    };

    // APPLICATION STATE
    const state = {
        currentPresetKey: 'xiao-round-240',
        preset: DEVICE_PRESETS['xiao-round-240'],
        zoom: 1.0,
        gridSnap: true,
        gridSize: 8,
        showBezel: true,
        widgets: [],
        selectedWidgetId: null,
        draggingWidgetId: null,
        dragOffset: { x: 0, y: 0 },
        undoStack: [],
        redoStack: [],
        telemetry: {
            rpm: 5400,
            speed_kmh: 118,
            gear: 4,
            throttle: 82,
            brake: 42,
            fuel_pct: 75,
            water_temp: 92,
            steering_deg: -15,
            battery_v: 13.8,
            link_status: 'ESP-NOW 20Hz'
        },
        simSweepActive: false,
        simSweepTimer: null,
        sweepStep: 0
    };

    // DOM ELEMENTS
    const canvas = document.getElementById('displayCanvas');
    const ctx = canvas.getContext('2d');
    const bezelFrame = document.getElementById('bezelFrame');
    const overlay = document.getElementById('canvasOverlay');
    const presetSelect = document.getElementById('devicePresetSelect');
    const inspectorContent = document.getElementById('inspectorContent');
    const inspectorTitle = document.getElementById('inspectorTitle');
    const layersList = document.getElementById('layersList');
    const codeModal = document.getElementById('codeModal');
    const codeSnippet = document.getElementById('codeSnippet');
    const fileInput = document.getElementById('fileInput');

    window.UIBuilder = state;

    // UNDO / REDO HISTORY ENGINE
    function saveHistoryState() {
        const snapshot = JSON.stringify(state.widgets);
        if (state.undoStack.length === 0 || state.undoStack[state.undoStack.length - 1] !== snapshot) {
            state.undoStack.push(snapshot);
            if (state.undoStack.length > 30) state.undoStack.shift();
            state.redoStack = [];
        }
    }

    function performUndo() {
        if (state.undoStack.length <= 1) return;
        const current = state.undoStack.pop();
        state.redoStack.push(current);

        const previous = state.undoStack[state.undoStack.length - 1];
        state.widgets = JSON.parse(previous);
        state.selectedWidgetId = null;

        renderInspector();
        renderLayersList();
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
    }

    function performRedo() {
        if (state.redoStack.length === 0) return;
        const next = state.redoStack.pop();
        state.undoStack.push(next);

        state.widgets = JSON.parse(next);
        state.selectedWidgetId = null;

        renderInspector();
        renderLayersList();
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
    }

    // KEYBOARD SHORTCUTS FOR UNDO / REDO
    window.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;

        const isCmdOrCtrl = e.metaKey || e.ctrlKey;
        if (isCmdOrCtrl && e.key.toLowerCase() === 'z') {
            e.preventDefault();
            if (e.shiftKey) {
                performRedo();
            } else {
                performUndo();
            }
        } else if (isCmdOrCtrl && e.key.toLowerCase() === 'y') {
            e.preventDefault();
            performRedo();
        } else if (e.key === 'Delete' || e.key === 'Backspace') {
            if (state.selectedWidgetId) {
                e.preventDefault();
                saveHistoryState();
                state.widgets = state.widgets.filter(item => item.id !== state.selectedWidgetId);
                state.selectedWidgetId = null;
                saveHistoryState();
                renderInspector();
                renderLayersList();
                renderCanvas();
                if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
            }
        }
    });

    // PRODUCTION 1-TO-1 LAYOUT PRESETS
    function loadProductionXiaoRoundLayout() {
        const cx = 120;
        const cy = 120;

        state.widgets = [
            // 1. Shift Light Outer LED Arch (12 LEDs across top from 210° to 330°, radius 110)
            { id: 'w_shift_lights', type: 'shift-lights', x: cx, y: cy, radius: 110, ledRadius: 4, ledCount: 12, startAngle: 210, endAngle: 330, max: 7000, binding: 'rpm' },

            // 2. Top Status & Battery Header
            { id: 'w_link_badge', type: 'text-label', x: cx, y: 16, text: 'ESP-NOW 20Hz', fontSize: 11, color: '#00f0ff', binding: 'link_status' },
            { id: 'w_batt_voltage', type: 'text-label', x: cx, y: 30, text: '13.8V', fontSize: 11, color: '#8a99ad', binding: 'battery_v' },

            // 3. Steering Angle Dial Header (270° top center)
            { id: 'w_steering_val', type: 'text-label', x: cx, y: 70, text: '15°L', fontSize: 12, color: '#8a99ad', binding: 'steering_deg' },

            // 4. Outer RPM Smooth Arc (135° to 405° across top, radius 98, thickness 8)
            { id: 'w_rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 98, thickness: 8, startAngle: 135, endAngle: 405, color: '#00ff66', trackColor: '#1e2942', min: 0, max: 8000, binding: 'rpm' },

            // 5. Throttle Slider Arc (150° to 210° left vertical arc, bottom-to-top, radius 82, thickness 6)
            { id: 'w_throttle_arc', type: 'smooth-arc', x: cx, y: cy, radius: 82, thickness: 6, startAngle: 150, endAngle: 210, color: '#00ff66', trackColor: '#1e2942', min: 0, max: 100, binding: 'throttle' },
            { id: 'w_throttle_num', type: 'digital-value', x: cx - 70, y: cy, fontSize: 14, color: '#00ff66', binding: 'throttle', unit: '%' },

            // 6. Brake Slider Arc (30° to -30° right vertical arc, bottom-to-top, radius 82, thickness 6)
            { id: 'w_brake_arc', type: 'smooth-arc', x: cx, y: cy, radius: 82, thickness: 6, startAngle: 30, endAngle: -30, color: '#0088ff', trackColor: '#1e2942', min: 0, max: 100, binding: 'brake' },
            { id: 'w_brake_num', type: 'digital-value', x: cx + 70, y: cy, fontSize: 14, color: '#0088ff', binding: 'brake', unit: '%' },

            // 7. Central Speedometer Readout
            { id: 'w_speed_val', type: 'digital-value', x: cx, y: cy + 8, fontSize: 42, color: '#ffffff', binding: 'speed_kmh', unit: '' },
            { id: 'w_speed_unit', type: 'text-label', x: cx, y: cy + 32, text: 'KM/H', fontSize: 11, color: '#8a99ad' },

            // 8. Bottom Badges (Coolant, Fuel, Gear)
            { id: 'w_water_temp', type: 'text-label', x: cx - 48, y: cy + 62, text: '92°C', fontSize: 12, color: '#ffcc00', binding: 'water_temp' },
            { id: 'w_fuel_level', type: 'text-label', x: cx, y: cy + 62, text: 'F:75%', fontSize: 12, color: '#00ff66', binding: 'fuel_pct' },
            { id: 'w_gear_circle', type: 'status-badge', x: cx + 48, y: cy + 62, text: '4', color: '#00f0ff', binding: 'gear' }
        ];
        saveHistoryState();
    }

    function loadProduction314Layout() {
        state.widgets = [
            // Background Cards
            { id: 'card_left', type: 'card-box', x: 16, y: 16, w: 216, h: 240, borderRadius: 12, color: '#00f0ff', bgColor: '#141b2d' },
            { id: 'card_right', type: 'card-box', x: 248, y: 16, w: 216, h: 240, borderRadius: 12, color: '#00ff66', bgColor: '#141b2d' },

            // Shift Lights Top Bar
            { id: 'shift_lights_314', type: 'shift-lights', x: 124, y: 30, radius: 80, ledRadius: 4, ledCount: 10, startAngle: 200, endAngle: 340, max: 7000, binding: 'rpm' },

            // Left Section: Engine Telemetry & RPM Arc
            { id: 'lbl_rpm', type: 'text-label', x: 124, y: 52, text: 'ENGINE TACHOMETER', fontSize: 11, color: '#00f0ff' },
            { id: 'rpm_arc', type: 'smooth-arc', x: 124, y: 142, radius: 68, thickness: 12, startAngle: 135, endAngle: 405, color: '#00f0ff', trackColor: '#1e2942', min: 0, max: 9000, binding: 'rpm' },
            { id: 'rpm_val', type: 'digital-value', x: 124, y: 142, fontSize: 32, color: '#ffffff', binding: 'rpm', unit: '' },
            { id: 'rpm_unit', type: 'text-label', x: 124, y: 168, text: 'RPM', fontSize: 11, color: '#8a99ad' },

            // Right Section: Speed & Pedal Inputs
            { id: 'lbl_speed', type: 'text-label', x: 356, y: 40, text: 'VEHICLE SPEED', fontSize: 12, color: '#00ff66' },
            { id: 'spd_val', type: 'digital-value', x: 356, y: 85, fontSize: 46, color: '#00ff66', binding: 'speed_kmh', unit: ' KM/H' },

            { id: 'lbl_thr', type: 'text-label', x: 290, y: 145, text: 'THR', fontSize: 11, color: '#00ff66' },
            { id: 'bar_thr', type: 'bar-slider', x: 315, y: 138, w: 125, h: 14, color: '#00ff66', bgColor: '#1e2942', min: 0, max: 100, binding: 'throttle' },

            { id: 'lbl_brk', type: 'text-label', x: 290, y: 175, text: 'BRK', fontSize: 11, color: '#ff3366' },
            { id: 'bar_brk', type: 'bar-slider', x: 315, y: 168, w: 125, h: 14, color: '#ff3366', bgColor: '#1e2942', min: 0, max: 100, binding: 'brake' },

            // Bottom Status Badges
            { id: 'gear_badge', type: 'status-badge', x: 356, y: 216, text: 'GEAR 4', color: '#00f0ff', binding: 'gear' }
        ];
        saveHistoryState();
    }

    function applyCurrentPresetLayout() {
        if (state.currentPresetKey === 'xiao-round-240') {
            loadProductionXiaoRoundLayout();
        } else {
            loadProduction314Layout();
        }
        renderLayersList();
    }

    // CANVAS RENDER ENGINE
    function renderCanvas() {
        ctx.save();
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        // Draw Canvas Background
        ctx.fillStyle = '#050811';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Draw Grid if Enabled
        if (state.gridSnap) {
            ctx.strokeStyle = '#101726';
            ctx.lineWidth = 1;
            for (let x = 0; x < canvas.width; x += state.gridSize) {
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, canvas.height);
                ctx.stroke();
            }
            for (let y = 0; y < canvas.height; y += state.gridSize) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(canvas.width, y);
                ctx.stroke();
            }
        }

        // Draw Widgets
        state.widgets.forEach(w => {
            ctx.save();
            renderWidget(w);
            ctx.restore();
        });

        // Draw Selection Bounding Box
        if (state.selectedWidgetId) {
            const selW = state.widgets.find(w => w.id === state.selectedWidgetId);
            if (selW) {
                drawSelectionHighlight(selW);
            }
        }

        ctx.restore();
        updateOverlayNodes();
    }

    function getWidgetValue(w) {
        if (w.binding && state.telemetry[w.binding] !== undefined) {
            return state.telemetry[w.binding];
        }
        return w.value !== undefined ? w.value : 50;
    }

    function renderWidget(w) {
        const val = getWidgetValue(w);

        switch (w.type) {
            case 'shift-lights':
                const sRadius = w.radius || 110;
                const sStartDeg = w.startAngle !== undefined ? w.startAngle : 210;
                const sEndDeg = w.endAngle !== undefined ? w.endAngle : 330;
                const sStartA = sStartDeg * Math.PI / 180;
                const sEndA = sEndDeg * Math.PI / 180;
                const count = w.ledCount || 12;
                const maxRpm = w.max || 7000;
                const rpmPct = Math.min(1.0, Math.max(0, val / maxRpm));

                for (let i = 0; i < count; i++) {
                    const angle = sStartA + (i / (count - 1)) * (sEndA - sStartA);
                    const lx = w.x + Math.cos(angle) * sRadius;
                    const ly = w.y + Math.sin(angle) * sRadius;
                    const threshold = (i + 1) / count;

                    ctx.beginPath();
                    ctx.arc(lx, ly, w.ledRadius || 4, 0, Math.PI * 2);

                    if (rpmPct >= threshold) {
                        let ledColor = '#00ff66';
                        if (i >= 8) ledColor = '#ff3366';
                        else if (i >= 4) ledColor = '#ffcc00';
                        ctx.fillStyle = ledColor;
                        ctx.shadowColor = ledColor;
                        ctx.shadowBlur = 8;
                    } else {
                        ctx.fillStyle = w.trackColor || '#1e2942';
                        ctx.shadowBlur = 0;
                    }
                    ctx.fill();
                    ctx.shadowBlur = 0;
                }
                break;

            case 'card-box':
                ctx.fillStyle = w.bgColor || '#141b2d';
                ctx.strokeStyle = w.color || '#00f0ff';
                ctx.lineWidth = 2;
                const r = w.borderRadius || 8;
                ctx.beginPath();
                ctx.roundRect(w.x, w.y, w.w || 100, w.h || 60, r);
                ctx.fill();
                ctx.stroke();
                break;

            case 'smooth-arc':
                const min = w.min || 0;
                const max = w.max || 100;
                let startDeg = w.startAngle !== undefined ? w.startAngle : 135;
                let endDeg = w.endAngle !== undefined ? w.endAngle : 405;

                let startA = startDeg * Math.PI / 180;
                let endA = endDeg * Math.PI / 180;

                let normVal = 0;
                if (max > min) {
                    normVal = Math.min(Math.max((val - min) / (max - min), 0), 1);
                }

                let counterClockwise = false;
                let totalSweep = endA - startA;

                if (startDeg > endDeg && (endDeg < 0 || endDeg <= 330)) {
                    if (endA > startA) endA -= Math.PI * 2;
                    totalSweep = endA - startA;
                    counterClockwise = true;
                }

                const valA = startA + normVal * totalSweep;

                let activeColor = w.color || '#00ff66';
                if (w.binding === 'rpm') {
                    const ratio = val / (max || 8000);
                    if (ratio > 0.85) activeColor = '#ff3366';
                    else if (ratio > 0.65) activeColor = '#ffcc00';
                    else if (ratio > 0.35) activeColor = '#00ff66';
                    else activeColor = '#00f0ff';
                }

                // Track
                ctx.beginPath();
                ctx.arc(w.x, w.y, w.radius, startA, endA, counterClockwise);
                ctx.strokeStyle = w.trackColor || '#1e2942';
                ctx.lineWidth = w.thickness || 12;
                ctx.lineCap = 'round';
                ctx.stroke();

                // Active Arc
                if (normVal > 0.001) {
                    ctx.beginPath();
                    ctx.arc(w.x, w.y, w.radius, startA, valA, counterClockwise);
                    ctx.strokeStyle = activeColor;
                    ctx.lineWidth = w.thickness || 12;
                    ctx.lineCap = 'round';
                    ctx.stroke();
                }
                break;

            case 'digital-value':
                ctx.fillStyle = w.color || '#ffffff';
                ctx.font = `700 ${w.fontSize || 32}px Inter, sans-serif`;
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(`${val}${w.unit || ''}`, w.x, w.y);
                break;

            case 'text-label':
                ctx.fillStyle = w.color || '#8a99ad';
                ctx.font = `600 ${w.fontSize || 12}px Inter, sans-serif`;
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                let displayTxt = w.text || 'LABEL';
                if (w.binding === 'steering_deg') {
                    const st = state.telemetry.steering_deg || 0;
                    displayTxt = st === 0 ? "0°" : (st > 0 ? `${st}°R` : `${Math.abs(st)}°L`);
                } else if (w.binding === 'water_temp') {
                    displayTxt = `${state.telemetry.water_temp}°C`;
                } else if (w.binding === 'fuel_pct') {
                    displayTxt = `F:${state.telemetry.fuel_pct}%`;
                } else if (w.binding === 'battery_v') {
                    displayTxt = `${state.telemetry.battery_v}V`;
                }
                ctx.fillText(displayTxt, w.x, w.y);
                break;

            case 'bar-slider':
                const barW = w.w || 140;
                const barH = w.h || 16;
                const bMin = w.min || 0;
                const bMax = w.max || 100;
                const bNorm = Math.min(Math.max((val - bMin) / (bMax - bMin), 0), 1);

                ctx.fillStyle = w.bgColor || '#1e2942';
                ctx.fillRect(w.x, w.y, barW, barH);

                ctx.fillStyle = w.color || '#00f0ff';
                ctx.fillRect(w.x, w.y, barW * bNorm, barH);
                break;

            case 'dial-needle':
                const nStartA = (w.startAngle || 135) * Math.PI / 180;
                const nEndA = (w.endAngle || 405) * Math.PI / 180;
                const nNorm = Math.min(Math.max((val - (w.min || 0)) / ((w.max || 100) - (w.min || 0)), 0), 1);
                const nAngle = nStartA + nNorm * (nEndA - nStartA);

                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.rotate(nAngle);
                ctx.beginPath();
                ctx.moveTo(0, 0);
                ctx.lineTo(w.radius || 60, 0);
                ctx.strokeStyle = w.color || '#ff3366';
                ctx.lineWidth = 4;
                ctx.stroke();

                ctx.beginPath();
                ctx.arc(0, 0, 8, 0, Math.PI * 2);
                ctx.fillStyle = w.color || '#ff3366';
                ctx.fill();
                ctx.restore();
                break;

            case 'status-badge':
                ctx.fillStyle = w.color || '#0088ff';
                let txt = w.text || 'STATUS';
                if (w.binding === 'gear') {
                    const gears = ['P', 'R', 'N', 'D', 'S', '1', '2', '3', '4', '5', '6'];
                    txt = gears[state.telemetry.gear] || `${val}`;
                }

                if (w.id.includes('gear')) {
                    ctx.beginPath();
                    ctx.arc(w.x, w.y, 14, 0, Math.PI * 2);
                    ctx.strokeStyle = w.color || '#00f0ff';
                    ctx.lineWidth = 2;
                    ctx.stroke();

                    ctx.fillStyle = '#ffffff';
                    ctx.font = '700 13px Inter, sans-serif';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(txt, w.x, w.y);
                } else {
                    ctx.beginPath();
                    ctx.roundRect(w.x - 40, w.y - 14, 80, 28, 6);
                    ctx.fill();

                    ctx.fillStyle = '#000000';
                    ctx.font = '700 12px Inter, sans-serif';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(txt, w.x, w.y);
                }
                break;
        }
    }

    function drawSelectionHighlight(w) {
        ctx.strokeStyle = '#00f0ff';
        ctx.lineWidth = 2;
        ctx.setLineDash([4, 4]);

        const bounds = getWidgetBounds(w);
        ctx.strokeRect(bounds.x - 4, bounds.y - 4, bounds.w + 8, bounds.h + 8);
        ctx.setLineDash([]);
    }

    function getWidgetBounds(w) {
        switch (w.type) {
            case 'shift-lights':
            case 'card-box':
                return { x: w.x - (w.radius || 60), y: w.y - (w.radius || 60), w: (w.radius || 60) * 2, h: (w.radius || 60) * 2 };
            case 'smooth-arc':
            case 'dial-needle':
                const r = w.radius || 50;
                return { x: w.x - r, y: w.y - r, w: r * 2, h: r * 2 };
            case 'digital-value':
            case 'text-label':
                return { x: w.x - 60, y: w.y - (w.fontSize || 16) / 2, w: 120, h: w.fontSize || 20 };
            case 'bar-slider':
                return { x: w.x, y: w.y, w: w.w || 140, h: w.h || 16 };
            case 'status-badge':
                return { x: w.x - 40, y: w.y - 14, w: 80, h: 28 };
            default:
                return { x: w.x - 20, y: w.y - 20, w: 40, h: 40 };
        }
    }

    function applyZoom(newZoom) {
        state.zoom = Math.min(Math.max(newZoom, 0.5), 3.0);
        document.getElementById('zoomLabel').textContent = `${Math.round(state.zoom * 100)}%`;
        bezelFrame.style.transform = `scale(${state.zoom})`;
        bezelFrame.style.transformOrigin = 'center center';
    }

    // PRESET SWITCHING & RESIZING
    function applyPreset(presetKey) {
        state.currentPresetKey = presetKey;
        state.preset = DEVICE_PRESETS[presetKey];

        canvas.width = state.preset.width;
        canvas.height = state.preset.height;

        document.getElementById('canvasDimInfo').textContent = `Resolution: ${state.preset.width} × ${state.preset.height} (${state.preset.shape.toUpperCase()})`;

        if (state.preset.shape === 'round') {
            bezelFrame.classList.add('round-display');
        } else {
            bezelFrame.classList.remove('round-display');
        }

        applyCurrentPresetLayout();
        renderCanvas();
    }

    // INSPECTOR PANEL SETUP
    function renderInspector() {
        const w = state.widgets.find(item => item.id === state.selectedWidgetId);
        if (!w) {
            inspectorTitle.textContent = 'Widget Inspector';
            inspectorContent.innerHTML = `<p class="no-selection-msg">Select a widget on the canvas to configure properties</p>`;
            return;
        }

        inspectorTitle.textContent = `Edit ${w.type.toUpperCase()}`;

        let html = `
            <div class="prop-group">
                <label>ID / Name</label>
                <input type="text" class="styled-input" id="propId" value="${w.id}">
            </div>
            <div class="prop-group">
                <label>Position (X, Y)</label>
                <div class="prop-row">
                    <input type="number" class="styled-input" id="propX" value="${Math.round(w.x)}">
                    <input type="number" class="styled-input" id="propY" value="${Math.round(w.y)}">
                </div>
            </div>
            <div class="prop-group">
                <label>Telemetry Data Binding</label>
                <select class="styled-select" id="propBinding" style="width: 100%;">
                    <option value="" ${!w.binding ? 'selected' : ''}>-- Static Value --</option>
                    <option value="rpm" ${w.binding === 'rpm' ? 'selected' : ''}>Engine RPM</option>
                    <option value="speed_kmh" ${w.binding === 'speed_kmh' ? 'selected' : ''}>Vehicle Speed (km/h)</option>
                    <option value="gear" ${w.binding === 'gear' ? 'selected' : ''}>Active Gear</option>
                    <option value="throttle" ${w.binding === 'throttle' ? 'selected' : ''}>Throttle Position (%)</option>
                    <option value="brake" ${w.binding === 'brake' ? 'selected' : ''}>Brake Pressure (%)</option>
                    <option value="fuel_pct" ${w.binding === 'fuel_pct' ? 'selected' : ''}>Fuel Level (%)</option>
                    <option value="water_temp" ${w.binding === 'water_temp' ? 'selected' : ''}>Water Temp (°C)</option>
                    <option value="steering_deg" ${w.binding === 'steering_deg' ? 'selected' : ''}>Steering Angle (°)</option>
                    <option value="battery_v" ${w.binding === 'battery_v' ? 'selected' : ''}>Battery Voltage (V)</option>
                </select>
            </div>
            <div class="prop-group">
                <label>Primary Color</label>
                <div class="color-picker-row">
                    <input type="color" id="propColor" value="${w.color || '#00f0ff'}">
                    <input type="text" class="styled-input" id="propColorHex" value="${w.color || '#00f0ff'}">
                </div>
            </div>
        `;

        if (w.type === 'shift-lights' || w.type === 'smooth-arc' || w.type === 'dial-needle') {
            html += `
                <div class="prop-group">
                    <label>Radius & Thickness / LED Count</label>
                    <div class="prop-row">
                        <input type="number" class="styled-input" id="propRadius" placeholder="Radius" value="${w.radius || 60}">
                        <input type="number" class="styled-input" id="propThickness" placeholder="Thickness / LED Count" value="${w.thickness || w.ledCount || 12}">
                    </div>
                </div>
                <div class="prop-group">
                    <label>Start / End Angle (deg)</label>
                    <div class="prop-row">
                        <input type="number" class="styled-input" id="propStartAngle" value="${w.startAngle !== undefined ? w.startAngle : 210}">
                        <input type="number" class="styled-input" id="propEndAngle" value="${w.endAngle !== undefined ? w.endAngle : 330}">
                    </div>
                </div>
            `;
        } else if (w.type === 'digital-value' || w.type === 'text-label') {
            html += `
                <div class="prop-group">
                    <label>Font Size / Label Text</label>
                    <div class="prop-row">
                        <input type="number" class="styled-input" id="propFontSize" value="${w.fontSize || 24}">
                        <input type="text" class="styled-input" id="propText" value="${w.text || w.unit || ''}" placeholder="Suffix / Text">
                    </div>
                </div>
            `;
        } else if (w.type === 'bar-slider' || w.type === 'card-box') {
            html += `
                <div class="prop-group">
                    <label>Dimensions (Width x Height)</label>
                    <div class="prop-row">
                        <input type="number" class="styled-input" id="propW" value="${w.w || 120}">
                        <input type="number" class="styled-input" id="propH" value="${w.h || 30}">
                    </div>
                </div>
            `;
        }

        html += `<button id="deleteWidgetBtn" class="btn btn-secondary" style="margin-top: 12px; border-color: #ff3366; color: #ff3366;">🗑️ Delete Widget</button>`;

        inspectorContent.innerHTML = html;

        bindInspectorListeners(w);
    }

    function bindInspectorListeners(w) {
        const bindInput = (id, prop, isNum = false) => {
            const el = document.getElementById(id);
            if (el) {
                el.addEventListener('input', (e) => {
                    saveHistoryState();
                    w[prop] = isNum ? parseFloat(e.target.value) || 0 : e.target.value;
                    renderCanvas();
                    renderLayersList();
                    if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
                });
            }
        };

        bindInput('propId', 'id');
        bindInput('propX', 'x', true);
        bindInput('propY', 'y', true);
        bindInput('propBinding', 'binding');
        bindInput('propColor', 'color');
        bindInput('propColorHex', 'color');
        bindInput('propRadius', 'radius', true);
        bindInput('propThickness', 'thickness', true);
        bindInput('propStartAngle', 'startAngle', true);
        bindInput('propEndAngle', 'endAngle', true);
        bindInput('propFontSize', 'fontSize', true);
        bindInput('propText', 'text');
        bindInput('propW', 'w', true);
        bindInput('propH', 'h', true);

        const delBtn = document.getElementById('deleteWidgetBtn');
        if (delBtn) {
            delBtn.addEventListener('click', () => {
                saveHistoryState();
                state.widgets = state.widgets.filter(item => item.id !== w.id);
                state.selectedWidgetId = null;
                saveHistoryState();
                renderInspector();
                renderLayersList();
                renderCanvas();
                if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
            });
        }
    }

    function renderLayersList() {
        layersList.innerHTML = '';
        state.widgets.slice().reverse().forEach(w => {
            const div = document.createElement('div');
            div.className = `layer-item ${w.id === state.selectedWidgetId ? 'selected' : ''}`;
            div.innerHTML = `<span>${w.id} (${w.type})</span>`;
            div.addEventListener('click', () => {
                state.selectedWidgetId = w.id;
                renderInspector();
                renderLayersList();
                renderCanvas();
            });
            layersList.appendChild(div);
        });
    }

    function updateOverlayNodes() {
        overlay.innerHTML = '';
    }

    // MOUSE INTERACTION ON CANVAS
    canvas.addEventListener('mousedown', (e) => {
        saveHistoryState();
        const rect = canvas.getBoundingClientRect();
        const mouseX = (e.clientX - rect.left) / state.zoom;
        const mouseY = (e.clientY - rect.top) / state.zoom;

        let hit = null;
        for (let i = state.widgets.length - 1; i >= 0; i--) {
            const w = state.widgets[i];
            const b = getWidgetBounds(w);
            if (mouseX >= b.x && mouseX <= b.x + b.w && mouseY >= b.y && mouseY <= b.y + b.h) {
                hit = w;
                break;
            }
        }

        if (hit) {
            state.selectedWidgetId = hit.id;
            state.draggingWidgetId = hit.id;
            state.dragOffset = { x: mouseX - hit.x, y: mouseY - hit.y };
        } else {
            state.selectedWidgetId = null;
        }

        renderInspector();
        renderLayersList();
        renderCanvas();
    });

    window.addEventListener('mousemove', (e) => {
        const rect = canvas.getBoundingClientRect();
        const mouseX = (e.clientX - rect.left) / state.zoom;
        const mouseY = (e.clientY - rect.top) / state.zoom;

        document.getElementById('cursorPosInfo').textContent = `X: ${Math.round(mouseX)}, Y: ${Math.round(mouseY)}`;

        if (state.draggingWidgetId) {
            const w = state.widgets.find(item => item.id === state.draggingWidgetId);
            if (w) {
                let newX = mouseX - state.dragOffset.x;
                let newY = mouseY - state.dragOffset.y;

                if (state.gridSnap) {
                    newX = Math.round(newX / state.gridSize) * state.gridSize;
                    newY = Math.round(newY / state.gridSize) * state.gridSize;
                }

                w.x = newX;
                w.y = newY;
                renderInspector();
                renderCanvas();
            }
        }
    });

    window.addEventListener('mouseup', () => {
        if (state.draggingWidgetId) {
            state.draggingWidgetId = null;
            saveHistoryState();
            if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
        }
    });

    // DRAG AND DROP FROM PALETTE
    document.querySelectorAll('.widget-item').forEach(item => {
        item.addEventListener('click', () => {
            saveHistoryState();
            const type = item.getAttribute('data-type');
            const newId = `${type}_${Date.now().toString().slice(-4)}`;
            const cx = Math.floor(state.preset.width / 2);
            const cy = Math.floor(state.preset.height / 2);

            const newWidget = {
                id: newId,
                type: type,
                x: cx,
                y: cy,
                color: '#00f0ff',
                bgColor: '#141b2d',
                radius: 60,
                thickness: 12,
                w: 140,
                h: 30,
                fontSize: 24,
                binding: 'rpm'
            };

            state.widgets.push(newWidget);
            state.selectedWidgetId = newId;
            saveHistoryState();
            renderInspector();
            renderLayersList();
            renderCanvas();
            if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
        });
    });

    // ZOOM & DISPLAY CONTROLS BINDINGS
    document.getElementById('zoomInBtn').addEventListener('click', () => applyZoom(state.zoom + 0.25));
    document.getElementById('zoomOutBtn').addEventListener('click', () => applyZoom(state.zoom - 0.25));

    const gridBtn = document.getElementById('gridToggleBtn');
    gridBtn.addEventListener('click', () => {
        state.gridSnap = !state.gridSnap;
        gridBtn.classList.toggle('active', state.gridSnap);
        gridBtn.textContent = state.gridSnap ? 'Grid: ON' : 'Grid: OFF';
        renderCanvas();
    });

    const bezelBtn = document.getElementById('bezelToggleBtn');
    bezelBtn.addEventListener('click', () => {
        state.showBezel = !state.showBezel;
        bezelBtn.classList.toggle('active', state.showBezel);
        bezelBtn.textContent = state.showBezel ? 'Bezel: ON' : 'Bezel: OFF';
        bezelFrame.style.borderWidth = state.showBezel ? '' : '0px';
    });

    presetSelect.addEventListener('change', (e) => applyPreset(e.target.value));

    const resetLayoutBtn = document.getElementById('resetLayoutBtn');
    if (resetLayoutBtn) {
        resetLayoutBtn.addEventListener('click', () => {
            saveHistoryState();
            applyCurrentPresetLayout();
            saveHistoryState();
            renderCanvas();
            if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
        });
    }

    // SAVE & LOAD LAYOUT JSON BUTTONS
    document.getElementById('exportJsonBtn').addEventListener('click', () => {
        const layoutData = {
            preset: state.currentPresetKey,
            width: state.preset.width,
            height: state.preset.height,
            widgets: state.widgets
        };
        const blob = new Blob([JSON.stringify(layoutData, null, 2)], { type: 'application/json' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = `espDash_layout_${state.currentPresetKey}_${Date.now()}.json`;
        a.click();
    });

    document.getElementById('importJsonBtn').addEventListener('click', () => fileInput.click());

    fileInput.addEventListener('change', (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = (evt) => {
            try {
                const data = JSON.parse(evt.target.result);
                saveHistoryState();
                if (data.preset && DEVICE_PRESETS[data.preset]) {
                    presetSelect.value = data.preset;
                    state.currentPresetKey = data.preset;
                    state.preset = DEVICE_PRESETS[data.preset];
                }
                if (Array.isArray(data.widgets)) {
                    state.widgets = data.widgets;
                    state.selectedWidgetId = null;
                    saveHistoryState();
                    renderInspector();
                    renderLayersList();
                    renderCanvas();
                    if (window.LivePreviewBridge) window.LivePreviewBridge.sendFullLayout();
                    alert('Layout loaded successfully!');
                }
            } catch (err) {
                alert('Failed to parse layout JSON file.');
            }
        };
        reader.readAsText(file);
    });

    // TELEMETRY SIMULATOR SWEEP TEST
    const simPlayPauseBtn = document.getElementById('simPlayPauseBtn');

    function updateTelemetryUI() {
        document.getElementById('simRpm').value = state.telemetry.rpm;
        document.getElementById('valRpm').textContent = Math.round(state.telemetry.rpm);

        document.getElementById('simSpeed').value = state.telemetry.speed_kmh;
        document.getElementById('valSpeed').textContent = Math.round(state.telemetry.speed_kmh);

        document.getElementById('simGear').value = state.telemetry.gear;
        document.getElementById('valGear').textContent = state.telemetry.gear;

        document.getElementById('simThrottle').value = state.telemetry.throttle;
        document.getElementById('valThrottle').textContent = Math.round(state.telemetry.throttle);

        document.getElementById('simBrake').value = state.telemetry.brake;
        document.getElementById('valBrake').textContent = Math.round(state.telemetry.brake);

        document.getElementById('simFuel').value = state.telemetry.fuel_pct;
        document.getElementById('valFuel').textContent = Math.round(state.telemetry.fuel_pct);

        document.getElementById('simWater').value = state.telemetry.water_temp;
        document.getElementById('valWater').textContent = Math.round(state.telemetry.water_temp);
    }

    simPlayPauseBtn.addEventListener('click', () => {
        state.simSweepActive = !state.simSweepActive;
        simPlayPauseBtn.textContent = state.simSweepActive ? '⏸ Pause Sweep' : '▶ Sweep Test';

        if (state.simSweepActive) {
            state.simSweepTimer = setInterval(() => {
                state.sweepStep += 0.05;
                const sinVal = (Math.sin(state.sweepStep) + 1) / 2; // 0 to 1

                state.telemetry.rpm = Math.round(1000 + sinVal * 6800);
                state.telemetry.speed_kmh = Math.round(sinVal * 210);
                state.telemetry.gear = Math.min(Math.floor(1 + sinVal * 5.8), 6);
                state.telemetry.throttle = Math.round(sinVal * 100);
                state.telemetry.brake = Math.round((1 - sinVal) * 100);
                state.telemetry.steering_deg = Math.round((Math.sin(state.sweepStep * 0.7)) * 180);

                updateTelemetryUI();
                renderCanvas();
                if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
            }, 50);
        } else {
            if (state.simSweepTimer) clearInterval(state.simSweepTimer);
        }
    });

    document.getElementById('simRpm').addEventListener('input', (e) => {
        state.telemetry.rpm = parseInt(e.target.value);
        document.getElementById('valRpm').textContent = state.telemetry.rpm;
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
    });

    document.getElementById('simSpeed').addEventListener('input', (e) => {
        state.telemetry.speed_kmh = parseInt(e.target.value);
        document.getElementById('valSpeed').textContent = state.telemetry.speed_kmh;
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
    });

    document.getElementById('simGear').addEventListener('input', (e) => {
        state.telemetry.gear = parseInt(e.target.value);
        document.getElementById('valGear').textContent = state.telemetry.gear;
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
    });

    document.getElementById('simThrottle').addEventListener('input', (e) => {
        state.telemetry.throttle = parseInt(e.target.value);
        document.getElementById('valThrottle').textContent = state.telemetry.throttle;
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
    });

    document.getElementById('simBrake').addEventListener('input', (e) => {
        state.telemetry.brake = parseInt(e.target.value);
        document.getElementById('valBrake').textContent = state.telemetry.brake;
        renderCanvas();
        if (window.LivePreviewBridge) window.LivePreviewBridge.sendTelemetryTick(state.telemetry);
    });

    // HARDWARE PREVIEW CONNECT BUTTONS
    document.getElementById('connectWsBtn').addEventListener('click', () => {
        const ip = document.getElementById('bridgeTargetIp').value.trim();
        if (ip && window.LivePreviewBridge) {
            window.LivePreviewBridge.connectWebSocket(ip);
        }
    });

    document.getElementById('connectSerialBtn').addEventListener('click', () => {
        if (window.LivePreviewBridge) {
            window.LivePreviewBridge.connectWebSerial();
        }
    });

    if (window.LivePreviewBridge) {
        window.LivePreviewBridge.init({
            onStatusChange: (connected, msg) => {
                const badge = document.getElementById('bridgeStatusBadge');
                if (connected) {
                    badge.className = 'status-badge connected';
                    badge.textContent = msg;
                } else {
                    badge.className = 'status-badge disconnected';
                    badge.textContent = msg;
                }
            }
        });
    }

    // EXPORT CODE MODAL
    document.getElementById('exportCodeBtn').addEventListener('click', () => {
        if (window.CodeGenerator) {
            const cpp = window.CodeGenerator.generateCpp(state.preset, state.widgets);
            codeSnippet.textContent = cpp;
            codeModal.classList.add('active');
        }
    });

    const closeModal = () => codeModal.classList.remove('active');
    document.getElementById('closeModalBtn').addEventListener('click', closeModal);
    document.getElementById('closeModalFooterBtn').addEventListener('click', closeModal);

    document.getElementById('copyCodeBtn').addEventListener('click', () => {
        navigator.clipboard.writeText(codeSnippet.textContent).then(() => {
            alert('C++ Code copied to clipboard!');
        });
    });

    // INITIALIZATION
    applyPreset('xiao-round-240');
});
