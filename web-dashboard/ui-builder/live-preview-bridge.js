/**
 * espDash UI Builder - Live Preview Bridge
 * Streams real-time widget layout & mock telemetry data over WebSockets / WebSerial to physical ESP displays.
 */

window.LivePreviewBridge = {
    ws: null,
    serialPort: null,
    writer: null,
    connected: false,
    connectionType: null,

    onStatusChange: null,

    init: function(callbacks) {
        if (callbacks && callbacks.onStatusChange) {
            this.onStatusChange = callbacks.onStatusChange;
        }
    },

    /**
     * Connect to target ESP32 board over WebSocket
     */
    connectWebSocket: function(host) {
        this.disconnect();
        let url = host;
        if (!url.startsWith('ws://') && !url.startsWith('wss://')) {
            url = 'ws://' + host + ':8888/preview';
        }

        try {
            this.ws = new WebSocket(url);
            this.ws.onopen = () => {
                this.connected = true;
                this.connectionType = 'WebSocket';
                if (this.onStatusChange) this.onStatusChange(true, `Connected via WS (${host})`);
                this.sendFullLayout();
            };

            this.ws.onclose = () => {
                this.connected = false;
                if (this.onStatusChange) this.onStatusChange(false, 'Disconnected');
            };

            this.ws.onerror = (err) => {
                console.error('WebSocket preview error:', err);
                if (this.onStatusChange) this.onStatusChange(false, 'Connection Failed');
            };
        } catch (e) {
            console.error('WebSocket init failed:', e);
            if (this.onStatusChange) this.onStatusChange(false, 'WS Error');
        }
    },

    /**
     * Connect to target ESP32 board over USB WebSerial
     */
    connectWebSerial: async function() {
        this.disconnect();
        if (!('serial' in navigator)) {
            alert('WebSerial API is not supported in this browser. Please use Chrome or Edge.');
            return;
        }

        try {
            this.serialPort = await navigator.serial.requestPort();
            await this.serialPort.open({ baudRate: 115200 });
            this.writer = this.serialPort.writable.getWriter();
            this.connected = true;
            this.connectionType = 'WebSerial';
            if (this.onStatusChange) this.onStatusChange(true, 'Connected via USB WebSerial');
            this.sendFullLayout();
        } catch (err) {
            console.error('WebSerial connection error:', err);
            if (this.onStatusChange) this.onStatusChange(false, 'WebSerial Failed');
        }
    },

    disconnect: function() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        if (this.writer) {
            this.writer.releaseLock();
            this.writer = null;
        }
        if (this.serialPort) {
            this.serialPort.close();
            this.serialPort = null;
        }
        this.connected = false;
        if (this.onStatusChange) this.onStatusChange(false, 'Disconnected');
    },

    /**
     * Send layout JSON to target display hardware
     */
    sendFullLayout: function() {
        if (!this.connected) return;
        const payload = {
            cmd: 'UPDATE_LAYOUT',
            preset: window.UIBuilder ? window.UIBuilder.currentPreset : 'esp32-s3-lcd-314',
            widgets: window.UIBuilder ? window.UIBuilder.widgets : []
        };
        this.transmit(JSON.stringify(payload));
    },

    /**
     * Send live telemetry tick to physical hardware
     */
    sendTelemetryTick: function(telemetry) {
        if (!this.connected) return;
        const payload = {
            cmd: 'TELEMETRY_TICK',
            data: telemetry
        };
        this.transmit(JSON.stringify(payload));
    },

    transmit: async function(strData) {
        const msg = strData + '\n';
        if (this.connectionType === 'WebSocket' && this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(msg);
        } else if (this.connectionType === 'WebSerial' && this.writer) {
            const encoder = new TextEncoder();
            await this.writer.write(encoder.encode(msg));
        }
    }
};
