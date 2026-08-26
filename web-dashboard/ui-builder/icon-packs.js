/**
 * espDash UI Studio - Automotive & Navigation Icon Packs (Vector & Animated)
 * Provides Theme-Tailored SVG & Canvas Icons for:
 * 1. Turn-by-Turn Navigation & Waze/GMaps Maneuvers
 * 2. Vehicle Annunciator Warning Lamps (ISO 2575 Standard)
 * 3. Radar, Police & Speed Camera Alerts
 */

window.IconPacks = {
    // 1. Navigation Maneuver Icons (SVG Paths)
    navigation: {
        'turn-right': '<path d="M6 24V14C6 9.58 9.58 6 14 6H24M24 6L18 0M24 6L18 12" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'turn-left': '<path d="M26 24V14C26 9.58 22.42 6 18 6H8M8 6L14 0M8 6L14 12" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'slight-right': '<path d="M8 24L20 8M20 8H10M20 8V18" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'slight-left': '<path d="M24 24L12 8M12 8H22M12 8V18" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'straight': '<path d="M16 26V6M16 6L9 13M16 6L23 13" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'u-turn': '<path d="M22 26V14C22 8.48 17.52 4 12 4C6.48 4 2 8.48 2 14V26M2 26L-3 20M2 26L7 20" stroke="currentColor" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/>',
        'roundabout': '<path d="M16 4A12 12 0 1 1 4 16M16 4L12 0M16 4L12 8" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" fill="none"/>'
    },

    // 2. Vehicle Annunciator Icons
    annunciators: {
        'check-engine': '<path d="M4 10H6V7H12V4H16V7H22V10H24V20H20V24H16V20H10V24H6V20H4V10Z" fill="currentColor"/>',
        'oil-pressure': '<path d="M4 14H18C19.1 14 20 13.1 20 12V10L24 6V14C24 16.2 22.2 18 20 18H10L6 22H2L4 18V14Z" fill="currentColor"/><circle cx="23" cy="22" r="2" fill="currentColor"/>',
        'battery': '<rect x="4" y="8" width="24" height="16" rx="2" stroke="currentColor" stroke-width="2.5" fill="none"/><path d="M8 5V8M24 5V8M9 16H15M20 16H24M22 14V18" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>',
        'abs': '<circle cx="16" cy="16" r="13" stroke="currentColor" stroke-width="2.5" stroke-dasharray="6 3" fill="none"/><text x="16" y="20" font-family="sans-serif" font-size="10" font-weight="bold" text-anchor="middle" fill="currentColor">ABS</text>',
        'brake-fluid': '<circle cx="16" cy="16" r="10" stroke="currentColor" stroke-width="2" fill="none"/><path d="M4 10C2.5 13.5 2.5 18.5 4 22M28 10C29.5 13.5 29.5 18.5 28 22M16 11V17M16 20V21" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"/>',
        'tpms-tire': '<path d="M6 10C4 14 4 18 6 22C10 24 22 24 26 22C28 18 28 14 26 10M16 10V16M16 19V20" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" fill="none"/>',
        'high-beam': '<path d="M6 10C10 10 14 12 14 16C14 20 10 22 6 22ZM18 10H26M18 14H26M18 18H26M18 22H26" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" fill="currentColor"/>',
        'coolant-temp': '<path d="M14 6V18.2C12.8 18.9 12 20.3 12 22C12 24.2 13.8 26 16 26C18.2 26 20 24.2 20 22C20 20.3 19.2 18.9 18 18.2V6C18 4.9 17.1 4 16 4C14.9 4 14 4.9 14 6Z" fill="currentColor"/><path d="M6 22H10M6 16H10M6 10H10" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>'
    },

    // 3. Waze / Speed Camera Hazard Icons
    hazards: {
        'speed-camera': '<rect x="6" y="8" width="20" height="14" rx="3" stroke="currentColor" stroke-width="2" fill="none"/><circle cx="16" cy="15" r="4" stroke="currentColor" stroke-width="2" fill="none"/><circle cx="21" cy="11" r="1.5" fill="currentColor"/>',
        'police-radar': '<path d="M16 4L28 24H4L16 4Z" stroke="currentColor" stroke-width="2.5" stroke-linejoin="round" fill="rgba(255,204,0,0.2)"/><path d="M16 11V17M16 20V21" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"/>',
        'accident': '<path d="M6 6L26 26M26 6L6 26" stroke="currentColor" stroke-width="3" stroke-linecap="round"/>',
        'traffic-slow': '<rect x="4" y="6" width="24" height="6" rx="2" fill="currentColor"/><rect x="8" y="14" width="20" height="6" rx="2" fill="currentColor"/><rect x="4" y="22" width="16" height="6" rx="2" fill="currentColor"/>'
    },

    /**
     * Render SVG String for any icon with custom size and color
     */
    getSvg: function(category, iconName, color = '#00f0ff', size = 32) {
        const cat = this[category];
        if (!cat || !cat[iconName]) return '';
        return `<svg width="${size}" height="${size}" viewBox="0 0 32 32" style="color: ${color}; overflow: visible;">${cat[iconName]}</svg>`;
    }
};
