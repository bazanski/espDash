#pragma once
#include <lvgl.h>
#include <stdint.h>

// Primary-flight-display style page, mapped to a car:
//   centre   attitude ball      roll / pitch from the IMU (+ skid = lateral g)
//   left     speed tape         km/h from the gateway, with a trend vector
//   right    RPM tape           with amber/red zones, plus a longitudinal-G scale
//   bottom   heading arc        IMU heading (compass if a magnetometer is fitted)
//   top      status bar         gear / drive mode / gateway link
// Invalid inputs get a red flag over the instrument, as on a real PFD.
struct PfdData {
    bool  link;            // fresh gateway telemetry
    bool  imu_ok;          // IMU present
    float speed_kmh;
    float rpm;
    uint8_t gear_sel;      // 0=P 1=R 2=N 3=D 4=S
    uint8_t gear_num;      // engaged gear, 0 = none
    float coolant_c;
    float batt_v;
    int   fuel_pct;
    bool  fuel_valid;
    bool  low_fuel, econ, sport;
    float roll;            // deg, positive = right side down
    float pitch_up;        // deg, positive = nose up
    float heading;         // deg 0..360
    bool  heading_mag;     // true = magnetic compass, false = relative to ZERO
    float lin_x;           // g, forward positive (gravity removed)
    float lin_y;           // g, left positive
    const char *link_text; // top bar, right cell
    uint32_t    link_color;
};

// on_next is wired to the NEXT button in the top-right corner.
lv_obj_t *pfd_screen_build(lv_event_cb_t on_next);
void pfd_screen_update(const PfdData &d);

// Simulated drive for bench testing (no car, no IMU needed).
void pfd_demo_fill(PfdData &d, uint32_t ms);
