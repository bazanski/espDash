/**
 * espDash UI Studio & Telemetry Builder Engine
 * 1-to-1 Pixel-Accurate ESP32 Display Simulator, Typography Studio & Visual Designer
 * Navigation & Waze/GMaps Integration Pack + Full 50Hz LVGL 8.4 Export Ready
 */

document.addEventListener('DOMContentLoaded', () => {
    // =========================================================================
    // 1. THEMES & COLOR PALETTES (ONE-CLICK THEME SWITCHER)
    // =========================================================================
    const THEMES = {
        'civic-eco': {
            name: 'Civic Eco (Green & Cyan)',
            primary: '#00ff66',
            secondary: '#00f0ff',
            accent: '#ffcc00',
            track: '#0c1e18',
            bgCard: '#091618',
            text: '#ffffff',
            textMuted: '#8a99ad',
            navArrow: '#00ff66',
            alert: '#ffcc00'
        },
        'type-r': {
            name: 'Type-R (Championship Red & Carbon)',
            primary: '#ff0033',
            secondary: '#ffffff',
            accent: '#ffcc00',
            track: '#220008',
            bgCard: '#140508',
            text: '#ffffff',
            textMuted: '#94a3b8',
            navArrow: '#ff0033',
            alert: '#ff3366'
        },
        'cyberpunk': {
            name: 'Cyberpunk 2077 (Neon Pink & Cyan)',
            primary: '#00f0ff',
            secondary: '#ff007f',
            accent: '#a855f7',
            track: '#180b28',
            bgCard: '#0e071c',
            text: '#ffffff',
            textMuted: '#c084fc',
            navArrow: '#00f0ff',
            alert: '#ff007f'
        },
        'motorsport-gt3': {
            name: 'Motorsport GT3 (Racing Yellow & Amber)',
            primary: '#ffcc00',
            secondary: '#ff6600',
            accent: '#00f0ff',
            track: '#261c08',
            bgCard: '#161208',
            text: '#ffffff',
            textMuted: '#a3a3a3',
            navArrow: '#ffcc00',
            alert: '#ff6600'
        },
        'stealth-oled': {
            name: 'Stealth OLED (Pure Black & White)',
            primary: '#ffffff',
            secondary: '#8a99ad',
            accent: '#00f0ff',
            track: '#141414',
            bgCard: '#0a0a0a',
            text: '#ffffff',
            textMuted: '#71717a',
            navArrow: '#ffffff',
            alert: '#ffffff'
        },
        'ice-titanium': {
            name: 'Frozen Ice (Glacier Blue & Cold White)',
            primary: '#38bdf8',
            secondary: '#06b6d4',
            accent: '#e0f2fe',
            track: '#0c1e33',
            bgCard: '#091626',
            text: '#f8fafc',
            textMuted: '#94a3b8',
            navArrow: '#38bdf8',
            alert: '#f59e0b'
        },
        'synthwave': {
            name: 'Synthwave (Violet & Sunset Orange)',
            primary: '#ff7700',
            secondary: '#8b5cf6',
            accent: '#f43f5e',
            track: '#230c33',
            bgCard: '#140820',
            text: '#fdf4ff',
            textMuted: '#d8b4fe',
            navArrow: '#ff7700',
            alert: '#f43f5e'
        }
    };

    // =========================================================================
    // 2. DEVICE PRESETS CONFIGURATION
    // =========================================================================
    const DEVICE_PRESETS = {
        'amoled-454': {
            name: 'Waveshare 1.43" AMOLED (454x454 Circle)',
            width: 454,
            height: 454,
            shape: 'round',
            rotation: 0,
            hardware: 'ESP32-S3 QSPI AMOLED',
            driver: 'SH8601 / RM69090'
        },
        'xiao-round-240': {
            name: 'Seeed XIAO ESP32-S3 Round (240x240 Circle)',
            width: 240,
            height: 240,
            shape: 'round',
            rotation: 0,
            hardware: 'Seeed Studio XIAO ESP32-S3',
            driver: 'GC9A01 SPI'
        },
        'xiao-dual-round': {
            name: 'Seeed XIAO Dual Round (480x240 Twin Pod)',
            width: 480,
            height: 240,
            shape: 'dual-round',
            rotation: 0,
            hardware: 'Dual GC9A01 240x240 Virtual Split',
            driver: 'GC9A01 Dual SPI'
        },
        'esp32-s3-lcd-314': {
            name: 'ESP32-S3-LCD-3.14 (800x240 Rect Bar)',
            width: 800,
            height: 240,
            shape: 'rect',
            rotation: 0,
            hardware: 'Waveshare ESP32-S3-Touch-LCD-3.14',
            driver: 'ST7701 RGB'
        },
        'waveshare-316': {
            name: 'Waveshare 3.16" LCD (320x240 Rect)',
            width: 320,
            height: 240,
            shape: 'rect',
            rotation: 0,
            hardware: 'Waveshare 3.16" LCD',
            driver: 'ST7789 SPI'
        },
        'waveshare-147': {
            name: 'Waveshare 1.47" LCD (172x320 Portrait)',
            width: 172,
            height: 320,
            shape: 'rect',
            rotation: 0,
            hardware: 'Waveshare 1.47" LCD',
            driver: 'ST7789 SPI'
        },
        'oled-128x64': {
            name: 'Generic OLED Mono (128x64)',
            width: 128,
            height: 64,
            shape: 'rect',
            rotation: 0,
            hardware: 'Generic I2C/SPI OLED',
            driver: 'SSD1306'
        }
    };

    // =========================================================================
    // 3. APPLICATION STATE
    // =========================================================================
    const state = {
        currentPresetKey: 'amoled-454',
        preset: DEVICE_PRESETS['amoled-454'],
        currentThemeKey: 'civic-eco',
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
        loadedCustomFonts: [],
        activeExportTab: 'lvgl',
        telemetry: {
            rpm: 5400,
            speed_kmh: 118,
            gear: 4,
            throttle: 82,
            brake: 42,
            boost_bar: 1.2,
            fuel_pct: 75,
            water_temp: 92,
            oil_temp: 98,
            battery_v: 13.8,
            steering_deg: -15,
            lat_g: 0.45,
            long_g: 0.20,
            heading_deg: 240,
            lap_current: '1:34.82',
            lap_best: '1:34.68',
            lap_delta: '+0.14',
            tpms_fl: 2.3,
            tpms_fr: 2.3,
            tpms_rl: 2.2,
            tpms_rr: 2.2,
            nav_dist_m: 350,
            nav_maneuver: 'turn-right', // 'turn-left', 'turn-right', 'slight-left', 'slight-right', 'sharp-left', 'sharp-right', 'roundabout', 'u-turn', 'straight'
            nav_road: 'B700 Main Highway',
            nav_eta: '18:42',
            nav_rem_km: '14.2 km',
            nav_rem_time: '18 min',
            nav_speed_limit: 80,
            nav_hazard_msg: '📸 Speed Camera 400m',
            link_status: 'ESP-NOW 50Hz'
        },
        chartHistory: {
            rpm: [],
            speed: [],
            throttle: []
        },
        simSweepActive: false,
        simSweepTimer: null,
        sweepStep: 0
    };

    for (let i = 0; i < 40; i++) {
        state.chartHistory.rpm.push(4000 + Math.sin(i * 0.2) * 1500);
        state.chartHistory.speed.push(80 + Math.sin(i * 0.15) * 40);
        state.chartHistory.throttle.push(50 + Math.sin(i * 0.3) * 45);
    }

    // =========================================================================
    // 4. DOM ELEMENTS
    // =========================================================================
    const canvas = document.getElementById('displayCanvas');
    const ctx = canvas.getContext('2d');
    const bezelFrame = document.getElementById('bezelFrame');
    const presetSelect = document.getElementById('devicePresetSelect');
    const themeSelect = document.getElementById('themeSelect');
    const inspectorContent = document.getElementById('inspectorContent');
    const inspectorTitle = document.getElementById('inspectorTitle');
    const layersList = document.getElementById('layersList');
    const codeModal = document.getElementById('codeModal');
    const codeSnippet = document.getElementById('codeSnippet');
    const fileInput = document.getElementById('fileInput');
    const fontFileInput = document.getElementById('fontFileInput');
    const presetsMenuBtn = document.getElementById('presetsMenuBtn');
    const presetsDropdown = document.getElementById('presetsDropdown');

    window.UIBuilder = state;

    // =========================================================================
    // 5. CUSTOM FONT MANAGER
    // =========================================================================
    const FONT_FAMILIES = [
        { label: 'Segment7 (7-Segment Digital)', value: 'Segment7, "DSEG7-Classic", monospace' },
        { label: 'DSEG7-Classic (Honda / Civic LCD)', value: '"DSEG7-Classic", monospace' },
        { label: 'Orbitron (Cyber / Motorsport)', value: 'Orbitron, monospace' },
        { label: 'Rajdhani (Industrial HUD)', value: 'Rajdhani, sans-serif' },
        { label: 'Share Tech Mono (Technical Monospace)', value: '"Share Tech Mono", monospace' },
        { label: 'Montserrat (LVGL Standard)', value: 'Montserrat, sans-serif' },
        { label: 'JetBrains Mono (Telemetry Monospace)', value: '"JetBrains Mono", monospace' },
        { label: 'Bebas Neue (Racing Condensed)', value: '"Bebas Neue", cursive' },
        { label: 'Chakra Petch (Futuristic Angle)', value: '"Chakra Petch", sans-serif' },
        { label: 'Inter (Clean Dashboard)', value: 'Inter, sans-serif' }
    ];

    async function loadCustomFontFile(file) {
        if (!file) return;
        const fontName = file.name.replace(/\.[^/.]+$/, "").replace(/[^a-zA-Z0-9_-]/g, "_");
        try {
            const arrayBuffer = await file.arrayBuffer();
            const fontFace = new FontFace(fontName, arrayBuffer);
            await fontFace.load();
            document.fonts.add(fontFace);
            
            state.loadedCustomFonts.push({
                label: `Custom: ${fontName}`,
                value: `"${fontName}", monospace`
            });

            document.getElementById('activeFontInfo').textContent = `Loaded Fonts: ${FONT_FAMILIES.length + state.loadedCustomFonts.length} families`;
            renderInspector();
            renderCanvas();
            alert(`Font "${fontName}" loaded successfully and ready to use!`);
        } catch (err) {
            console.error("Font error:", err);
            alert(`Failed to load font: ${err.message}`);
        }
    }

    fontFileInput?.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            loadCustomFontFile(e.target.files[0]);
        }
    });

    document.getElementById('uploadFontBtn')?.addEventListener('click', () => {
        fontFileInput.click();
    });

    // =========================================================================
    // 6. ONE-CLICK THEME APPLICATION
    // =========================================================================
    function applyThemeToWidgets(themeKey) {
        const t = THEMES[themeKey];
        if (!t) return;
        saveHistoryState();

        state.currentThemeKey = themeKey;

        state.widgets.forEach(w => {
            if (w.type === 'smooth-arc' || w.type === 'shift-lights' || w.type === 'rev-strip' || w.type === 'boost-gauge') {
                w.color = t.primary;
                w.trackColor = t.track;
            } else if (w.type === 'digital-value') {
                w.color = t.text;
            } else if (w.type === 'card-box') {
                w.bgColor = t.bgCard;
                w.color = t.primary;
            } else if (w.type === 'bar-slider' || w.type === 'temp-stack' || w.type === 'battery-meter') {
                w.color = t.primary;
                w.bgColor = t.track;
            } else if (w.type === 'dial-needle') {
                w.color = t.primary;
            } else if (w.type === 'text-label') {
                w.color = t.textMuted;
            } else if (w.type === 'status-badge') {
                w.color = t.primary;
            } else if (w.type === 'nav-turn-arrow' || w.type === 'nav-maneuver-banner') {
                w.color = t.navArrow || t.primary;
                w.bgColor = t.bgCard;
            } else if (w.type === 'nav-hazard-alert' || w.type === 'nav-camera-alert') {
                w.color = t.alert || '#ff3366';
                w.bgColor = t.bgCard;
            }
        });

        renderInspector();
        renderLayersList();
        renderCanvas();
    }

    themeSelect?.addEventListener('change', (e) => {
        applyThemeToWidgets(e.target.value);
    });

    // =========================================================================
    // 7. UNDO / REDO HISTORY ENGINE
    // =========================================================================
    function saveHistoryState() {
        const snapshot = JSON.stringify(state.widgets);
        if (state.undoStack.length === 0 || state.undoStack[state.undoStack.length - 1] !== snapshot) {
            state.undoStack.push(snapshot);
            if (state.undoStack.length > 40) state.undoStack.shift();
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
    }

    window.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;

        const isCmdOrCtrl = e.metaKey || e.ctrlKey;
        if (isCmdOrCtrl && e.key.toLowerCase() === 'z') {
            e.preventDefault();
            if (e.shiftKey) performRedo();
            else performUndo();
        } else if (isCmdOrCtrl && e.key.toLowerCase() === 'y') {
            e.preventDefault();
            performRedo();
        } else if (e.key === 'Delete' || e.key === 'Backspace') {
            if (state.selectedWidgetId) {
                e.preventDefault();
                deleteSelectedWidget();
            }
        } else if (isCmdOrCtrl && e.key.toLowerCase() === 'd') {
            e.preventDefault();
            duplicateSelectedWidget();
        }
    });

    function deleteSelectedWidget() {
        if (!state.selectedWidgetId) return;
        saveHistoryState();
        state.widgets = state.widgets.filter(w => w.id !== state.selectedWidgetId);
        state.selectedWidgetId = null;

        renderLayersList();
        renderInspector();
        renderCanvas();
    }

    function duplicateSelectedWidget() {
        if (!state.selectedWidgetId) return;
        const orig = state.widgets.find(w => w.id === state.selectedWidgetId);
        if (!orig) return;

        saveHistoryState();
        const clone = JSON.parse(JSON.stringify(orig));
        clone.id = `${orig.type}_${Date.now().toString().slice(-4)}`;
        clone.name = `${orig.name || orig.type} (Copy)`;
        clone.x += 16;
        clone.y += 16;
        state.widgets.push(clone);
        state.selectedWidgetId = clone.id;

        renderLayersList();
        renderInspector();
        renderCanvas();
    }

    // =========================================================================
    // 8. EXACT 1-TO-1 REAL ESP SCREEN PRESETS & NAVIGATION PRESETS
    // =========================================================================
    function loadPresetCivicAmoled() {
        const cx = 227;
        const cy = 227;

        state.widgets = [
            { id: 'rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 200, thickness: 14, startAngle: 140, endAngle: 400, color: '#00f0ff', trackColor: '#101726', min: 0, max: 8000, binding: 'rpm', name: 'RPM Arc (0-8k)' },
            { id: 'throttle_arc', type: 'smooth-arc', x: cx, y: cy, radius: 170, thickness: 10, startAngle: 140, endAngle: 400, color: '#00ff66', trackColor: '#101726', min: 0, max: 100, binding: 'throttle', name: 'Throttle Arc (%)' },
            { id: 'eff_arc', type: 'smooth-arc', x: cx, y: cy, radius: 140, thickness: 8, startAngle: 140, endAngle: 400, color: '#ffcc00', trackColor: '#101726', min: 0, max: 20, value: 7.2, name: 'Eco Coach Arc' },
            { id: 'speed_value', type: 'digital-value', x: cx, y: 184, fontSize: 96, fontFamily: 'Segment7, "DSEG7-Classic", monospace', color: '#ffffff', binding: 'speed_kmh', showGhost: true, ghostOpacity: 0.08, ghostDigits: '888', name: 'Speed Readout' },
            { id: 'speed_label', type: 'text-label', x: cx, y: 248, text: 'km/h', fontSize: 18, fontFamily: 'Montserrat, sans-serif', color: '#00f0ff', name: 'Speed Unit' },
            { id: 'eff_val', type: 'digital-value', x: cx - 20, y: 290, fontSize: 20, fontFamily: '"DSEG7-Classic", monospace', color: '#ffcc00', value: '7.2', name: 'Eco Value' },
            { id: 'eff_label', type: 'text-label', x: cx + 24, y: 290, text: 'L/100', fontSize: 14, fontFamily: 'Montserrat, sans-serif', color: '#8a99ad', name: 'Eco Unit' },
            { id: 'throttle_val', type: 'digital-value', x: cx - 20, y: 320, fontSize: 20, fontFamily: '"DSEG7-Classic", monospace', color: '#00ff66', binding: 'throttle', name: 'Throttle Value' },
            { id: 'throttle_label', type: 'text-label', x: cx + 20, y: 320, text: '%', fontSize: 14, fontFamily: 'Montserrat, sans-serif', color: '#8a99ad', name: 'Throttle Unit' },
            { id: 'rpm_num_val', type: 'digital-value', x: cx - 20, y: 350, fontSize: 20, fontFamily: '"DSEG7-Classic", monospace', color: '#00f0ff', binding: 'rpm', name: 'RPM Value' },
            { id: 'rpm_num_label', type: 'text-label', x: cx + 26, y: 350, text: 'rpm', fontSize: 14, fontFamily: 'Montserrat, sans-serif', color: '#8a99ad', name: 'RPM Unit' },
            { id: 'status_label', type: 'text-label', x: cx, y: 390, text: 'Efficiency Monitor', fontSize: 16, fontFamily: 'Montserrat, sans-serif', color: '#00ff66', name: 'Status Footer' },
            { id: 'gear_badge', type: 'status-badge', x: cx, y: 80, text: 'D4', fontSize: 14, color: '#00f0ff', binding: 'gear', name: 'Gear Pill' }
        ];
        saveHistoryState();
    }

    // PRESET: Waze / GMaps Turn-by-Turn Navigation Cockpit (AMOLED 454x454)
    function loadPresetNavAmoled() {
        const cx = 227;
        const cy = 227;

        state.widgets = [
            // Outer Speedometer & Tachometer Arcs
            { id: 'nav_rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 205, thickness: 8, startAngle: 140, endAngle: 400, color: '#00f0ff', trackColor: '#101726', min: 0, max: 8000, binding: 'rpm', name: 'Outer RPM Arc' },

            // Big Navigation Turn Arrow (Center Top)
            { id: 'nav_turn', type: 'nav-turn-arrow', x: cx, y: 130, w: 90, h: 90, color: '#00ff66', bgColor: '#0e172a', name: 'Turn Arrow' },

            // Next Maneuver Road Banner
            { id: 'nav_road_banner', type: 'nav-maneuver-banner', x: cx, y: 215, w: 320, h: 42, color: '#00f0ff', bgColor: '#0f172a', name: 'Next Road Banner' },

            // Digital Speedometer (Center)
            { id: 'nav_speed', type: 'digital-value', x: cx - 60, y: 300, fontSize: 56, fontFamily: 'Orbitron, monospace', color: '#ffffff', binding: 'speed_kmh', name: 'Speed Readout' },
            { id: 'nav_speed_unit', type: 'text-label', x: cx - 60, y: 334, text: 'KM/H', fontSize: 11, fontFamily: 'Rajdhani, sans-serif', color: '#8a99ad', name: 'Speed Unit' },

            // Speed Limit Sign
            { id: 'nav_speed_limit', type: 'speed-sign', x: cx + 65, y: 300, text: '80', name: 'Speed Limit Sign' },

            // Trip ETA & Remaining Distance Footer Badge
            { id: 'nav_trip_eta', type: 'nav-eta-badge', x: cx, y: 380, w: 300, h: 40, color: '#00ff66', name: 'ETA & Trip Badge' },

            // Top Camera / Hazard Alert Banner
            { id: 'nav_camera_top', type: 'nav-camera-alert', x: cx, y: 55, w: 220, h: 32, color: '#ffcc00', name: 'Speed Trap Alert' }
        ];
        saveHistoryState();
    }

    // PRESET: Highway Navigation & Radar Bar (3.14" 800x240)
    function loadPresetNavHighway314() {
        state.widgets = [
            // Left Container: Turn-by-turn Maneuver & Next Street
            { id: 'nav_left_card', type: 'card-box', x: 12, y: 12, w: 280, h: 216, borderRadius: 12, color: '#00ff66', bgColor: '#0b1322', name: 'Maneuver Card' },
            { id: 'nav_big_arrow', type: 'nav-turn-arrow', x: 80, y: 85, w: 90, h: 90, color: '#00ff66', name: 'Large Turn Arrow' },
            { id: 'nav_dist_text', type: 'text-label', x: 200, y: 70, text: '350 m', fontSize: 28, fontFamily: 'Orbitron, monospace', color: '#ffffff', name: 'Turn Distance' },
            { id: 'nav_turn_sub', type: 'text-label', x: 200, y: 102, text: 'Turn Right onto', fontSize: 11, fontFamily: 'Rajdhani, sans-serif', color: '#00ff66', name: 'Turn Instruction' },
            { id: 'nav_street_name', type: 'text-label', x: 140, y: 160, text: 'B700 HIGHWAY EXIT 4B', fontSize: 13, fontFamily: 'Orbitron, monospace', color: '#00f0ff', name: 'Highway Exit' },
            { id: 'nav_lane_guide', type: 'nav-lane-assist', x: 140, y: 195, w: 180, h: 20, name: 'Highway Lane Assist' },

            // Center Container: Speed, Limit & Rev Strip
            { id: 'nav_center_card', type: 'card-box', x: 304, y: 12, w: 250, h: 216, borderRadius: 12, color: '#00f0ff', bgColor: '#0b1322', name: 'Speed Cluster Card' },
            { id: 'nav_rev_bar', type: 'rev-strip', x: 320, y: 30, w: 218, h: 12, ledCount: 16, max: 8000, color: '#00f0ff', trackColor: '#162238', binding: 'rpm', name: 'Tach Bar' },
            { id: 'nav_center_spd', type: 'digital-value', x: 395, y: 110, fontSize: 64, fontFamily: 'Segment7, "DSEG7-Classic", monospace', color: '#ffffff', binding: 'speed_kmh', showGhost: true, ghostOpacity: 0.08, ghostDigits: '888', name: 'Center Speed' },
            { id: 'nav_speed_sign_314', type: 'speed-sign', x: 495, y: 110, text: '90', name: 'Speed Limit 90' },
            { id: 'nav_gear_pill', type: 'status-badge', x: 429, y: 185, text: 'D5', color: '#00f0ff', binding: 'gear', name: 'Gear Pill' },

            // Right Container: Radar Hazard & Trip ETA
            { id: 'nav_right_card', type: 'card-box', x: 566, y: 12, w: 222, h: 216, borderRadius: 12, color: '#ffcc00', bgColor: '#0b1322', name: 'Waze Radar Card' },
            { id: 'nav_hazard_box', type: 'nav-hazard-alert', x: 677, y: 70, w: 198, h: 64, color: '#ffcc00', name: 'Waze Police Hazard' },
            { id: 'nav_eta_box', type: 'nav-eta-badge', x: 677, y: 165, w: 198, h: 60, color: '#00ff66', name: 'Trip ETA Box' }
        ];
        saveHistoryState();
    }

    function loadPresetXiaoProduction() {
        const cx = 120;
        const cy = 120;

        state.widgets = [
            { id: 'w_shift_lights', type: 'shift-lights', x: cx, y: cy, radius: 110, ledRadius: 4, ledCount: 12, startAngle: 210, endAngle: 330, max: 7000, binding: 'rpm', name: 'Tach Shift Arch' },
            { id: 'w_link_badge', type: 'text-label', x: cx, y: 16, text: 'ESP-NOW 50Hz', fontSize: 10, fontFamily: 'Inter, sans-serif', color: '#00f0ff', binding: 'link_status', name: 'Link Badge' },
            { id: 'w_batt_voltage', type: 'text-label', x: cx, y: 28, text: '13.8V', fontSize: 10, fontFamily: '"JetBrains Mono", monospace', color: '#8a99ad', binding: 'battery_v', name: 'Batt Readout' },
            { id: 'w_steering_val', type: 'text-label', x: cx, y: 68, text: '15°L', fontSize: 12, fontFamily: 'Orbitron, monospace', color: '#8a99ad', binding: 'steering_deg', name: 'Steering Angle' },
            { id: 'w_rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 98, thickness: 8, startAngle: 135, endAngle: 405, color: '#00ff66', trackColor: '#1e2942', min: 0, max: 8000, binding: 'rpm', name: 'RPM Arc Sweep' },
            { id: 'w_throttle_arc', type: 'smooth-arc', x: cx, y: cy, radius: 82, thickness: 6, startAngle: 150, endAngle: 210, color: '#00ff66', trackColor: '#1e2942', min: 0, max: 100, binding: 'throttle', name: 'Throttle Arc' },
            { id: 'w_throttle_num', type: 'digital-value', x: cx - 68, y: cy, fontSize: 13, fontFamily: 'Orbitron, monospace', color: '#00ff66', binding: 'throttle', unit: '%', name: 'Throttle Value' },
            { id: 'w_brake_arc', type: 'smooth-arc', x: cx, y: cy, radius: 82, thickness: 6, startAngle: 30, endAngle: -30, color: '#0088ff', trackColor: '#1e2942', min: 0, max: 100, binding: 'brake', name: 'Brake Arc' },
            { id: 'w_brake_num', type: 'digital-value', x: cx + 68, y: cy, fontSize: 13, fontFamily: 'Orbitron, monospace', color: '#0088ff', binding: 'brake', unit: '%', name: 'Brake Value' },
            { id: 'w_speed_val', type: 'digital-value', x: cx, y: cy + 6, fontSize: 44, fontFamily: 'Orbitron, monospace', color: '#ffffff', binding: 'speed_kmh', unit: '', name: 'Central Speed' },
            { id: 'w_speed_unit', type: 'text-label', x: cx, y: cy + 30, text: 'KM/H', fontSize: 10, fontFamily: 'Rajdhani, sans-serif', color: '#8a99ad', name: 'Speed Unit' },
            { id: 'w_water_temp', type: 'text-label', x: cx - 48, y: cy + 62, text: '92°C', fontSize: 11, fontFamily: 'Inter, sans-serif', color: '#ffcc00', binding: 'water_temp', name: 'Water Temp' },
            { id: 'w_fuel_level', type: 'text-label', x: cx, y: cy + 62, text: 'F:75%', fontSize: 11, fontFamily: 'Inter, sans-serif', color: '#00ff66', binding: 'fuel_pct', name: 'Fuel Level' },
            { id: 'w_gear_circle', type: 'status-badge', x: cx + 48, y: cy + 62, text: '4', color: '#00f0ff', binding: 'gear', name: 'Gear Indicator' }
        ];
        saveHistoryState();
    }

    function loadPresetXiaoEez() {
        const cx = 120;
        const cy = 120;

        state.widgets = [
            { id: 'eez_rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 106, thickness: 10, startAngle: 135, endAngle: 405, color: '#00f0ff', trackColor: '#141c30', min: 0, max: 8000, binding: 'rpm', name: 'EEZ RPM Arc' },
            { id: 'eez_thr_arc', type: 'smooth-arc', x: cx, y: cy, radius: 90, thickness: 7, startAngle: 135, endAngle: 405, color: '#00ff66', trackColor: '#141c30', min: 0, max: 100, binding: 'throttle', name: 'EEZ Throttle Arc' },
            { id: 'eez_speed_val', type: 'digital-value', x: cx, y: cy - 10, fontSize: 52, fontFamily: 'Segment7, "DSEG7-Classic", monospace', color: '#ffffff', binding: 'speed_kmh', showGhost: true, ghostOpacity: 0.08, ghostDigits: '888', name: 'EEZ Speed' },
            { id: 'eez_speed_label', type: 'text-label', x: cx, y: cy + 24, text: 'km/h', fontSize: 12, fontFamily: 'Montserrat, sans-serif', color: '#8a99ad', name: 'EEZ Unit' },
            { id: 'eez_gear', type: 'status-badge', x: cx, y: 40, text: '4', color: '#00f0ff', binding: 'gear', name: 'EEZ Gear' },
            { id: 'eez_batt', type: 'text-label', x: cx - 44, y: cy + 54, text: '13.8V', fontSize: 11, fontFamily: 'Montserrat, sans-serif', color: '#ffcc00', binding: 'battery_v', name: 'EEZ Batt' },
            { id: 'eez_temp', type: 'text-label', x: cx + 44, y: cy + 54, text: '92°C', fontSize: 11, fontFamily: 'Montserrat, sans-serif', color: '#00ff66', binding: 'water_temp', name: 'EEZ Temp' }
        ];
        saveHistoryState();
    }

    function loadPresetRaceLab314() {
        state.widgets = [
            { id: 'card_bars', type: 'card-box', x: 12, y: 12, w: 140, h: 216, borderRadius: 10, color: '#00f0ff', bgColor: '#0e1526', name: 'Telemetry Bar Panel' },
            { id: 'card_chart', type: 'card-box', x: 164, y: 12, w: 410, h: 216, borderRadius: 10, color: '#00ff66', bgColor: '#0e1526', name: 'Live Waveform Panel' },
            { id: 'card_tpms', type: 'card-box', x: 586, y: 12, w: 202, h: 216, borderRadius: 10, color: '#ffcc00', bgColor: '#0e1526', name: 'TPMS Chassis Panel' },
            { id: 'bar_thr', type: 'bar-slider', x: 30, y: 40, w: 22, h: 140, min: 0, max: 100, color: '#00ff66', bgColor: '#18243c', binding: 'throttle', name: 'Throttle Bar' },
            { id: 'lbl_thr', type: 'text-label', x: 41, y: 195, text: 'THR', fontSize: 10, fontFamily: 'Rajdhani, sans-serif', color: '#00ff66', name: 'THR Label' },
            { id: 'bar_brk', type: 'bar-slider', x: 68, y: 40, w: 22, h: 140, min: 0, max: 100, color: '#ff3366', bgColor: '#18243c', binding: 'brake', name: 'Brake Bar' },
            { id: 'lbl_brk', type: 'text-label', x: 79, y: 195, text: 'BRK', fontSize: 10, fontFamily: 'Rajdhani, sans-serif', color: '#ff3366', name: 'BRK Label' },
            { id: 'bar_rpm', type: 'bar-slider', x: 106, y: 40, w: 22, h: 140, min: 0, max: 8000, color: '#00f0ff', bgColor: '#18243c', binding: 'rpm', name: 'RPM Bar' },
            { id: 'lbl_rpm', type: 'text-label', x: 117, y: 195, text: 'RPM', fontSize: 10, fontFamily: 'Rajdhani, sans-serif', color: '#00f0ff', name: 'RPM Label' },
            { id: 'chart_history', type: 'history-chart', x: 180, y: 46, w: 378, h: 140, color: '#00ff66', name: 'Live CAN Waveform' },
            { id: 'chart_title', type: 'text-label', x: 250, y: 28, text: 'CAN TELEMETRY 50Hz TRACE', fontSize: 11, fontFamily: 'Orbitron, monospace', color: '#00ff66', name: 'Chart Header' },
            { id: 'chart_speed_val', type: 'digital-value', x: 510, y: 28, fontSize: 18, fontFamily: '"JetBrains Mono", monospace', color: '#ffffff', binding: 'speed_kmh', unit: ' km/h', name: 'Chart Speed' },
            { id: 'tpms_chassis', type: 'tpms-map', x: 687, y: 120, w: 180, h: 170, name: '4-Wheel TPMS' },
            { id: 'tpms_header', type: 'text-label', x: 687, y: 28, text: 'TPMS & G-METER', fontSize: 11, fontFamily: 'Orbitron, monospace', color: '#ffcc00', name: 'TPMS Header' }
        ];
        saveHistoryState();
    }

    function loadPresetDualCockpit() {
        const c1x = 120;
        const c2x = 360;
        const cy = 120;

        state.widgets = [
            { id: 'dual_shift', type: 'shift-lights', x: c1x, y: cy, radius: 105, ledRadius: 4, ledCount: 12, startAngle: 210, endAngle: 330, max: 7000, binding: 'rpm', name: 'Left Shift Arch' },
            { id: 'dual_rpm_arc', type: 'smooth-arc', x: c1x, y: cy, radius: 92, thickness: 8, startAngle: 135, endAngle: 405, color: '#00f0ff', trackColor: '#141c30', min: 0, max: 8000, binding: 'rpm', name: 'Left RPM Arc' },
            { id: 'dual_speed', type: 'digital-value', x: c1x, y: cy - 4, fontSize: 42, fontFamily: 'Orbitron, monospace', color: '#ffffff', binding: 'speed_kmh', name: 'Left Speed' },
            { id: 'dual_speed_u', type: 'text-label', x: c1x, y: cy + 22, text: 'KM/H', fontSize: 10, fontFamily: 'Rajdhani, sans-serif', color: '#8a99ad', name: 'Left Speed Unit' },
            { id: 'dual_gear', type: 'status-badge', x: c1x, y: cy + 50, text: '4', color: '#00f0ff', binding: 'gear', name: 'Left Gear' },
            { id: 'dual_gforce', type: 'g-force-meter', x: c2x, y: cy, radius: 78, color: '#00ff66', name: 'Right G-Force Radar' },
            { id: 'dual_g_label', type: 'text-label', x: c2x, y: 44, text: 'LATERAL G-FORCE', fontSize: 10, fontFamily: 'Orbitron, monospace', color: '#00ff66', name: 'G-Force Label' },
            { id: 'dual_temp', type: 'text-label', x: c2x - 45, y: cy + 62, text: '92°C', fontSize: 11, fontFamily: '"JetBrains Mono", monospace', color: '#ffcc00', binding: 'water_temp', name: 'Right Temp' },
            { id: 'dual_batt', type: 'text-label', x: c2x + 45, y: cy + 62, text: '13.8V', fontSize: 11, fontFamily: '"JetBrains Mono", monospace', color: '#00f0ff', binding: 'battery_v', name: 'Right Batt' }
        ];
        saveHistoryState();
    }

    function loadPresetCyberpunk() {
        const cx = state.preset.width / 2;
        const cy = state.preset.height / 2;

        state.widgets = [
            { id: 'cyber_bg_card', type: 'card-box', x: 20, y: 20, w: state.preset.width - 40, h: state.preset.height - 40, borderRadius: 16, color: '#a855f7', bgColor: '#090d1a', name: 'Cyberpunk Frame' },
            { id: 'cyber_shift', type: 'shift-lights', x: cx, y: cy, radius: 160, ledRadius: 5, ledCount: 16, startAngle: 200, endAngle: 340, max: 7500, binding: 'rpm', name: 'Cyber Shift Bar' },
            { id: 'cyber_rpm_arc', type: 'smooth-arc', x: cx, y: cy, radius: 140, thickness: 12, startAngle: 135, endAngle: 405, color: '#ff0055', trackColor: '#1c1033', min: 0, max: 8000, binding: 'rpm', name: 'Neon RPM Arc' },
            { id: 'cyber_speed', type: 'digital-value', x: cx, y: cy - 10, fontSize: 72, fontFamily: 'Orbitron, monospace', color: '#00f0ff', binding: 'speed_kmh', showGhost: true, ghostOpacity: 0.1, ghostDigits: '888', name: 'Neon Speed' },
            { id: 'cyber_unit', type: 'text-label', x: cx, y: cy + 40, text: 'KILOMETERS PER HOUR', fontSize: 11, fontFamily: 'Rajdhani, sans-serif', color: '#a855f7', name: 'Cyber Unit' },
            { id: 'cyber_gear', type: 'status-badge', x: cx, y: cy + 80, text: 'S6', color: '#ff0055', binding: 'gear', name: 'Hyper Gear' }
        ];
        saveHistoryState();
    }

    // =========================================================================
    // 9. CANVAS RENDERING ENGINE & NAVIGATION GRAPHICS
    // =========================================================================
    function updateBezelFrameDimensions() {
        canvas.width = state.preset.width;
        canvas.height = state.preset.height;

        bezelFrame.className = 'bezel-frame';
        if (!state.showBezel) {
            bezelFrame.classList.add('no-bezel');
        } else if (state.preset.shape === 'round') {
            bezelFrame.classList.add('round-display');
        } else if (state.preset.shape === 'dual-round') {
            bezelFrame.classList.add('dual-round-display');
        } else {
            bezelFrame.classList.add('rect-display');
        }

        document.getElementById('canvasDimInfo').textContent = `Resolution: ${state.preset.width} × ${state.preset.height} (${state.preset.hardware || state.preset.shape.toUpperCase()})`;
    }

    function renderCanvas() {
        ctx.save();
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        ctx.fillStyle = '#050811';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        if (state.gridSnap) {
            ctx.strokeStyle = '#0d1526';
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

        state.widgets.forEach(w => {
            ctx.save();
            renderWidget(w);
            ctx.restore();
        });

        if (state.selectedWidgetId) {
            const selW = state.widgets.find(w => w.id === state.selectedWidgetId);
            if (selW) drawSelectionHighlight(selW);
        }

        ctx.restore();
    }

    function getWidgetValue(w) {
        if (w.binding && state.telemetry[w.binding] !== undefined) {
            return state.telemetry[w.binding];
        }
        return w.value !== undefined ? w.value : 50;
    }

    // DRAW WIDGETS
    function renderWidget(w) {
        const val = getWidgetValue(w);

        switch (w.type) {
            case 'nav-turn-arrow': {
                const nw = w.w || 80;
                const nh = w.h || 80;
                ctx.save();
                ctx.translate(w.x, w.y);

                // Rounded background box
                ctx.fillStyle = w.bgColor || '#0f172a';
                ctx.strokeStyle = w.color || '#00ff66';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.roundRect(-nw / 2, -nh / 2, nw, nh, 12);
                ctx.fill();
                ctx.stroke();

                // Vector Turn Arrow
                const mType = state.telemetry.nav_maneuver || 'turn-right';
                ctx.strokeStyle = w.color || '#00ff66';
                ctx.fillStyle = w.color || '#00ff66';
                ctx.lineWidth = 6;
                ctx.lineCap = 'round';
                ctx.lineJoin = 'round';
                ctx.shadowColor = w.color || '#00ff66';
                ctx.shadowBlur = 8;

                ctx.beginPath();
                if (mType === 'turn-right') {
                    ctx.moveTo(-16, 18);
                    ctx.lineTo(-16, -4);
                    ctx.arcTo(-16, -18, 0, -18, 16);
                    ctx.lineTo(16, -18);
                    ctx.stroke();
                    // Arrowhead
                    ctx.beginPath();
                    ctx.moveTo(10, -26);
                    ctx.lineTo(22, -18);
                    ctx.lineTo(10, -10);
                    ctx.fill();
                } else if (mType === 'turn-left') {
                    ctx.moveTo(16, 18);
                    ctx.lineTo(16, -4);
                    ctx.arcTo(16, -18, 0, -18, 16);
                    ctx.lineTo(-16, -18);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(-10, -26);
                    ctx.lineTo(-22, -18);
                    ctx.lineTo(-10, -10);
                    ctx.fill();
                } else if (mType === 'slight-right') {
                    ctx.moveTo(-12, 18);
                    ctx.lineTo(14, -14);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(4, -20);
                    ctx.lineTo(20, -18);
                    ctx.lineTo(16, -2);
                    ctx.fill();
                } else if (mType === 'slight-left') {
                    ctx.moveTo(12, 18);
                    ctx.lineTo(-14, -14);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(-4, -20);
                    ctx.lineTo(-20, -18);
                    ctx.lineTo(-16, -2);
                    ctx.fill();
                } else if (mType === 'u-turn') {
                    ctx.moveTo(14, 18);
                    ctx.lineTo(14, -6);
                    ctx.arcTo(14, -22, 0, -22, 14);
                    ctx.arcTo(-14, -22, -14, -6, 14);
                    ctx.lineTo(-14, 14);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(-20, 8);
                    ctx.lineTo(-14, 20);
                    ctx.lineTo(-8, 8);
                    ctx.fill();
                } else {
                    // Straight Arrow
                    ctx.moveTo(0, 18);
                    ctx.lineTo(0, -16);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(-8, -10);
                    ctx.lineTo(0, -22);
                    ctx.lineTo(8, -10);
                    ctx.fill();
                }

                ctx.restore();
                break;
            }

            case 'nav-maneuver-banner': {
                const bw = w.w || 300;
                const bh = w.h || 44;
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.fillStyle = w.bgColor || '#0f172a';
                ctx.strokeStyle = w.color || '#00f0ff';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.roundRect(-bw / 2, -bh / 2, bw, bh, 8);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = '#00ff66';
                ctx.font = '700 16px Orbitron, monospace';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'middle';
                ctx.fillText(`${state.telemetry.nav_dist_m} m`, -bw / 2 + 14, 0);

                ctx.fillStyle = '#ffffff';
                ctx.font = '600 13px Rajdhani, sans-serif';
                ctx.textAlign = 'right';
                ctx.fillText(state.telemetry.nav_road, bw / 2 - 14, 0);
                ctx.restore();
                break;
            }

            case 'nav-eta-badge': {
                const ew = w.w || 280;
                const eh = w.h || 40;
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.fillStyle = '#0c1424';
                ctx.strokeStyle = '#1e2d4a';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.roundRect(-ew / 2, -eh / 2, ew, eh, 8);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = '#00ff66';
                ctx.font = '700 13px "JetBrains Mono", monospace';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'middle';
                ctx.fillText(`ETA ${state.telemetry.nav_eta}`, -ew / 2 + 12, 0);

                ctx.fillStyle = '#ffffff';
                ctx.font = '600 12px "JetBrains Mono", monospace';
                ctx.textAlign = 'center';
                ctx.fillText(state.telemetry.nav_rem_time, 0, 0);

                ctx.fillStyle = '#8a99ad';
                ctx.font = '600 12px "JetBrains Mono", monospace';
                ctx.textAlign = 'right';
                ctx.fillText(state.telemetry.nav_rem_km, ew / 2 - 12, 0);
                ctx.restore();
                break;
            }

            case 'nav-lane-assist': {
                const lw = w.w || 180;
                const lh = w.h || 24;
                ctx.save();
                ctx.translate(w.x, w.y);
                const lanes = [
                    { dir: '↖', active: false },
                    { dir: '↑', active: true },
                    { dir: '↑', active: true },
                    { dir: '↗', active: false }
                ];
                const laneW = lw / lanes.length;

                lanes.forEach((l, i) => {
                    const lx = -lw / 2 + i * laneW;
                    ctx.fillStyle = l.active ? '#00ff66' : '#1e293b';
                    ctx.strokeStyle = l.active ? '#00ff66' : '#334155';
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.roundRect(lx + 2, -lh / 2, laneW - 4, lh, 4);
                    ctx.fill();
                    ctx.stroke();

                    ctx.fillStyle = l.active ? '#050811' : '#64748b';
                    ctx.font = '700 13px sans-serif';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(l.dir, lx + laneW / 2, 0);
                });
                ctx.restore();
                break;
            }

            case 'nav-camera-alert': {
                const cw = w.w || 200;
                const ch = w.h || 32;
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.fillStyle = 'rgba(255, 204, 0, 0.15)';
                ctx.strokeStyle = w.color || '#ffcc00';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.roundRect(-cw / 2, -ch / 2, cw, ch, 6);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = '#ffcc00';
                ctx.font = '700 12px Rajdhani, sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(state.telemetry.nav_hazard_msg, 0, 0);
                ctx.restore();
                break;
            }

            case 'nav-hazard-alert': {
                const hw = w.w || 190;
                const hh = w.h || 60;
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.fillStyle = '#1c1014';
                ctx.strokeStyle = '#ff3366';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.roundRect(-hw / 2, -hh / 2, hw, hh, 8);
                ctx.fill();
                ctx.stroke();

                ctx.fillStyle = '#ff3366';
                ctx.font = '700 14px Rajdhani, sans-serif';
                ctx.textAlign = 'center';
                ctx.fillText('⚠️ POLICE RADAR', 0, -8);

                ctx.fillStyle = '#ffffff';
                ctx.font = '600 11px Inter, sans-serif';
                ctx.fillText('Reported 400m ahead', 0, 14);
                ctx.restore();
                break;
            }

            case 'shift-lights': {
                const sRadius = w.radius || 110;
                const sStartDeg = w.startAngle !== undefined ? w.startAngle : 210;
                const sEndDeg = w.endAngle !== undefined ? w.endAngle : 330;
                const sStartA = sStartDeg * Math.PI / 180;
                const sEndA = sEndDeg * Math.PI / 180;
                const count = w.ledCount || 12;
                const maxRpm = w.max || 7000;
                const rpmPct = Math.min(1.0, Math.max(0, val / maxRpm));
                const isRedlineFlashing = rpmPct >= 0.95 && (Math.floor(Date.now() / 100) % 2 === 0);

                for (let i = 0; i < count; i++) {
                    const angle = sStartA + (i / (count - 1)) * (sEndA - sStartA);
                    const lx = w.x + Math.cos(angle) * sRadius;
                    const ly = w.y + Math.sin(angle) * sRadius;
                    const threshold = (i + 1) / count;

                    ctx.beginPath();
                    ctx.arc(lx, ly, w.ledRadius || 4, 0, Math.PI * 2);

                    if (isRedlineFlashing) {
                        ctx.fillStyle = '#ff0055';
                        ctx.shadowColor = '#ff0055';
                        ctx.shadowBlur = 12;
                    } else if (rpmPct >= threshold) {
                        let ledColor = w.color || '#00ff66';
                        if (i >= count * 0.75) ledColor = '#ff3366';
                        else if (i >= count * 0.45) ledColor = '#ffcc00';
                        ctx.fillStyle = ledColor;
                        ctx.shadowColor = ledColor;
                        ctx.shadowBlur = 8;
                    } else {
                        ctx.fillStyle = w.trackColor || '#141e30';
                        ctx.shadowBlur = 0;
                    }
                    ctx.fill();
                    ctx.shadowBlur = 0;
                }
                break;
            }

            case 'rev-strip': {
                const count = w.ledCount || 16;
                const rw = w.w || 220;
                const rh = w.h || 14;
                const rpmPct = Math.min(1.0, Math.max(0, val / (w.max || 8000)));
                const ledW = (rw - (count - 1) * 3) / count;

                for (let i = 0; i < count; i++) {
                    const lx = w.x + i * (ledW + 3);
                    const threshold = (i + 1) / count;
                    ctx.beginPath();
                    ctx.roundRect(lx, w.y, ledW, rh, 2);

                    if (rpmPct >= threshold) {
                        let c = w.color || '#00ff66';
                        if (i >= count * 0.8) c = '#00f0ff';
                        else if (i >= count * 0.6) c = '#ff0055';
                        else if (i >= count * 0.35) c = '#ffcc00';
                        ctx.fillStyle = c;
                        ctx.shadowColor = c;
                        ctx.shadowBlur = 6;
                    } else {
                        ctx.fillStyle = w.trackColor || '#141e30';
                        ctx.shadowBlur = 0;
                    }
                    ctx.fill();
                    ctx.shadowBlur = 0;
                }
                break;
            }

            case 'smooth-arc': {
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

                ctx.beginPath();
                ctx.arc(w.x, w.y, w.radius || 100, startA, endA, counterClockwise);
                ctx.strokeStyle = w.trackColor || '#121829';
                ctx.lineWidth = w.thickness || 12;
                ctx.lineCap = 'round';
                ctx.stroke();

                if (normVal > 0.001) {
                    ctx.beginPath();
                    ctx.arc(w.x, w.y, w.radius || 100, startA, valA, counterClockwise);
                    ctx.strokeStyle = w.color || '#00ff66';
                    ctx.lineWidth = w.thickness || 12;
                    ctx.lineCap = 'round';
                    ctx.shadowColor = w.color || '#00ff66';
                    ctx.shadowBlur = 10;
                    ctx.stroke();
                    ctx.shadowBlur = 0;
                }
                break;
            }

            case 'boost-gauge': {
                const minB = w.min || -1.0;
                const maxB = w.max || 2.5;
                const bNorm = Math.min(Math.max((val - minB) / (maxB - minB), 0), 1);
                const bAngle = (135 + bNorm * 270) * Math.PI / 180;

                ctx.beginPath();
                ctx.arc(w.x, w.y, w.radius || 60, 135 * Math.PI / 180, 405 * Math.PI / 180);
                ctx.strokeStyle = w.trackColor || '#141e30';
                ctx.lineWidth = 8;
                ctx.stroke();

                const boostColor = val > 0 ? (w.color || '#ff6600') : '#00f0ff';
                ctx.beginPath();
                ctx.arc(w.x, w.y, w.radius || 60, 135 * Math.PI / 180, bAngle);
                ctx.strokeStyle = boostColor;
                ctx.lineWidth = 8;
                ctx.shadowColor = boostColor;
                ctx.shadowBlur = 8;
                ctx.stroke();
                ctx.shadowBlur = 0;

                ctx.fillStyle = '#ffffff';
                ctx.font = '700 16px Orbitron, monospace';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(`${val > 0 ? '+' : ''}${val} bar`, w.x, w.y);
                break;
            }

            case 'digital-value': {
                const fontStr = `${w.fontWeight || '700'} ${w.fontSize || 36}px ${w.fontFamily || 'Orbitron, monospace'}`;
                ctx.font = fontStr;
                ctx.textAlign = w.textAlign || 'center';
                ctx.textBaseline = 'middle';

                const displayStr = `${val}${w.unit || ''}`;

                if (w.showGhost) {
                    ctx.save();
                    ctx.fillStyle = w.color || '#00f0ff';
                    ctx.globalAlpha = w.ghostOpacity !== undefined ? w.ghostOpacity : 0.08;
                    ctx.fillText(w.ghostDigits || '888', w.x, w.y);
                    ctx.restore();
                }

                ctx.fillStyle = w.color || '#ffffff';
                ctx.shadowColor = w.color || '#00f0ff';
                ctx.shadowBlur = w.glow ? 10 : 0;
                ctx.fillText(displayStr, w.x, w.y);
                ctx.shadowBlur = 0;
                break;
            }

            case 'speed-sign': {
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.beginPath();
                ctx.arc(0, 0, 22, 0, Math.PI * 2);
                ctx.fillStyle = '#ffffff';
                ctx.fill();
                ctx.strokeStyle = '#ff0033';
                ctx.lineWidth = 5;
                ctx.stroke();

                ctx.fillStyle = '#000000';
                ctx.font = '800 16px "Inter", sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(w.text || '90', 0, 0);
                ctx.restore();
                break;
            }

            case 'text-label': {
                ctx.fillStyle = w.color || '#8a99ad';
                ctx.font = `${w.fontWeight || '600'} ${w.fontSize || 12}px ${w.fontFamily || 'Inter, sans-serif'}`;
                ctx.textAlign = w.textAlign || 'center';
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
            }

            case 'dial-needle': {
                const nStartA = (w.startAngle || 135) * Math.PI / 180;
                const nEndA = (w.endAngle || 405) * Math.PI / 180;
                const nNorm = Math.min(Math.max((val - (w.min || 0)) / ((w.max || 100) - (w.min || 0)), 0), 1);
                const nAngle = nStartA + nNorm * (nEndA - nStartA);

                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.rotate(nAngle);

                ctx.beginPath();
                ctx.moveTo(-10, 0);
                ctx.lineTo(w.radius || 70, 0);
                ctx.strokeStyle = w.color || '#ff3366';
                ctx.lineWidth = w.thickness || 4;
                ctx.lineCap = 'round';
                ctx.shadowColor = w.color || '#ff3366';
                ctx.shadowBlur = 8;
                ctx.stroke();

                ctx.beginPath();
                ctx.arc(0, 0, 8, 0, Math.PI * 2);
                ctx.fillStyle = '#0f172a';
                ctx.fill();
                ctx.strokeStyle = w.color || '#ff3366';
                ctx.lineWidth = 2;
                ctx.stroke();
                ctx.restore();
                break;
            }

            case 'gauge-ticks': {
                const tCount = w.tickCount || 9;
                const tStartA = (w.startAngle || 135) * Math.PI / 180;
                const tEndA = (w.endAngle || 405) * Math.PI / 180;
                const tRadius = w.radius || 100;
                const tLen = w.tickLen || 10;

                ctx.strokeStyle = w.color || '#64748b';
                ctx.lineWidth = 2;

                for (let i = 0; i < tCount; i++) {
                    const angle = tStartA + (i / (tCount - 1)) * (tEndA - tStartA);
                    const x1 = w.x + Math.cos(angle) * tRadius;
                    const y1 = w.y + Math.sin(angle) * tRadius;
                    const x2 = w.x + Math.cos(angle) * (tRadius - tLen);
                    const y2 = w.y + Math.sin(angle) * (tRadius - tLen);

                    ctx.beginPath();
                    ctx.moveTo(x1, y1);
                    ctx.lineTo(x2, y2);
                    ctx.stroke();
                }
                break;
            }

            case 'bar-slider': {
                const barW = w.w || 140;
                const barH = w.h || 16;
                const bMin = w.min || 0;
                const bMax = w.max || 100;
                const bNorm = Math.min(Math.max((val - bMin) / (bMax - bMin), 0), 1);

                ctx.fillStyle = w.bgColor || '#141c30';
                ctx.beginPath();
                ctx.roundRect(w.x, w.y, barW, barH, w.borderRadius || 4);
                ctx.fill();

                if (barH > barW) {
                    const fillH = barH * bNorm;
                    ctx.fillStyle = w.color || '#00ff66';
                    ctx.beginPath();
                    ctx.roundRect(w.x, w.y + (barH - fillH), barW, fillH, w.borderRadius || 4);
                    ctx.fill();
                } else {
                    ctx.fillStyle = w.color || '#00f0ff';
                    ctx.beginPath();
                    ctx.roundRect(w.x, w.y, barW * bNorm, barH, w.borderRadius || 4);
                    ctx.fill();
                }
                break;
            }

            case 'history-chart': {
                const cw = w.w || 380;
                const ch = w.h || 140;

                ctx.fillStyle = '#080d1a';
                ctx.fillRect(w.x, w.y, cw, ch);
                ctx.strokeStyle = '#18243c';
                ctx.lineWidth = 1;
                ctx.strokeRect(w.x, w.y, cw, ch);

                ctx.beginPath();
                const pts = state.chartHistory.rpm;
                for (let i = 0; i < pts.length; i++) {
                    const px = w.x + (i / (pts.length - 1)) * cw;
                    const py = w.y + ch - (pts[i] / 8000) * ch;
                    if (i === 0) ctx.moveTo(px, py);
                    else ctx.lineTo(px, py);
                }
                ctx.strokeStyle = w.color || '#00ff66';
                ctx.lineWidth = 2;
                ctx.stroke();
                break;
            }

            case 'g-force-meter': {
                const gRadius = w.radius || 70;
                ctx.save();
                ctx.translate(w.x, w.y);

                [0.33, 0.66, 1.0].forEach((rFactor, idx) => {
                    ctx.beginPath();
                    ctx.arc(0, 0, gRadius * rFactor, 0, Math.PI * 2);
                    ctx.strokeStyle = idx === 1 ? '#ffcc00' : '#1e2942';
                    ctx.lineWidth = 1;
                    ctx.stroke();
                });

                ctx.beginPath();
                ctx.moveTo(-gRadius, 0);
                ctx.lineTo(gRadius, 0);
                ctx.moveTo(0, -gRadius);
                ctx.lineTo(0, gRadius);
                ctx.strokeStyle = '#1e2942';
                ctx.stroke();

                const gx = (state.telemetry.lat_g / 1.5) * gRadius;
                const gy = -(state.telemetry.long_g / 1.5) * gRadius;

                ctx.beginPath();
                ctx.arc(gx, gy, 5, 0, Math.PI * 2);
                ctx.fillStyle = w.color || '#00ff66';
                ctx.shadowColor = w.color || '#00ff66';
                ctx.shadowBlur = 8;
                ctx.fill();
                ctx.restore();
                break;
            }

            case 'tpms-map': {
                ctx.save();
                ctx.translate(w.x, w.y);
                ctx.strokeStyle = '#273654';
                ctx.lineWidth = 2;
                ctx.strokeRect(-24, -40, 48, 80);

                const tires = [
                    { x: -50, y: -30, label: `${state.telemetry.tpms_fl}b`, color: '#00ff66' },
                    { x: 50, y: -30, label: `${state.telemetry.tpms_fr}b`, color: '#00ff66' },
                    { x: -50, y: 30, label: `${state.telemetry.tpms_rl}b`, color: '#00ff66' },
                    { x: 50, y: 30, label: `${state.telemetry.tpms_rr}b`, color: '#00ff66' }
                ];

                tires.forEach(t => {
                    ctx.fillStyle = '#141c30';
                    ctx.fillRect(t.x - 14, t.y - 12, 28, 24);
                    ctx.strokeStyle = t.color;
                    ctx.lineWidth = 1.5;
                    ctx.strokeRect(t.x - 14, t.y - 12, 28, 24);

                    ctx.fillStyle = '#ffffff';
                    ctx.font = '600 10px Orbitron, monospace';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText(t.label, t.x, t.y);
                });
                ctx.restore();
                break;
            }

            case 'status-badge': {
                let txt = w.text || 'STATUS';
                if (w.binding === 'gear') {
                    const gears = ['P', 'R', 'N', 'D', 'S', '1', '2', '3', '4', '5', '6'];
                    txt = gears[state.telemetry.gear] || `${val}`;
                }

                ctx.beginPath();
                ctx.arc(w.x, w.y, 16, 0, Math.PI * 2);
                ctx.fillStyle = '#101726';
                ctx.fill();
                ctx.strokeStyle = w.color || '#00f0ff';
                ctx.lineWidth = 2;
                ctx.stroke();

                ctx.fillStyle = '#ffffff';
                ctx.font = '700 14px Orbitron, monospace';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(txt, w.x, w.y);
                break;
            }

            case 'card-box': {
                ctx.fillStyle = w.bgColor || '#0e1526';
                ctx.strokeStyle = w.color || '#00f0ff';
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.roundRect(w.x, w.y, w.w || 100, w.h || 60, w.borderRadius || 8);
                ctx.fill();
                ctx.stroke();
                break;
            }
        }
    }

    function drawSelectionHighlight(w) {
        let bx = w.x - 20, by = w.y - 20, bw = 40, bh = 40;

        if (w.w && w.h) {
            bx = w.x;
            by = w.y;
            bw = w.w;
            bh = w.h;
        } else if (w.radius) {
            bx = w.x - w.radius;
            by = w.y - w.radius;
            bw = w.radius * 2;
            bh = w.radius * 2;
        }

        ctx.save();
        ctx.strokeStyle = '#00f0ff';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 4]);
        ctx.strokeRect(bx - 4, by - 4, bw + 8, bh + 8);

        ctx.setLineDash([]);
        ctx.fillStyle = '#00f0ff';
        const corners = [
            [bx - 7, by - 7],
            [bx + bw + 1, by - 7],
            [bx - 7, by + bh + 1],
            [bx + bw + 1, by + bh + 1]
        ];
        corners.forEach(([cx, cy]) => {
            ctx.fillRect(cx, cy, 6, 6);
        });

        ctx.restore();
    }

    // =========================================================================
    // 10. INSPECTOR PANEL (WITH DELETE & NAVIGATION CONTROLS)
    // =========================================================================
    function renderInspector() {
        if (!state.selectedWidgetId) {
            inspectorTitle.textContent = 'Widget Inspector';
            inspectorContent.innerHTML = '<p class="no-selection-msg">Select a widget on the canvas to configure properties, navigation & typography</p>';
            return;
        }

        const w = state.widgets.find(item => item.id === state.selectedWidgetId);
        if (!w) return;

        inspectorTitle.textContent = `Inspector: ${w.type}`;

        let html = '';

        // SECTION 1: Identity & Layer
        html += `
        <div class="prop-section">
            <span class="prop-section-title">Identity</span>
            <div class="prop-input-group">
                <label>Widget Name / Layer</label>
                <input type="text" class="styled-input" id="propName" value="${w.name || w.id}">
            </div>
            <div class="prop-input-group">
                <label>Telemetry / GPS Binding</label>
                <select class="styled-select" id="propBinding">
                    <option value="" ${!w.binding ? 'selected' : ''}>(None / Static)</option>
                    <option value="rpm" ${w.binding === 'rpm' ? 'selected' : ''}>Engine RPM (0-8000)</option>
                    <option value="speed_kmh" ${w.binding === 'speed_kmh' ? 'selected' : ''}>Vehicle Speed (km/h)</option>
                    <option value="gear" ${w.binding === 'gear' ? 'selected' : ''}>Transmission Gear</option>
                    <option value="throttle" ${w.binding === 'throttle' ? 'selected' : ''}>Throttle Position (%)</option>
                    <option value="brake" ${w.binding === 'brake' ? 'selected' : ''}>Brake Pressure (%)</option>
                    <option value="boost_bar" ${w.binding === 'boost_bar' ? 'selected' : ''}>Turbo Boost (bar)</option>
                    <option value="fuel_pct" ${w.binding === 'fuel_pct' ? 'selected' : ''}>Fuel Level (%)</option>
                    <option value="water_temp" ${w.binding === 'water_temp' ? 'selected' : ''}>Coolant Temp (°C)</option>
                    <option value="oil_temp" ${w.binding === 'oil_temp' ? 'selected' : ''}>Engine Oil Temp (°C)</option>
                    <option value="battery_v" ${w.binding === 'battery_v' ? 'selected' : ''}>Battery Voltage (V)</option>
                    <option value="steering_deg" ${w.binding === 'steering_deg' ? 'selected' : ''}>Steering Angle (°)</option>
                    <option value="lat_g" ${w.binding === 'lat_g' ? 'selected' : ''}>Lateral G-Force (g)</option>
                </select>
            </div>
        </div>`;

        // SECTION 2: Position & Dimensions
        html += `
        <div class="prop-section">
            <span class="prop-section-title">Transform & Geometry</span>
            <div class="prop-grid-2">
                <div class="prop-input-group">
                    <label>X Pos</label>
                    <input type="number" class="styled-input" id="propX" value="${w.x}">
                </div>
                <div class="prop-input-group">
                    <label>Y Pos</label>
                    <input type="number" class="styled-input" id="propY" value="${w.y}">
                </div>
            </div>`;

        if (w.radius !== undefined) {
            html += `
            <div class="prop-input-group">
                <label>Radius (px)</label>
                <input type="number" class="styled-input" id="propRadius" value="${w.radius}">
            </div>`;
        }

        if (w.w !== undefined && w.h !== undefined) {
            html += `
            <div class="prop-grid-2">
                <div class="prop-input-group">
                    <label>Width (px)</label>
                    <input type="number" class="styled-input" id="propW" value="${w.w}">
                </div>
                <div class="prop-input-group">
                    <label>Height (px)</label>
                    <input type="number" class="styled-input" id="propH" value="${w.h}">
                </div>
            </div>`;
        }

        if (w.startAngle !== undefined && w.endAngle !== undefined) {
            html += `
            <div class="prop-grid-2">
                <div class="prop-input-group">
                    <label>Start Angle (°)</label>
                    <input type="number" class="styled-input" id="propStartAngle" value="${w.startAngle}">
                </div>
                <div class="prop-input-group">
                    <label>End Angle (°)</label>
                    <input type="number" class="styled-input" id="propEndAngle" value="${w.endAngle}">
                </div>
            </div>`;
        }
        html += `</div>`;

        // SECTION 3: Typography
        if (w.type === 'digital-value' || w.type === 'text-label' || w.type === 'speed-sign') {
            const allFonts = [...FONT_FAMILIES, ...state.loadedCustomFonts];
            html += `
            <div class="prop-section">
                <span class="prop-section-title">Typography & Digital Font</span>
                <div class="prop-input-group">
                    <label>Font Family</label>
                    <select class="styled-select" id="propFontFamily">
                        ${allFonts.map(f => `<option value='${f.value}' ${w.fontFamily === f.value ? 'selected' : ''}>${f.label}</option>`).join('')}
                    </select>
                </div>
                <div class="prop-grid-2">
                    <div class="prop-input-group">
                        <label>Font Size (px)</label>
                        <input type="number" class="styled-input" id="propFontSize" value="${w.fontSize || 32}">
                    </div>
                    <div class="prop-input-group">
                        <label>Text Align</label>
                        <select class="styled-select" id="propTextAlign">
                            <option value="left" ${w.textAlign === 'left' ? 'selected' : ''}>Left</option>
                            <option value="center" ${!w.textAlign || w.textAlign === 'center' ? 'selected' : ''}>Center</option>
                            <option value="right" ${w.textAlign === 'right' ? 'selected' : ''}>Right</option>
                        </select>
                    </div>
                </div>`;

            if (w.type === 'digital-value') {
                html += `
                <div class="prop-row">
                    <label>7-Segment Ghost Digit Backdrop</label>
                    <input type="checkbox" id="propShowGhost" ${w.showGhost ? 'checked' : ''}>
                </div>
                <div class="prop-input-group">
                    <label>Unit Suffix (e.g. km/h, %, V)</label>
                    <input type="text" class="styled-input" id="propUnit" value="${w.unit || ''}">
                </div>`;
            } else {
                html += `
                <div class="prop-input-group">
                    <label>Static Text Content</label>
                    <input type="text" class="styled-input" id="propText" value="${w.text || ''}">
                </div>`;
            }
            html += `</div>`;
        }

        // SECTION 4: Colors & Styling
        html += `
        <div class="prop-section">
            <span class="prop-section-title">Colors & Glow</span>
            <div class="prop-row">
                <label>Primary Accent Color</label>
                <div class="color-picker-row">
                    <input type="color" id="propColor" value="${w.color || '#00f0ff'}">
                    <span>${w.color || '#00f0ff'}</span>
                </div>
            </div>`;

        if (w.bgColor !== undefined) {
            html += `
            <div class="prop-row">
                <label>Background Fill Color</label>
                <div class="color-picker-row">
                    <input type="color" id="propBgColor" value="${w.bgColor || '#141c30'}">
                    <span>${w.bgColor || '#141c30'}</span>
                </div>
            </div>`;
        }

        if (w.trackColor !== undefined) {
            html += `
            <div class="prop-row">
                <label>Track Arc Color</label>
                <div class="color-picker-row">
                    <input type="color" id="propTrackColor" value="${w.trackColor || '#121829'}">
                    <span>${w.trackColor || '#121829'}</span>
                </div>
            </div>`;
        }

        html += `</div>`;

        // SECTION 5: DELETE BUTTON
        html += `
        <button id="inspectorDeleteBtn" class="btn-delete-widget">
            🗑️ Delete Selected Widget [Del]
        </button>`;

        inspectorContent.innerHTML = html;
        attachInspectorEventListeners(w);
    }

    function attachInspectorEventListeners(w) {
        const bindInput = (id, key, parser = v => v) => {
            const el = document.getElementById(id);
            if (!el) return;
            el.addEventListener('input', (e) => {
                saveHistoryState();
                w[key] = parser(e.target.value);
                renderCanvas();
                renderLayersList();
            });
        };

        bindInput('propName', 'name');
        bindInput('propBinding', 'binding');
        bindInput('propX', 'x', parseInt);
        bindInput('propY', 'y', parseInt);
        bindInput('propRadius', 'radius', parseInt);
        bindInput('propW', 'w', parseInt);
        bindInput('propH', 'h', parseInt);
        bindInput('propStartAngle', 'startAngle', parseInt);
        bindInput('propEndAngle', 'endAngle', parseInt);
        bindInput('propFontFamily', 'fontFamily');
        bindInput('propFontSize', 'fontSize', parseInt);
        bindInput('propTextAlign', 'textAlign');
        bindInput('propUnit', 'unit');
        bindInput('propText', 'text');
        bindInput('propColor', 'color');
        bindInput('propBgColor', 'bgColor');
        bindInput('propTrackColor', 'trackColor');

        const ghostEl = document.getElementById('propShowGhost');
        if (ghostEl) {
            ghostEl.addEventListener('change', (e) => {
                saveHistoryState();
                w.showGhost = e.target.checked;
                renderCanvas();
            });
        }

        document.getElementById('inspectorDeleteBtn')?.addEventListener('click', deleteSelectedWidget);
    }

    // =========================================================================
    // 11. LAYERS PANEL (WITH DIRECT DELETE & DUPLICATE BUTTONS)
    // =========================================================================
    function renderLayersList() {
        layersList.innerHTML = '';
        state.widgets.forEach((w, idx) => {
            const div = document.createElement('div');
            div.className = `layer-item ${w.id === state.selectedWidgetId ? 'active' : ''}`;
            div.innerHTML = `
                <span class="layer-item-title">${w.name || w.id}</span>
                <div class="layer-btn-group">
                    <button class="layer-icon-btn duplicate-layer" title="Duplicate Layer">⧉</button>
                    <button class="layer-icon-btn delete-layer" title="Delete Layer">🗑️</button>
                </div>
            `;
            
            div.addEventListener('click', (e) => {
                if (e.target.classList.contains('layer-icon-btn')) return;
                state.selectedWidgetId = w.id;
                renderInspector();
                renderLayersList();
                renderCanvas();
            });

            div.querySelector('.duplicate-layer')?.addEventListener('click', (e) => {
                e.stopPropagation();
                saveHistoryState();
                const clone = JSON.parse(JSON.stringify(w));
                clone.id = `${w.type}_${Date.now().toString().slice(-4)}`;
                clone.name = `${w.name || w.type} (Copy)`;
                clone.x += 16;
                clone.y += 16;
                state.widgets.splice(idx + 1, 0, clone);
                state.selectedWidgetId = clone.id;
                renderLayersList();
                renderInspector();
                renderCanvas();
            });

            div.querySelector('.delete-layer')?.addEventListener('click', (e) => {
                e.stopPropagation();
                saveHistoryState();
                state.widgets = state.widgets.filter(item => item.id !== w.id);
                if (state.selectedWidgetId === w.id) state.selectedWidgetId = null;
                renderLayersList();
                renderInspector();
                renderCanvas();
            });

            layersList.appendChild(div);
        });
    }

    document.getElementById('clearLayersBtn')?.addEventListener('click', () => {
        if (confirm('Clear all widgets from canvas?')) {
            saveHistoryState();
            state.widgets = [];
            state.selectedWidgetId = null;
            renderLayersList();
            renderInspector();
            renderCanvas();
        }
    });

    // Alignment
    function alignSelected(direction) {
        if (!state.selectedWidgetId) return;
        const w = state.widgets.find(item => item.id === state.selectedWidgetId);
        if (!w) return;

        saveHistoryState();
        const cw = canvas.width;
        const ch = canvas.height;

        switch (direction) {
            case 'left':
                w.x = w.w ? 0 : 20;
                break;
            case 'centerH':
                w.x = w.w ? Math.round((cw - w.w) / 2) : Math.round(cw / 2);
                break;
            case 'right':
                w.x = w.w ? cw - w.w : cw - 20;
                break;
            case 'top':
                w.y = w.h ? 0 : 20;
                break;
            case 'centerV':
                w.y = w.h ? Math.round((ch - w.h) / 2) : Math.round(ch / 2);
                break;
            case 'bottom':
                w.y = w.h ? ch - w.h : ch - 20;
                break;
        }

        renderInspector();
        renderCanvas();
    }

    document.getElementById('alignLeftBtn')?.addEventListener('click', () => alignSelected('left'));
    document.getElementById('alignCenterHBtn')?.addEventListener('click', () => alignSelected('centerH'));
    document.getElementById('alignRightBtn')?.addEventListener('click', () => alignSelected('right'));
    document.getElementById('alignTopBtn')?.addEventListener('click', () => alignSelected('top'));
    document.getElementById('alignCenterVBtn')?.addEventListener('click', () => alignSelected('centerV'));
    document.getElementById('alignBottomBtn')?.addEventListener('click', () => alignSelected('bottom'));
    document.getElementById('duplicateWidgetBtn')?.addEventListener('click', duplicateSelectedWidget);
    document.getElementById('deleteWidgetBtn')?.addEventListener('click', deleteSelectedWidget);

    document.getElementById('layerUpBtn')?.addEventListener('click', () => {
        if (!state.selectedWidgetId) return;
        const idx = state.widgets.findIndex(w => w.id === state.selectedWidgetId);
        if (idx > 0) {
            saveHistoryState();
            const item = state.widgets.splice(idx, 1)[0];
            state.widgets.splice(idx - 1, 0, item);
            renderLayersList();
            renderCanvas();
        }
    });

    document.getElementById('layerDownBtn')?.addEventListener('click', () => {
        if (!state.selectedWidgetId) return;
        const idx = state.widgets.findIndex(w => w.id === state.selectedWidgetId);
        if (idx < state.widgets.length - 1 && idx !== -1) {
            saveHistoryState();
            const item = state.widgets.splice(idx, 1)[0];
            state.widgets.splice(idx + 1, 0, item);
            renderLayersList();
            renderCanvas();
        }
    });

    // =========================================================================
    // 12. CANVAS DRAG AND DROP
    // =========================================================================
    canvas.addEventListener('mousedown', (e) => {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const mx = (e.clientX - rect.left) * scaleX;
        const my = (e.clientY - rect.top) * scaleY;

        let hitWidget = null;
        for (let i = state.widgets.length - 1; i >= 0; i--) {
            const w = state.widgets[i];
            let bx = w.x, by = w.y, bw = 50, bh = 50;

            if (w.w && w.h) {
                bx = w.x - (w.w / 2);
                by = w.y - (w.h / 2);
                bw = w.w;
                bh = w.h;
            } else if (w.radius) {
                bx = w.x - w.radius;
                by = w.y - w.radius;
                bw = w.radius * 2;
                bh = w.radius * 2;
            } else {
                bx = w.x - 30;
                by = w.y - 20;
                bw = 60;
                bh = 40;
            }

            if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                hitWidget = w;
                break;
            }
        }

        if (hitWidget) {
            state.selectedWidgetId = hitWidget.id;
            state.draggingWidgetId = hitWidget.id;
            state.dragOffset = { x: mx - hitWidget.x, y: my - hitWidget.y };
            saveHistoryState();
        } else {
            state.selectedWidgetId = null;
        }

        renderInspector();
        renderLayersList();
        renderCanvas();
    });

    window.addEventListener('mousemove', (e) => {
        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        const mx = Math.round((e.clientX - rect.left) * scaleX);
        const my = Math.round((e.clientY - rect.top) * scaleY);

        document.getElementById('cursorPosInfo').textContent = `X: ${mx}, Y: ${my}`;

        if (state.draggingWidgetId) {
            const w = state.widgets.find(item => item.id === state.draggingWidgetId);
            if (w) {
                let targetX = mx - state.dragOffset.x;
                let targetY = my - state.dragOffset.y;

                if (state.gridSnap) {
                    targetX = Math.round(targetX / state.gridSize) * state.gridSize;
                    targetY = Math.round(targetY / state.gridSize) * state.gridSize;
                }

                w.x = targetX;
                w.y = targetY;
                renderCanvas();
            }
        }
    });

    window.addEventListener('mouseup', () => {
        if (state.draggingWidgetId) {
            state.draggingWidgetId = null;
            renderInspector();
        }
    });

    document.querySelectorAll('.widget-item').forEach(item => {
        item.addEventListener('dragstart', (e) => {
            e.dataTransfer.setData('text/plain', item.getAttribute('data-type'));
        });
    });

    canvas.addEventListener('dragover', (e) => e.preventDefault());

    canvas.addEventListener('drop', (e) => {
        e.preventDefault();
        const widgetType = e.dataTransfer.getData('text/plain');
        if (!widgetType) return;

        const rect = canvas.getBoundingClientRect();
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;
        let dropX = Math.round((e.clientX - rect.left) * scaleX);
        let dropY = Math.round((e.clientY - rect.top) * scaleY);

        if (state.gridSnap) {
            dropX = Math.round(dropX / state.gridSize) * state.gridSize;
            dropY = Math.round(dropY / state.gridSize) * state.gridSize;
        }

        saveHistoryState();
        const newWidget = createDefaultWidget(widgetType, dropX, dropY);
        state.widgets.push(newWidget);
        state.selectedWidgetId = newWidget.id;

        renderLayersList();
        renderInspector();
        renderCanvas();
    });

    function createDefaultWidget(type, x, y) {
        const id = `${type}_${Date.now().toString().slice(-4)}`;
        const t = THEMES[state.currentThemeKey] || THEMES['civic-eco'];

        switch (type) {
            case 'nav-turn-arrow':
                return { id, type, name: 'Turn Arrow', x, y, w: 80, h: 80, color: t.navArrow || t.primary, bgColor: t.bgCard };
            case 'nav-maneuver-banner':
                return { id, type, name: 'Next Road Banner', x, y, w: 260, h: 42, color: t.secondary, bgColor: t.bgCard };
            case 'nav-eta-badge':
                return { id, type, name: 'ETA Badge', x, y, w: 240, h: 36, color: t.primary };
            case 'nav-lane-assist':
                return { id, type, name: 'Lane Guidance', x, y, w: 160, h: 24 };
            case 'nav-camera-alert':
                return { id, type, name: 'Speed Camera', x, y, w: 180, h: 30, color: t.alert || '#ffcc00' };
            case 'nav-hazard-alert':
                return { id, type, name: 'Hazard Alert', x, y, w: 180, h: 56, color: '#ff3366' };
            case 'shift-lights':
                return { id, type, name: 'Shift Light Arch', x, y, radius: 90, ledRadius: 4, ledCount: 12, startAngle: 210, endAngle: 330, max: 7000, color: t.primary, trackColor: t.track, binding: 'rpm' };
            case 'rev-strip':
                return { id, type, name: 'F1 Rev Strip', x, y, w: 220, h: 14, ledCount: 16, max: 8000, color: t.primary, trackColor: t.track, binding: 'rpm' };
            case 'smooth-arc':
                return { id, type, name: 'Smooth Arc', x, y, radius: 80, thickness: 10, startAngle: 135, endAngle: 405, color: t.primary, trackColor: t.track, min: 0, max: 100, binding: 'throttle' };
            case 'boost-gauge':
                return { id, type, name: 'Turbo Boost Gauge', x, y, radius: 60, min: -1.0, max: 2.5, color: '#ff6600', trackColor: t.track, binding: 'boost_bar', value: 1.2 };
            case 'digital-value':
                return { id, type, name: 'Digital Speed', x, y, fontSize: 48, fontFamily: 'Segment7, "DSEG7-Classic", monospace', color: t.text, binding: 'speed_kmh', showGhost: true, ghostOpacity: 0.08, ghostDigits: '888', unit: ' km/h' };
            case 'lap-timer':
                return { id, type, name: 'Lap Timer', x, y, w: 200, h: 60, color: t.primary };
            case 'temp-stack':
                return { id, type, name: 'Dual Temp Stack', x, y, w: 90, h: 120, bgColor: t.track };
            case 'battery-meter':
                return { id, type, name: 'Battery Meter', x, y, w: 120, h: 40, color: t.secondary, bgColor: t.bgCard, binding: 'battery_v' };
            case 'compass-dial':
                return { id, type, name: 'Heading Compass', x, y, radius: 40, color: t.secondary };
            case 'speed-sign':
                return { id, type, name: 'Speed Limit Sign', x, y, text: '90' };
            case 'dial-needle':
                return { id, type, name: 'Gauge Needle', x, y, radius: 70, startAngle: 135, endAngle: 405, color: '#ff3366', min: 0, max: 100, binding: 'throttle' };
            case 'gauge-ticks':
                return { id, type, name: 'Dial Ticks', x, y, radius: 85, tickLen: 8, tickCount: 9, startAngle: 135, endAngle: 405, color: '#64748b' };
            case 'bar-slider':
                return { id, type, name: 'Bar Meter', x, y, w: 140, h: 16, borderRadius: 4, color: t.primary, bgColor: t.track, min: 0, max: 100, binding: 'throttle' };
            case 'history-chart':
                return { id, type, name: 'CAN Waveform', x, y, w: 280, h: 120, color: t.primary };
            case 'g-force-meter':
                return { id, type, name: 'G-Force Radar', x, y, radius: 60, color: t.primary };
            case 'tpms-map':
                return { id, type, name: 'TPMS 4-Tire', x, y, w: 180, h: 160 };
            case 'annunciator-icon':
                return { id, type, name: 'Warning Lamp', x, y, color: '#ffcc00' };
            case 'status-badge':
                return { id, type, name: 'Gear Pill', x, y, text: '4', color: t.secondary, binding: 'gear' };
            case 'text-label':
                return { id, type, name: 'Text Label', x, y, text: 'TELEMETRY', fontSize: 14, fontFamily: 'Rajdhani, sans-serif', color: t.textMuted };
            case 'card-box':
                return { id, type, name: 'Glass Card', x, y, w: 140, h: 90, borderRadius: 10, color: t.primary, bgColor: t.bgCard };
            default:
                return { id, type, name: type, x, y };
        }
    }

    // =========================================================================
    // 13. TELEMETRY & GPS SIMULATOR
    // =========================================================================
    const simBindings = [
        { id: 'simRpm', valId: 'valRpm', key: 'rpm' },
        { id: 'simSpeed', valId: 'valSpeed', key: 'speed_kmh' },
        { id: 'simGear', valId: 'valGear', key: 'gear' },
        { id: 'simThrottle', valId: 'valThrottle', key: 'throttle' },
        { id: 'simBrake', valId: 'valBrake', key: 'brake' },
        { id: 'simNavDist', valId: 'valNavDist', key: 'nav_dist_m' },
        { id: 'simBoost', valId: 'valBoost', key: 'boost_bar', parser: v => (v / 10).toFixed(1) },
        { id: 'simWater', valId: 'valWater', key: 'water_temp' },
        { id: 'simBatt', valId: 'valBatt', key: 'battery_v', parser: v => (v / 10).toFixed(1) },
        { id: 'simLatG', valId: 'valLatG', key: 'lat_g', parser: v => (v / 100).toFixed(2) }
    ];

    simBindings.forEach(b => {
        const slider = document.getElementById(b.id);
        const valLabel = document.getElementById(b.valId);
        if (!slider) return;

        slider.addEventListener('input', (e) => {
            const rawVal = parseFloat(e.target.value);
            const formatted = b.parser ? b.parser(rawVal) : rawVal;
            state.telemetry[b.key] = formatted;
            if (valLabel) valLabel.textContent = formatted;
            renderCanvas();
        });
    });

    document.getElementById('simNavManeuver')?.addEventListener('change', (e) => {
        state.telemetry.nav_maneuver = e.target.value;
        renderCanvas();
    });

    const sweepBtn = document.getElementById('simPlayPauseBtn');
    if (sweepBtn) {
        sweepBtn.addEventListener('click', () => {
            state.simSweepActive = !state.simSweepActive;
            if (state.simSweepActive) {
                sweepBtn.textContent = '⏸ Pause Sweep';
                sweepBtn.classList.remove('btn-accent');
                sweepBtn.classList.add('btn-secondary');
                document.getElementById('simModeBadge').textContent = 'SWEEP (50Hz)';
                document.getElementById('simModeBadge').style.color = '#00ff66';

                state.simSweepTimer = setInterval(() => {
                    state.sweepStep += 0.04;
                    const phase = state.sweepStep;

                    state.telemetry.rpm = Math.round(3200 + Math.sin(phase) * 2900 + Math.sin(phase * 2.5) * 600);
                    state.telemetry.speed_kmh = Math.round(95 + Math.sin(phase * 0.7) * 45);
                    state.telemetry.throttle = Math.round(50 + Math.sin(phase * 1.2) * 45);
                    state.telemetry.brake = Math.round(Math.max(0, -Math.sin(phase * 1.2) * 80));
                    state.telemetry.boost_bar = parseFloat((Math.sin(phase * 1.2) * 1.4).toFixed(1));
                    state.telemetry.water_temp = Math.round(92 + Math.sin(phase * 0.1) * 6);
                    state.telemetry.lat_g = parseFloat((Math.sin(phase * 0.9) * 1.1).toFixed(2));
                    state.telemetry.heading_deg = (state.telemetry.heading_deg + 1) % 360;
                    state.telemetry.nav_dist_m = Math.max(10, Math.round(600 - ((phase * 80) % 600)));
                    state.telemetry.gear = 1 + (Math.floor(phase * 0.6) % 6);

                    document.getElementById('valRpm').textContent = state.telemetry.rpm;
                    document.getElementById('valSpeed').textContent = state.telemetry.speed_kmh;
                    document.getElementById('valGear').textContent = state.telemetry.gear;
                    document.getElementById('valThrottle').textContent = state.telemetry.throttle;
                    document.getElementById('valBrake').textContent = state.telemetry.brake;
                    document.getElementById('valNavDist').textContent = state.telemetry.nav_dist_m;
                    document.getElementById('valBoost').textContent = state.telemetry.boost_bar;
                    document.getElementById('valWater').textContent = state.telemetry.water_temp;
                    document.getElementById('valLatG').textContent = state.telemetry.lat_g;

                    state.chartHistory.rpm.shift();
                    state.chartHistory.rpm.push(state.telemetry.rpm);

                    renderCanvas();
                }, 20); // 50 FPS
            } else {
                sweepBtn.textContent = '▶ Sweep Test';
                sweepBtn.classList.add('btn-accent');
                sweepBtn.classList.remove('btn-secondary');
                document.getElementById('simModeBadge').textContent = 'MANUAL (50Hz)';
                document.getElementById('simModeBadge').style.color = '#ffcc00';
                clearInterval(state.simSweepTimer);
            }
        });
    }

    // =========================================================================
    // 14. PRESETS DROPDOWN
    // =========================================================================
    if (presetSelect) {
        presetSelect.addEventListener('change', (e) => {
            const key = e.target.value;
            if (DEVICE_PRESETS[key]) {
                state.currentPresetKey = key;
                state.preset = DEVICE_PRESETS[key];
                updateBezelFrameDimensions();
                if (key === 'amoled-454') loadPresetCivicAmoled();
                else if (key === 'xiao-round-240') loadPresetXiaoProduction();
                else if (key === 'xiao-dual-round') loadPresetDualCockpit();
                else if (key === 'esp32-s3-lcd-314') loadPresetRaceLab314();
                renderLayersList();
                renderCanvas();
            }
        });
    }

    if (presetsMenuBtn && presetsDropdown) {
        presetsMenuBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            presetsDropdown.classList.toggle('hidden');
        });

        document.addEventListener('click', () => {
            presetsDropdown.classList.add('hidden');
        });

        document.querySelectorAll('.dropdown-item').forEach(item => {
            item.addEventListener('click', () => {
                const layoutKey = item.getAttribute('data-preset-layout');
                if (layoutKey === 'civic-amoled') {
                    presetSelect.value = 'amoled-454';
                    state.currentPresetKey = 'amoled-454';
                    state.preset = DEVICE_PRESETS['amoled-454'];
                    updateBezelFrameDimensions();
                    loadPresetCivicAmoled();
                } else if (layoutKey === 'nav-amoled') {
                    presetSelect.value = 'amoled-454';
                    state.currentPresetKey = 'amoled-454';
                    state.preset = DEVICE_PRESETS['amoled-454'];
                    updateBezelFrameDimensions();
                    loadPresetNavAmoled();
                } else if (layoutKey === 'nav-highway-314') {
                    presetSelect.value = 'esp32-s3-lcd-314';
                    state.currentPresetKey = 'esp32-s3-lcd-314';
                    state.preset = DEVICE_PRESETS['esp32-s3-lcd-314'];
                    updateBezelFrameDimensions();
                    loadPresetNavHighway314();
                } else if (layoutKey === 'xiao-production') {
                    presetSelect.value = 'xiao-round-240';
                    state.currentPresetKey = 'xiao-round-240';
                    state.preset = DEVICE_PRESETS['xiao-round-240'];
                    updateBezelFrameDimensions();
                    loadPresetXiaoProduction();
                } else if (layoutKey === 'xiao-eez') {
                    presetSelect.value = 'xiao-round-240';
                    state.currentPresetKey = 'xiao-round-240';
                    state.preset = DEVICE_PRESETS['xiao-round-240'];
                    updateBezelFrameDimensions();
                    loadPresetXiaoEez();
                } else if (layoutKey === 'racelab-314') {
                    presetSelect.value = 'esp32-s3-lcd-314';
                    state.currentPresetKey = 'esp32-s3-lcd-314';
                    state.preset = DEVICE_PRESETS['esp32-s3-lcd-314'];
                    updateBezelFrameDimensions();
                    loadPresetRaceLab314();
                } else if (layoutKey === 'dual-cockpit') {
                    presetSelect.value = 'xiao-dual-round';
                    state.currentPresetKey = 'xiao-dual-round';
                    state.preset = DEVICE_PRESETS['xiao-dual-round'];
                    updateBezelFrameDimensions();
                    loadPresetDualCockpit();
                } else if (layoutKey === 'cyberpunk-hud') {
                    loadPresetCyberpunk();
                } else if (layoutKey === 'blank') {
                    saveHistoryState();
                    state.widgets = [];
                }
                renderLayersList();
                renderCanvas();
            });
        });
    }

    document.getElementById('gridToggleBtn')?.addEventListener('click', (e) => {
        state.gridSnap = !state.gridSnap;
        e.target.textContent = state.gridSnap ? 'Grid: 8px' : 'Grid: OFF';
        e.target.classList.toggle('active', state.gridSnap);
        renderCanvas();
    });

    document.getElementById('bezelToggleBtn')?.addEventListener('click', (e) => {
        state.showBezel = !state.showBezel;
        e.target.textContent = state.showBezel ? 'Bezel: ON' : 'Bezel: OFF';
        e.target.classList.toggle('active', state.showBezel);
        updateBezelFrameDimensions();
    });

    // =========================================================================
    // 15. MODAL CODE EXPORT (LVGL 8.4 / TFT_eSPI / JSON)
    // =========================================================================
    const exportBtn = document.getElementById('exportCodeBtn');
    const tabLvglBtn = document.getElementById('tabLvglBtn');
    const tabCppBtn = document.getElementById('tabCppBtn');
    const tabJsonBtn = document.getElementById('tabJsonBtn');
    const snippetDesc = document.getElementById('modalSnippetDesc');

    function updateModalSnippet() {
        if (!window.CodeGenerator) return;
        if (state.activeExportTab === 'lvgl') {
            snippetDesc.textContent = 'High-performance zero-allocation LVGL 8.4 C code optimized for 50Hz smooth updates without screen tearing:';
            codeSnippet.textContent = window.CodeGenerator.generateLvglC(state.preset, state.widgets);
        } else if (state.activeExportTab === 'cpp') {
            snippetDesc.textContent = 'Double-buffered C++ TFT_eSPI rendering loop for your ESP32 display node:';
            codeSnippet.textContent = window.CodeGenerator.generateCpp(state.preset, state.widgets);
        } else {
            snippetDesc.textContent = 'Complete JSON Schema layout representation for espDash UI Studio:';
            codeSnippet.textContent = window.CodeGenerator.generateJson(state.preset, state.widgets);
        }
    }

    exportBtn?.addEventListener('click', () => {
        updateModalSnippet();
        codeModal.classList.add('active');
    });

    tabLvglBtn?.addEventListener('click', () => {
        state.activeExportTab = 'lvgl';
        tabLvglBtn.classList.add('active');
        tabCppBtn?.classList.remove('active');
        tabJsonBtn?.classList.remove('active');
        updateModalSnippet();
    });

    tabCppBtn?.addEventListener('click', () => {
        state.activeExportTab = 'cpp';
        tabCppBtn.classList.add('active');
        tabLvglBtn?.classList.remove('active');
        tabJsonBtn?.classList.remove('active');
        updateModalSnippet();
    });

    tabJsonBtn?.addEventListener('click', () => {
        state.activeExportTab = 'json';
        tabJsonBtn.classList.add('active');
        tabCppBtn?.classList.remove('active');
        tabLvglBtn?.classList.remove('active');
        updateModalSnippet();
    });

    document.getElementById('closeModalBtn')?.addEventListener('click', () => codeModal.classList.remove('active'));
    document.getElementById('closeModalFooterBtn')?.addEventListener('click', () => codeModal.classList.remove('active'));

    document.getElementById('copyCodeBtn')?.addEventListener('click', () => {
        navigator.clipboard.writeText(codeSnippet.textContent).then(() => {
            const btn = document.getElementById('copyCodeBtn');
            btn.textContent = '✅ Copied to Clipboard!';
            setTimeout(() => { btn.textContent = '📋 Copy Code to Clipboard'; }, 2000);
        });
    });

    // Save & Load JSON
    document.getElementById('exportJsonBtn')?.addEventListener('click', () => {
        if (!window.CodeGenerator) return;
        const jsonStr = window.CodeGenerator.generateJson(state.preset, state.widgets);
        const blob = new Blob([jsonStr], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `espdash_layout_${state.preset.name.toLowerCase().replace(/[^a-z0-9]/g, '_')}.json`;
        a.click();
        URL.revokeObjectURL(url);
    });

    document.getElementById('importJsonBtn')?.addEventListener('click', () => fileInput.click());

    fileInput?.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            const reader = new FileReader();
            reader.onload = (evt) => {
                try {
                    const parsed = JSON.parse(evt.target.result);
                    if (parsed.widgets) {
                        saveHistoryState();
                        state.widgets = parsed.widgets;
                        if (parsed.preset) {
                            state.preset = parsed.preset;
                            updateBezelFrameDimensions();
                        }
                        renderLayersList();
                        renderInspector();
                        renderCanvas();
                        alert('Layout imported successfully!');
                    }
                } catch (err) {
                    alert(`Failed to load layout JSON: ${err.message}`);
                }
            };
            reader.readAsText(e.target.files[0]);
        }
    });

    // =========================================================================
    // 16. INITIAL STARTUP
    // =========================================================================
    updateBezelFrameDimensions();
    loadPresetCivicAmoled();
    renderLayersList();
    renderCanvas();
});
