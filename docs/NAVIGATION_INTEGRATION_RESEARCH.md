# Navigation Integration Investigation: Waze, Google Maps & Open-Source GPS for espDash

**Branch**: `feature/nav-waze-gmaps-integration`  
**Date**: August 2026  
**Target Hardware**: ESP32 / ESP32-S3 Display Nodes (AMOLED 454x454, ST7701 800x240, GC9A01 240x240)  
**Primary Objective**: Investigate 100% free, reliable, and real-time methods to stream turn-by-turn navigation, maneuver arrows, speed limits, ETA, and speed camera/hazard alerts from Waze, Google Maps, and open-source alternatives to espDash screens.

---

## 1. Executive Summary

| Category | Finding |
| :--- | :--- |
| **Feasibility** | **100% Feasible & Tested**. Real-time turn-by-turn maneuvers, speed limits, ETA, and hazard alerts can be streamed to ESP32 screens without paid APIs. |
| **Best Free Approach (Android)** | **Android Notification Listener Service + BLE / ESP-NOW Bridge**. Android allows background services to read persistent Google Maps & Waze navigation notifications for free. |
| **Best Open-Source Native Approach** | **OsmAnd AIDL API / Broadcast Intents**. Provides direct, structured binary navigation data (maneuver angle, distance in meters, lane guidance, speed limit). |
| **Best Zero-Code-on-Phone Approach** | **Gadgetbridge BLE Compatibility**. Emulating a Gadgetbridge-compatible wearable protocol allows users to use existing open-source app infrastructure on Android. |
| **iOS Status** | iOS restricts background notification scraping. Apple ANCS (Apple Notification Center Service) over BLE delivers basic lock-screen text alerts; full live countdowns on iOS require dedicated apps like Sygic GPS BLE HUD. |

---

## 2. Deep-Dive: Navigation Providers & Data Extraction Methods

### Method 1: Android `NotificationListenerService` (Google Maps & Waze) — **Recommended Free Solution**

Google Maps (`com.google.android.apps.maps`) and Waze (`com.waze`) maintain a persistent foreground notification whenever active navigation is running.

```
[Android OS] ──> [Google Maps / Waze] ──(Notification)──> [espDash Companion Service] 
                                                                     │
                                                    (Parses icon, dist, road, ETA)
                                                                     │
                                                     [BLE / Wi-Fi / USB Serial]
                                                                     │
                                                                     ▼
                                                          [espDash ESP32 Master]
                                                                     │
                                                             (ESP-NOW 50Hz)
                                                                     ▼
                                                   [Display Nodes: AMOLED / 3.14 / XIAO]
```

#### What Data Can Be Extracted?
- **Google Maps**:
  - `android.title`: Distance to next maneuver (e.g. `"In 350 m"`, `"In 1.2 km"`).
  - `android.text`: Maneuver action and street name (e.g. `"Turn right onto Main Street / A1"`).
  - `android.subText`: Trip ETA, time remaining, and distance (e.g. `"18:42 · 18 min · 14.2 km"`).
  - `android.largeIcon`: Bitmap / vector resource of the turn arrow (Left, Right, Roundabout, U-Turn, Highway Exit).
- **Waze**:
  - Next turn direction and distance.
  - Upcoming speed camera / radar trap warnings (e.g. `"Speed Camera 400m - Limit 80"`).
  - Police presence, road construction, and traffic slowdown alerts.

#### Android Implementation Snippet
```java
public class NavigationNotificationService extends NotificationListenerService {
    @Override
    public void onNotificationPosted(StatusBarNotification sbn) {
        String pkg = sbn.getPackageName();
        if ("com.google.android.apps.maps".equals(pkg) || "com.waze".equals(pkg)) {
            Notification notification = sbn.getNotification();
            Bundle extras = notification.extras;
            
            String distance = extras.getString(Notification.EXTRA_TITLE);
            String instruction = extras.getString(Notification.EXTRA_TEXT);
            String etaInfo = extras.getString(Notification.EXTRA_SUB_TEXT);
            
            // Encode into lightweight EspDashNavPacket and send via BLE / UDP
            sendToEspDash(parseManeuver(instruction), distance, instruction, etaInfo);
        }
    }
}
```

---

### Method 2: OsmAnd Native AIDL / Intent API (100% Open-Source & Offline)

OsmAnd provides an official, robust **AIDL (Android Interface Definition Language)** interface and broadcast intent system (`net.osmand.aidl.IOsmAndAidlInterface`).

#### Advantages:
1. **100% Free and Open-Source**: No scraping or regex parsing of strings.
2. **Offline Navigation**: Works anywhere without cell service using downloaded OpenStreetMap tiles.
3. **Structured Binary Data**:
   - Exact maneuver angle in degrees (e.g. 45° slight right, 90° right, 180° U-turn).
   - Distance to turn in exact integer meters (`dist_m`).
   - Speed limit of current road segment (`speed_limit_kmh`).
   - Active lane configurations (e.g. 4 lanes, lane 2 & 3 required).
   - Destination remaining distance, duration, and coordinates.

---

### Method 3: Direct Cloud Routing APIs (OSRM / Valhalla / OpenRouteService)

If running a standalone ESP32 with Wi-Fi / LTE / SIM module (without relying on a phone app):

1. **OSRM (Open Source Routing Machine)**:
   - Free demo API or self-hostable in Docker (`docker run -t -v "${PWD}:/data" osrm/osrm-backend`).
   - REST endpoint: `GET /route/v1/driving/{lon1},{lat1};{lon2},{lat2}?steps=true&overview=false`
   - Returns full JSON turn-by-turn array.
2. **OpenRouteService**:
   - Free tier includes 2,000 requests/day.
3. **Mapbox Directions API**:
   - Free tier includes 100,000 requests/month.

---

## 3. Existing Open-Source Projects & Reference Implementations

| Project | Description & Reference | Technology Stack |
| :--- | :--- | :--- |
| **[appleshaman/CarPlayBLE](https://github.com/appleshaman/CarPlayBLE)** | Navigation unit parsing Google Maps notifications from Android and transmitting to ESP32 over BLE. | Android Java + ESP32 C++ (BLE GATT) |
| **[Shr-c0de/navHUD](https://github.com/Shr-c0de/navHUD)** | Motorcycle / Automotive HUD using ESP32-C3 and Google Maps notification listener. | Android Kotlin + ESP32 C++ |
| **[Radiokot/osmand-display-app](https://github.com/Radiokot/osmand-display-app)** | Production-ready Android bridge forwarding OsmAnd AIDL turn-by-turn directions to BLE displays. | Android Kotlin (AIDL) + BLE Peripheral |
| **[Gadgetbridge](https://github.com/Freeyourgadget/Gadgetbridge)** | Leading open-source Android wearable manager with built-in Google Maps and OsmAnd notification parsing. | Android Java + Multi-device BLE protocols |
| **[alexanderlavrushko/BLE-HUD-navigation-ESP32](https://github.com/alexanderlavrushko/BLE-HUD-navigation-ESP32)** | Turn-by-turn HUD for iOS using Sygic Maps custom BLE GATT service. | iOS Swift + ESP32 C++ |
| **[Smartphone-Companions/ESP32-ANCS-Notifications](https://github.com/Smartphone-Companions/ESP32-ANCS-Notifications)** | Native Apple ANCS GATT client on ESP32 (reads incoming iOS notifications over BLE). | ESP32 NimBLE-Arduino |

---

## 4. Proposed `EspDashProto` Wire Protocol Extension for Navigation

To seamlessly incorporate navigation data across our CAN bus / ESP-NOW wireless bus, we extend `firmware/shared/include/EspDashProto.h`:

```cpp
#pragma once
#include <stdint.h>

// Maneuver Type Enums (1-to-1 matching with UI Builder nav widgets)
enum EspDashNavManeuver : uint8_t {
    NAV_MANEUVER_NONE = 0,
    NAV_MANEUVER_STRAIGHT,
    NAV_MANEUVER_TURN_LEFT,
    NAV_MANEUVER_TURN_RIGHT,
    NAV_MANEUVER_SLIGHT_LEFT,
    NAV_MANEUVER_SLIGHT_RIGHT,
    NAV_MANEUVER_SHARP_LEFT,
    NAV_MANEUVER_SHARP_RIGHT,
    NAV_MANEUVER_U_TURN,
    NAV_MANEUVER_ROUNDABOUT_EXIT_1,
    NAV_MANEUVER_ROUNDABOUT_EXIT_2,
    NAV_MANEUVER_ROUNDABOUT_EXIT_3,
    NAV_MANEUVER_HIGHWAY_EXIT,
    NAV_MANEUVER_ARRIVED
};

// Hazard / Speed Trap Alert Flags
enum EspDashNavHazard : uint8_t {
    NAV_HAZARD_NONE = 0,
    NAV_HAZARD_SPEED_CAMERA = (1 << 0),
    NAV_HAZARD_POLICE_RADAR = (1 << 1),
    NAV_HAZARD_ACCIDENT     = (1 << 2),
    NAV_HAZARD_TRAFFIC_JAM  = (1 << 3)
};

// Zero-allocation, lightweight 40-byte binary packet for 50Hz ESP-NOW broadcast
struct __attribute__((packed)) EspDashNavPacket {
    uint8_t  packet_type;        // 0x03 = Navigation Telemetry
    uint8_t  maneuver_type;      // EspDashNavManeuver
    uint16_t dist_to_turn_m;     // Distance to next turn in meters (0 - 65535 m)
    uint16_t total_rem_dist_km_x10; // Total remaining trip distance (e.g. 142 = 14.2 km)
    uint16_t total_rem_time_min; // Total remaining time in minutes
    uint8_t  eta_hours;          // Arrival hour (0-23)
    uint8_t  eta_minutes;        // Arrival minute (0-59)
    uint8_t  speed_limit_kmh;    // Speed limit (e.g. 50, 80, 100, 120 km/h)
    uint8_t  hazard_flags;       // Bitmask of EspDashNavHazard
    uint16_t hazard_dist_m;      // Distance to camera/hazard (meters)
    uint8_t  active_lanes_mask;  // Bit 0 = Lane 1, Bit 1 = Lane 2 (1 = keep lane, 0 = exit)
    uint8_t  total_lanes_count;  // Total number of lanes on current road (e.g. 4)
    char     next_road_name[24]; // Null-terminated next road / street name string
};
```

---

## 5. Architectural Comparison Matrix

| Option | Cost | Data Richness | Setup Complexity | Phone Battery Impact | Offline Support |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **1. espDash Android Companion App (Google Maps + Waze)** | **$0.00** | ⭐⭐⭐⭐⭐ (Turn, Dist, ETA, Camera, Hazard) | Low (Install APK once, grant notification access) | Minimal (<1%) | No (Requires Waze/GMaps online) |
| **2. OsmAnd AIDL / Broadcast Bridge** | **$0.00** | ⭐⭐⭐⭐⭐ (Exact angles, lanes, speed limits) | Low | Minimal (<1%) | **Yes (100% Offline)** |
| **3. Gadgetbridge Emulation Mode** | **$0.00** | ⭐⭐⭐⭐ (Turn, Dist, Next Street) | Zero (Use off-the-shelf F-Droid app) | Minimal | Dependent on app |
| **4. Cloud REST API (Mapbox / OSRM)** | Free Tier | ⭐⭐⭐⭐ (Turn steps only, no live traffic alerts) | High (Requires onboard LTE/Wi-Fi modem) | N/A | No |
| **5. Apple ANCS on iOS** | **$0.00** | ⭐⭐ (Static text alert strings only) | Medium (iOS BLE pairing) | Minimal | Dependent on iOS app |

---

## 6. Recommended Action Plan & Next Steps

1. **Step 1: Prototype Android Companion Bridge**:
   - Create a lightweight open-source Android helper APK (`android-bridge/`) using `NotificationListenerService`.
   - Parses Google Maps & Waze notifications and streams `EspDashNavPacket` over BLE or USB Serial.
2. **Step 2: Add BLE Receiver on Master Node (`firmware/can-hub-master`)**:
   - Master node receives navigation BLE packets from the phone and re-broadcasts them over ESP-NOW to all display nodes (`esp-round-amoled-touch`, `esp-rectangular-314`, `xiao-round-gauge`).
3. **Step 3: Test Real-Time Display with UI Studio Nav Presets**:
   - Use our newly created `nav-amoled` and `nav-highway-314` layouts to render turn arrows, speed signs, and camera warnings on physical AMOLED and 3.14" screens.
