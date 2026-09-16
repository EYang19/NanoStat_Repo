/*
 * NanoStat nPM1300 & AD5941 System Bring-up
 * NCS v2.9.2 Target
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include "ad5941_app.h"
#include "ble_service.h"
#include "oled_display.h"
#include "power_config.h"

#if ENABLE_LINEARITY_TEST
#include <SEGGER_RTT.h>
#endif

#define CDC_ACM_NODE    DT_CHOSEN(zephyr_console)
#define PMIC_NODE       DT_NODELABEL(npm1300)
#define CHARGER_NODE    DT_NODELABEL(npm1300_charger)
#define LEDS_NODE       DT_NODELABEL(npm1300_leds)

BUILD_ASSERT(DT_NODE_HAS_COMPAT(CDC_ACM_NODE, zephyr_cdc_acm_uart),
             "zephyr,console must point to a zephyr,cdc-acm-uart node");
BUILD_ASSERT(DT_NODE_EXISTS(PMIC_NODE), "Missing DT node label: npm1300");
BUILD_ASSERT(DT_NODE_EXISTS(CHARGER_NODE), "Missing DT node label: npm1300_charger");
BUILD_ASSERT(DT_NODE_EXISTS(LEDS_NODE), "Missing DT node label: npm1300_leds");

static const struct device *const cdc_dev = DEVICE_DT_GET(CDC_ACM_NODE);
static const struct device *const pmic = DEVICE_DT_GET(PMIC_NODE);
static const struct device *const charger = DEVICE_DT_GET(CHARGER_NODE);
static const struct device *const npm1300_leds = DEVICE_DT_GET(LEDS_NODE);

extern bool shutting_down;

static void usb_cdc_start(void)
{
    if (!device_is_ready(cdc_dev)) {
        return;
    }

    if (usb_enable(NULL) == 0) {
        for (int i = 0; i < 15; i++) {
            if (device_is_ready(npm1300_leds)) {
                (void)led_on(npm1300_leds, 0);
                k_sleep(K_MSEC(100));
                (void)led_off(npm1300_leds, 0);
            }
            k_sleep(K_MSEC(100));
        }
    }
}

static void print_sensor_value(const char *name, const struct sensor_value *val, const char *unit)
{
    int32_t micro = (val->val1 * 1000000) + val->val2;
    const char *sign = "";

    if (micro < 0) {
        sign = "-";
        micro = -micro;
    }

    printk("%s=%s%d.%06d %s", name, sign, micro / 1000000, micro % 1000000, unit);
}

static int32_t sensor_value_to_mv(const struct sensor_value *val)
{
    return (val->val1 * 1000) + (val->val2 / 1000);
}

static int32_t sensor_value_to_ua(const struct sensor_value *val)
{
    return (val->val1 * 1000000) + val->val2;
}

static uint8_t rough_lipo_soc_percent(int32_t vbat_mv)
{
    if (vbat_mv >= 4200) {
        return 100U;
    }
    if (vbat_mv >= 4100) {
        return 90U;
    }
    if (vbat_mv >= 4000) {
        return 80U;
    }
    if (vbat_mv >= 3900) {
        return 65U;
    }
    if (vbat_mv >= 3800) {
        return 50U;
    }
    if (vbat_mv >= 3700) {
        return 35U;
    }
    if (vbat_mv >= 3600) {
        return 20U;
    }
    if (vbat_mv >= 3500) {
        return 10U;
    }
    if (vbat_mv >= 3300) {
        return 5U;
    }

    return 0U;
}

static const char *rough_lipo_soc_note(int32_t vbat_mv)
{
    if (vbat_mv >= 4100) {
        return "high";
    }
    if (vbat_mv >= 3700) {
        return "normal";
    }
    if (vbat_mv >= 3500) {
        return "low";
    }
    if (vbat_mv >= 3300) {
        return "very low";
    }

    return "critical";
}

static int get_channel(enum sensor_channel chan, struct sensor_value *val, const char *name)
{
    int err = sensor_channel_get(charger, chan, val);

    if (err != 0) {
        printk("%s get failed: %d", name, err);
    }

    return err;
}

static void print_charger_sample(void)
{
    int err;
    struct sensor_value vbat;
    struct sensor_value ibat;
    struct sensor_value ntc_temp;
    struct sensor_value die_temp;
    struct sensor_value chg_status;
    struct sensor_value chg_error;
    struct sensor_value vbus_limit;
    struct sensor_value charge_current;
    struct sensor_value discharge_limit;
    int32_t vbat_mv = 0;
    int32_t ibat_ua = 0;
    bool have_vbat = false;
    bool have_ibat = false;

    printk("\n[BAT] nPM1300 battery/charger status snapshot\n");
    printk("[BAT] Note: IBAT sign follows Zephyr nPM1300 driver: +discharge, -charge.\n");
    err = sensor_sample_fetch(charger);
    if (err != 0) {
        printk("[BAT] sensor_sample_fetch(charger) failed: %d\n", err);
        return;
    }

    if (get_channel(SENSOR_CHAN_GAUGE_VOLTAGE, &vbat, "VBAT") == 0) {
        vbat_mv = sensor_value_to_mv(&vbat);
        have_vbat = true;
        printk("[BAT] ");
        print_sensor_value("VBAT", &vbat, "V");
        printk(" ~= %d mV, rough LiPo SOC ~= %u%% (%s, voltage-only estimate)\n",
               vbat_mv,
               rough_lipo_soc_percent(vbat_mv),
               rough_lipo_soc_note(vbat_mv));
    }
    if (get_channel(SENSOR_CHAN_GAUGE_AVG_CURRENT, &ibat, "IBAT") == 0) {
        ibat_ua = sensor_value_to_ua(&ibat);
        have_ibat = true;
        printk("[BAT] ");
        print_sensor_value("IBAT", &ibat, "A");
        printk(" ~= %d uA (%s)\n",
               ibat_ua,
               ibat_ua < 0 ? "charging" :
               (ibat_ua > 0 ? "discharging" : "near zero / idle"));
    }
    if (get_channel(SENSOR_CHAN_GAUGE_TEMP, &ntc_temp, "NTC") == 0) {
        printk("[BAT] ");
        print_sensor_value("NTC", &ntc_temp, "C");
        printk(" (10k fixed resistor currently fitted, not real cell temperature)\n");
    }
    if (get_channel(SENSOR_CHAN_DIE_TEMP, &die_temp, "DIE") == 0) {
        printk("[BAT] ");
        print_sensor_value("DIE", &die_temp, "C");
        printk("\n");
    }

    if (get_channel((enum sensor_channel)SENSOR_CHAN_NPM1300_CHARGER_STATUS,
                    &chg_status, "CHG_STATUS") == 0) {
        printk("[BAT] CHG_STATUS=0x%02x\n", chg_status.val1);
    }
    if (get_channel((enum sensor_channel)SENSOR_CHAN_NPM1300_CHARGER_ERROR,
                    &chg_error, "CHG_ERROR") == 0) {
        printk("[BAT] CHG_ERROR=0x%02x%s\n",
               chg_error.val1,
               chg_error.val1 == 0 ? " (no charger error latched)" : " (check nPM1300 charger error flags)");
    }

    err = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &vbus_limit);
    if (err == 0) {
        printk("[BAT] ");
        print_sensor_value("VBUS_LIMIT", &vbus_limit, "A");
        printk("%s\n",
               sensor_value_to_ua(&vbus_limit) == 0 ? " (USB/VBUS not detected)" :
                                                       " (USB/VBUS detected)");
    } else {
        printk("[BAT] VBUS_LIMIT get failed: %d\n", err);
    }

    if (get_channel(SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT,
                    &charge_current, "CHARGE_CURRENT_CFG") == 0) {
        printk("[BAT] ");
        print_sensor_value("CHARGE_CURRENT_CFG", &charge_current, "A");
        printk("\n");
    }
    if (get_channel(SENSOR_CHAN_GAUGE_MAX_LOAD_CURRENT,
                    &discharge_limit, "DISCHARGE_LIMIT_CFG") == 0) {
        printk("[BAT] ");
        print_sensor_value("DISCHARGE_LIMIT_CFG", &discharge_limit, "A");
        printk("\n");
    }

    if (have_vbat) {
        if (vbat_mv < 3300) {
            printk("[BAT] WARNING: VBAT is critically low. Charge before battery-only tests.\n");
        } else if (vbat_mv < 3500) {
            printk("[BAT] WARNING: VBAT is very low; boot failures or brownout-like behaviour are possible.\n");
        }
    }
    if (have_ibat && ibat_ua < 0) {
        printk("[BAT] Charging is active or current is flowing into the battery.\n");
    }

    printk("\n");
}

static bool battery_trace_active;
static uint32_t battery_trace_next_ms;

bool nanostat_battery_trace_is_active(void)
{
    return battery_trace_active;
}

static void print_battery_current_point(void)
{
    struct sensor_value ibat;
    uint32_t now_ms = k_uptime_get_32();
    int32_t ibat_ua;

    if (sensor_sample_fetch(charger) != 0 ||
        get_channel(SENSOR_CHAN_GAUGE_AVG_CURRENT, &ibat, "IBAT") != 0) {
        printk("BATCUR,t_ms=%u,IBAT_UA=NA\n", now_ms);
        return;
    }

    ibat_ua = sensor_value_to_ua(&ibat);
    printk("BATCUR,t_ms=%u,IBAT_UA=%d\n", now_ms, ibat_ua);
}

/* Keep the RTT battery shortcut responsive while BLE-triggered AFE work runs.
 * The normal main-loop poll is intentionally slow because it also drives the
 * status LEDs and display. Only the battery shortcut is consumed here; the
 * test-menu key remains handled at the normal idle-loop boundary. */
static void poll_rtt_battery_snapshot(void)
{
    uint32_t now_ms = k_uptime_get_32();

    if (SEGGER_RTT_HasKey()) {
        int key = SEGGER_RTT_GetKey();

        if (key == 'b' || key == 'B') {
            print_charger_sample();
        } else if (key == 'P') {
            battery_trace_active = !battery_trace_active;
            battery_trace_next_ms = now_ms;
            printk("[BATCUR] Current trace %s. Format: BATCUR,t_ms=...,IBAT_UA=...\n",
                   battery_trace_active ? "STARTED" : "STOPPED");
        }
    }

    if (battery_trace_active &&
        (int32_t)(now_ms - battery_trace_next_ms) >= 0) {
        print_battery_current_point();
        battery_trace_next_ms = now_ms + 100U;
    }
}

/* Keep the RTT battery trace serviced during the main-loop housekeeping
 * delays. Without this wrapper, LED/OLED delays create artificial gaps in the
 * nominal 100 ms current trace. */
static void main_loop_sleep(uint32_t duration_ms)
{
    uint32_t deadline = k_uptime_get_32() + duration_ms;

    while ((int32_t)(k_uptime_get_32() - deadline) < 0) {
        uint32_t now_ms = k_uptime_get_32();
        uint32_t remaining_ms = deadline - now_ms;

        poll_rtt_battery_snapshot();
        k_sleep(K_MSEC(MIN(remaining_ms, 10U)));
    }
}

#if ENABLE_LINEARITY_TEST
static uint32_t blank_peak_frequency_hz = 120U;
static enum ad5941_blank_peak_tia_path blank_peak_tia_path =
    AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K;
static enum ad5941_swv_profile dummy_swv_profile = AD5941_SWV_PROFILE_5MV_FULL;
static uint32_t dummy_swv_resistor_ohms = 1100000U;
static uint8_t dummy_swv_repeat_count = 10U;

static uint32_t wait_for_blank_peak_frequency_hz(void)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;
    bool leading_eol_discarded = false;

    printk("\n[BLANK-PEAK] Enter SWV frequency in Hz (1..250), then press Enter.\n");
    printk("[BLANK-PEAK] Example: 120<Enter>. Empty Enter uses 120Hz.\n");
    printk("[BLANK-PEAK] f = ");

    while (1) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '\r' || key == '\n') {
            if (digits == 0U && !leading_eol_discarded) {
                leading_eol_discarded = true;
                continue;
            }
            if (digits == 0U) {
                value = 120U;
            }
            if (value >= 1U && value <= 250U) {
                printk("\n[BLANK-PEAK] Frequency selected: %u Hz.\n", value);
                return value;
            }

            printk("\n[BLANK-PEAK] Invalid frequency %u. Enter 1..250 Hz.\n", value);
            value = 0U;
            digits = 0U;
            printk("[BLANK-PEAK] f = ");
        } else if (key == '\b' || key == 0x7f) {
            if (digits > 0U) {
                value /= 10U;
                digits--;
                printk("\b \b");
            }
        } else if (key >= '0' && key <= '9') {
            if (digits < 3U) {
                value = (value * 10U) + (uint32_t)(key - '0');
                digits++;
                printk("%c", key);
            }
        } else {
            printk("\n[BLANK-PEAK] Ignored key '%c'. Use digits then Enter.\n", key);
            printk("[BLANK-PEAK] f = ");
            if (digits > 0U) {
                printk("%u", value);
            }
        }
    }
}

static enum ad5941_blank_peak_tia_path wait_for_blank_peak_tia_path(void)
{
    printk("\n[BLANK-PEAK] Select HSTIA feedback resistor for the 1.1M dummy-resistor test.\n");
    printk("[BLANK-PEAK] Press '1' for internal 160k.\n");
    printk("[BLANK-PEAK] Press '2' for external AIN3 160k thin film.\n");
    printk("[BLANK-PEAK] Press '3' for external AIN2 330k thin film.\n");
    printk("[BLANK-PEAK] Press '4' for external AIN1 680k thin film.\n");
    printk("[BLANK-PEAK] RTIA = ");

    while (1) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '1') {
            printk("internal 160k\n");
            return AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K;
        }
        if (key == '2') {
            printk("external AIN3 160k\n");
            return AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K;
        }
        if (key == '3') {
            printk("external AIN2 330k\n");
            return AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K;
        }
        if (key == '4') {
            printk("external AIN1 680k\n");
            return AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K;
        }

        printk("\n[BLANK-PEAK] Invalid key '%c'. Press '1', '2', '3', or '4'.\n",
               key);
        printk("[BLANK-PEAK] RTIA = ");
    }
}

static uint32_t wait_for_dummy_resistor_ohms(void)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;
    bool leading_eol_discarded = false;

    printk("\n[DUMMY-SWV] Enter dummy resistor in ohms, then press Enter.\n");
    printk("[DUMMY-SWV] Example: 1100000<Enter>. Empty Enter uses 1100000 ohm.\n");
    printk("[DUMMY-SWV] R = ");

    while (1) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '\r' || key == '\n') {
            if (digits == 0U && !leading_eol_discarded) {
                leading_eol_discarded = true;
                continue;
            }
            if (digits == 0U) {
                value = 1100000U;
            }
            if (value >= 1000U && value <= 100000000U) {
                printk("\n[DUMMY-SWV] Dummy resistor selected: %u ohm.\n", value);
                return value;
            }

            printk("\n[DUMMY-SWV] Invalid resistor %u. Enter 1000..100000000 ohm.\n", value);
            value = 0U;
            digits = 0U;
            printk("[DUMMY-SWV] R = ");
        } else if (key == '\b' || key == 0x7f) {
            if (digits > 0U) {
                value /= 10U;
                digits--;
                printk("\b \b");
            }
        } else if (key >= '0' && key <= '9') {
            if (digits < 9U) {
                value = (value * 10U) + (uint32_t)(key - '0');
                digits++;
                printk("%c", key);
            }
        } else {
            printk("\n[DUMMY-SWV] Ignored key '%c'. Use digits then Enter.\n", key);
            printk("[DUMMY-SWV] R = ");
            if (digits > 0U) {
                printk("%u", value);
            }
        }
    }
}

static uint8_t wait_for_dummy_repeat_count(void)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;
    bool leading_eol_discarded = false;

    printk("\n[DUMMY-SWV] Enter repeat count (1..50), then press Enter.\n");
    printk("[DUMMY-SWV] Empty Enter uses 10 runs.\n");
    printk("[DUMMY-SWV] n = ");

    while (1) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '\r' || key == '\n') {
            if (digits == 0U && !leading_eol_discarded) {
                leading_eol_discarded = true;
                continue;
            }
            if (digits == 0U) {
                value = 10U;
            }
            if (value >= 1U && value <= 50U) {
                printk("\n[DUMMY-SWV] Repeat count selected: %u.\n", value);
                return (uint8_t)value;
            }

            printk("\n[DUMMY-SWV] Invalid repeat count %u. Enter 1..50.\n", value);
            value = 0U;
            digits = 0U;
            printk("[DUMMY-SWV] n = ");
        } else if (key == '\b' || key == 0x7f) {
            if (digits > 0U) {
                value /= 10U;
                digits--;
                printk("\b \b");
            }
        } else if (key >= '0' && key <= '9') {
            if (digits < 2U) {
                value = (value * 10U) + (uint32_t)(key - '0');
                digits++;
                printk("%c", key);
            }
        } else {
            printk("\n[DUMMY-SWV] Ignored key '%c'. Use digits then Enter.\n", key);
            printk("[DUMMY-SWV] n = ");
            if (digits > 0U) {
                printk("%u", value);
            }
        }
    }
}

static enum ad5941_swv_profile wait_for_dummy_swv_profile(void)
{
    printk("\n[DUMMY-SWV] Select SWV profile.\n");
    printk("[DUMMY-SWV] Press '5' for 5.37mV full-sequencer profile.\n");
    printk("[DUMMY-SWV] Press '2' for 2.15mV full-sequencer profile.\n");
    printk("[DUMMY-SWV] Press '1' for 1.07mV segmented profile.\n");
    printk("[DUMMY-SWV] Profile = ");

    while (1) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '5') {
            printk("5mV full\n");
            return AD5941_SWV_PROFILE_5MV_FULL;
        }
        if (key == '2') {
            printk("2mV full\n");
            return AD5941_SWV_PROFILE_2MV_FULL;
        }
        if (key == '1') {
            printk("1mV segmented\n");
            return AD5941_SWV_PROFILE_1MV_SEGMENTED;
        }

        printk("\n[DUMMY-SWV] Invalid key '%c'. Press '5', '2', or '1'.\n",
               key);
        printk("[DUMMY-SWV] Profile = ");
    }
}

static uint8_t wait_for_measurement_selection(void)
{
    int mode = 0;

    printk("\n===================================\n");
    printk("Ready for AD5941 Test Selection.\n");
    printk("Connect the external resistor between SE0 and CE0/RE0.\n");
    printk("Press '1' for +50mV test.\n");
    printk("Press '2' for +100mV test.\n");
    printk("Press '3' for Sequencer CA +100mV / 5s test.\n");
    printk("Press '4' for Sequencer CV staircase test.\n");
    printk("Press '5' for Sequencer SWV pulse test.\n");
    printk("Press '6' for Sequencer SWV-KDM diagnostic test.\n");
    printk("Press '7' for Sequencer SWV-KDM fast test.\n");
    printk("Press '8' for Sequencer SWV slow-motion SP test.\n");
    printk("Press '9' for Sequencer SWV low-impedance SP test.\n");
    printk("Press '0' for Sequencer SWV mixed-mode HSTIA test.\n");
    printk("Press 'a' for Sequencer SWV HSTIA external 2M 150Hz comparison test.\n");
    printk("Press 'b' for Sequencer SWV LPTIA external 2M 150Hz comparison test.\n");
    printk("Press 'c' for Sequencer SWV LPTIA external 2M 210Hz detune test.\n");
    printk("Press 'd' for Sequencer SWV LPTIA 512k||2M boost 150Hz test.\n");
    printk("Press 'e' for Sequencer SWV LPTIA external 2M no-boost 150Hz test.\n");
    printk("Press 'f' for Sequencer SWV HSTIA external 2M 150Hz Notch test.\n");
    printk("Press 'g' for Sequencer SWV LPTIA external 2M 150Hz Notch test.\n");
    printk("Press 'h' for Sequencer dual-frequency SWV/KDM dummy test.\n");
    printk("Press 'i' for AUTO repeat: HSTIA external AIN1 2M 150Hz, CTIA=4pF, 5 runs, 10s interval.\n");
    printk("Press 'j' for LPTIA endpoint step response test.\n");
    printk("Press 'k' for HSTIA endpoint step response test.\n");
    printk("Press 'l' for single-frequency PBS/MB blank peak SWV test.\n");
    printk("Press 'm' to skip tests and enter idle BLE/OLED loop.\n");
    printk("Press 'n' for blank peak SWV with smooth5/smooth7/SG5 output.\n");
    printk("Press 'o' for LPTIA external 2M zero-offset diagnostic.\n");
    printk("Press 'p' for HSTIA 160k blank peak SWV anti-saturation test.\n");
    printk("Press 'q' for HSTIA selectable RTIA PBS/MB blank peak SWV test.\n");
    printk("Press 'r' for DUMMY SWV, 5.37mV full sequence.\n");
    printk("Press 's' for DUMMY SWV, 2.15mV full sequence.\n");
    printk("Press 't' for DUMMY SWV, 1.07mV segmented sequence.\n");
    printk("Press 'u' to force AD5941 high-Z / cell-off now.\n");
    printk("Press 'v' for one DUMMY SWV high-Z validation scan.\n");
    printk("Press 'w' for AUTO repeat DUMMY SWV.\n");
    printk("Press 'x' for nPM1300 battery/charger status snapshot.\n");
    printk("Press uppercase 'P' in the idle loop to start/stop 100ms battery-current trace.\n");
    printk("Press 'z' for LEGACY DUMMY SWV: AIN2 330k, 120Hz, no high-Z guards.\n");
    printk("Press 'y' for manual High-Z validation: sweep -400mV to -200mV, then hold until 'u'.\n");
    printk("===================================\n");

    while (mode == 0) {
        int key = SEGGER_RTT_WaitKey();

        if (key == '1') {
            mode = 1;
        } else if (key == '2') {
            mode = 2;
        } else if (key == '3') {
            mode = 3;
        } else if (key == '4') {
            mode = 4;
        } else if (key == '5') {
            mode = 5;
        } else if (key == '6') {
            mode = 6;
        } else if (key == '7') {
            mode = 7;
        } else if (key == '8') {
            mode = 8;
        } else if (key == '9') {
            mode = 9;
        } else if (key == '0') {
            mode = 10;
        } else if (key == 'a' || key == 'A') {
            mode = 11;
        } else if (key == 'b' || key == 'B') {
            mode = 12;
        } else if (key == 'c' || key == 'C') {
            mode = 13;
        } else if (key == 'd' || key == 'D') {
            mode = 14;
        } else if (key == 'e' || key == 'E') {
            mode = 15;
        } else if (key == 'f' || key == 'F') {
            mode = 16;
        } else if (key == 'g' || key == 'G') {
            mode = 17;
        } else if (key == 'h' || key == 'H') {
            mode = 18;
        } else if (key == 'i' || key == 'I') {
            mode = 19;
        } else if (key == 'j' || key == 'J') {
            mode = 20;
        } else if (key == 'k' || key == 'K') {
            mode = 21;
        } else if (key == 'l' || key == 'L') {
            mode = 22;
        } else if (key == 'm' || key == 'M') {
            mode = 23;
        } else if (key == 'n' || key == 'N') {
            mode = 24;
        } else if (key == 'o' || key == 'O') {
            mode = 25;
        } else if (key == 'p' || key == 'P') {
            mode = 26;
        } else if (key == 'q' || key == 'Q') {
            mode = 27;
        } else if (key == 'r' || key == 'R') {
            mode = 28;
        } else if (key == 's' || key == 'S') {
            mode = 29;
        } else if (key == 't' || key == 'T') {
            mode = 30;
        } else if (key == 'u' || key == 'U') {
            mode = 31;
        } else if (key == 'v' || key == 'V') {
            mode = 32;
        } else if (key == 'w' || key == 'W') {
            mode = 33;
        } else if (key == 'x' || key == 'X') {
            mode = 34;
        } else if (key == 'y' || key == 'Y') {
            mode = 36;
        } else if (key == 'z' || key == 'Z') {
            mode = 35;
        } else {
            printk("Invalid key '%c'. Press '0' through '9', 'a' through 'x', or 'z'.\n", key);
        }
    }

    if (mode == 1 || mode == 2) {
        ad5941_linearity_test_setup((uint8_t)mode);
        k_msleep(100);
    } else if (mode == 3) {
        printk("[TEST] Sequencer CA mode selected.\n");
    } else if (mode == 4) {
        printk("[TEST] Sequencer CV mode selected.\n");
    } else if (mode == 5) {
        printk("[TEST] Sequencer SWV mode selected.\n");
    } else if (mode == 6) {
        printk("[TEST] Sequencer SWV-KDM diagnostic mode selected.\n");
    } else if (mode == 7) {
        printk("[TEST] Sequencer SWV-KDM fast mode selected.\n");
    } else if (mode == 8) {
        printk("[TEST] Sequencer SWV slow-motion SP mode selected.\n");
    } else if (mode == 9) {
        printk("[TEST] Sequencer SWV low-impedance SP mode selected.\n");
    } else if (mode == 10) {
        printk("[TEST] Sequencer SWV mixed-mode HSTIA mode selected.\n");
    } else if (mode == 11) {
        printk("[TEST] Sequencer SWV HSTIA external 2M 150Hz comparison mode selected.\n");
    } else if (mode == 12) {
        printk("[TEST] Sequencer SWV LPTIA external 2M 150Hz comparison mode selected.\n");
    } else if (mode == 13) {
        printk("[TEST] Sequencer SWV LPTIA external 2M 210Hz detune mode selected.\n");
    } else if (mode == 14) {
        printk("[TEST] Sequencer SWV LPTIA 512k||2M boost 150Hz mode selected.\n");
    } else if (mode == 15) {
        printk("[TEST] Sequencer SWV LPTIA external 2M no-boost 150Hz mode selected.\n");
    } else if (mode == 16) {
        printk("[TEST] Sequencer SWV HSTIA external 2M 150Hz Notch mode selected.\n");
    } else if (mode == 17) {
        printk("[TEST] Sequencer SWV LPTIA external 2M 150Hz Notch mode selected.\n");
    } else if (mode == 18) {
        printk("[TEST] Sequencer dual-frequency SWV/KDM dummy mode selected.\n");
    } else if (mode == 19) {
        printk("[TEST] AUTO repeat HSTIA external AIN1 2M 150Hz, CTIA=4pF mode selected.\n");
    } else if (mode == 20) {
        printk("[TEST] LPTIA endpoint step response mode selected.\n");
    } else if (mode == 21) {
        printk("[TEST] HSTIA endpoint step response mode selected.\n");
    } else if (mode == 22) {
        printk("[TEST] Single-frequency PBS/MB blank peak SWV mode selected.\n");
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
    } else if (mode == 23) {
        printk("[TEST] No measurement selected. Entering idle BLE/OLED loop.\n");
    } else if (mode == 24) {
        printk("[TEST] Single-frequency blank peak SWV smoothing comparison mode selected.\n");
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
    } else if (mode == 25) {
        printk("[TEST] LPTIA external 2M zero-offset diagnostic mode selected.\n");
    } else if (mode == 26) {
        printk("[TEST] HSTIA 160k blank peak SWV anti-saturation mode selected.\n");
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K;
    } else if (mode == 27) {
        printk("[TEST] HSTIA selectable RTIA PBS/MB blank peak SWV mode selected.\n");
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
    } else if (mode == 28) {
        printk("[TEST] Dummy SWV 5.37mV full-sequence mode selected.\n");
        dummy_swv_profile = AD5941_SWV_PROFILE_5MV_FULL;
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
    } else if (mode == 29) {
        printk("[TEST] Dummy SWV 2.15mV full-sequence mode selected.\n");
        dummy_swv_profile = AD5941_SWV_PROFILE_2MV_FULL;
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
    } else if (mode == 30) {
        printk("[TEST] Dummy SWV 1.07mV segmented mode selected.\n");
        dummy_swv_profile = AD5941_SWV_PROFILE_1MV_SEGMENTED;
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
    } else if (mode == 31) {
        printk("[TEST] Manual AD5941 high-Z / cell-off mode selected.\n");
    } else if (mode == 32) {
        printk("[TEST] One-shot dummy SWV high-Z validation mode selected.\n");
        dummy_swv_profile = AD5941_SWV_PROFILE_5MV_FULL;
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
    } else if (mode == 33) {
        printk("[TEST] Auto-repeat dummy SWV mode selected.\n");
        dummy_swv_profile = wait_for_dummy_swv_profile();
        blank_peak_frequency_hz = wait_for_blank_peak_frequency_hz();
        blank_peak_tia_path = wait_for_blank_peak_tia_path();
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
        dummy_swv_repeat_count = wait_for_dummy_repeat_count();
    } else if (mode == 34) {
        printk("[TEST] Battery/charger status selected.\n");
    } else if (mode == 35) {
        printk("[TEST] LEGACY dummy SWV selected: fixed AIN2 330k, 120Hz, 5.37mV step.\n");
        dummy_swv_resistor_ohms = wait_for_dummy_resistor_ohms();
    } else if (mode == 36) {
        printk("[TEST] Manual High-Z validation selected: sweep -400mV to -200mV, then hold until 'u'.\n");
    } else {
        printk("[TEST] No measurement selected. Entering idle BLE/OLED loop.\n");
    }

    return (uint8_t)mode;
}

static void run_selected_measurement(uint8_t test_mode)
{
    if (test_mode == 3U) {
        if (App_SeqCA_Test_Start() != 0) {
            printk("ERROR: Sequencer CA test failed to start.\n");
        }
    } else if (test_mode == 4U) {
        if (App_SeqCV_Test_Start() != 0) {
            printk("ERROR: Sequencer CV test failed to start.\n");
        }
    } else if (test_mode == 5U) {
        if (App_SeqSWV_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV test failed to start.\n");
        }
    } else if (test_mode == 6U) {
        if (App_SeqSWV_SinglePoint_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV-KDM diagnostic test failed to start.\n");
        }
    } else if (test_mode == 7U) {
        if (App_SeqSWV_FastKDM_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV-KDM fast test failed to start.\n");
        }
    } else if (test_mode == 8U) {
        if (App_SeqSWV_SlowMotion_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV slow-motion SP test failed to start.\n");
        }
    } else if (test_mode == 9U) {
        if (App_SeqSWV_LowImpedance_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV low-impedance SP test failed to start.\n");
        }
    } else if (test_mode == 10U) {
        if (App_SeqSWV_MixedModeHSTIA_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV mixed-mode HSTIA test failed to start.\n");
        }
    } else if (test_mode == 11U) {
        if (App_SeqSWV_HSTIAExternalAIN1_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV HSTIA external 2M 150Hz comparison test failed to start.\n");
        }
    } else if (test_mode == 12U) {
        if (App_SeqSWV_LPTIAExternal2M_150Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV LPTIA external 2M 150Hz comparison test failed to start.\n");
        }
    } else if (test_mode == 13U) {
        if (App_SeqSWV_LPTIAExternal2M_200Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV LPTIA external 2M 210Hz detune test failed to start.\n");
        }
    } else if (test_mode == 14U) {
        if (App_SeqSWV_LPTIA512KParallel2MBoost150Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV LPTIA 512k||2M boost 150Hz test failed to start.\n");
        }
    } else if (test_mode == 15U) {
        if (App_SeqSWV_LPTIAExternal2M_NoBoost150Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV LPTIA external 2M no-boost 150Hz test failed to start.\n");
        }
    } else if (test_mode == 16U) {
        if (App_SeqSWV_HSTIAExternalAIN1_Notch150Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV HSTIA external 2M 150Hz Notch test failed to start.\n");
        }
    } else if (test_mode == 17U) {
        if (App_SeqSWV_LPTIAExternal2M_Notch150Hz_Test_Start() != 0) {
            printk("ERROR: Sequencer SWV LPTIA external 2M 150Hz Notch test failed to start.\n");
        }
    } else if (test_mode == 18U) {
        if (App_SeqSWV_KDMPaper_Dummy_Test_Start() != 0) {
            printk("ERROR: Sequencer dual-frequency SWV/KDM dummy test failed to start.\n");
        }
    } else if (test_mode == 19U) {
        if (App_AutoRepeat_LPTIAExternal2M_150Hz_Test_Start() != 0) {
            printk("ERROR: AUTO repeat HSTIA external AIN1 2M 150Hz test failed to start.\n");
        }
    } else if (test_mode == 20U) {
        if (App_LPTIAStepResponse_Test_Start() != 0) {
            printk("ERROR: LPTIA endpoint step response test failed to start.\n");
        }
    } else if (test_mode == 21U) {
        if (App_HSTIAStepResponse_Test_Start() != 0) {
            printk("ERROR: HSTIA endpoint step response test failed to start.\n");
        }
    } else if (test_mode == 22U) {
        if (App_SeqSWV_BlankPeak_Test_Start(blank_peak_frequency_hz) != 0) {
            printk("ERROR: Single-frequency PBS/MB blank peak SWV test failed to start.\n");
        }
    } else if (test_mode == 23U) {
        printk("[TEST] Idle mode active. BLE is available; no AD5941 measurement started.\n");
    } else if (test_mode == 24U) {
        if (App_SeqSWV_BlankPeakSmoothing_Test_Start(blank_peak_frequency_hz) != 0) {
            printk("ERROR: Single-frequency blank peak SWV smoothing test failed to start.\n");
        }
    } else if (test_mode == 25U) {
        if (App_LPTIAOffsetDiagnostic_Test_Start() != 0) {
            printk("ERROR: LPTIA external 2M zero-offset diagnostic failed.\n");
        }
    } else if (test_mode == 26U) {
        if (App_SeqSWV_BlankPeakHSTIA160K_Test_Start(blank_peak_frequency_hz) != 0) {
            printk("ERROR: HSTIA 160k blank peak SWV anti-saturation test failed to start.\n");
        }
    } else if (test_mode == 27U) {
        if (App_SeqSWV_BlankPeakSelectableTIA_Test_Start(blank_peak_frequency_hz,
                                                         blank_peak_tia_path) != 0) {
            printk("ERROR: HSTIA selectable RTIA blank peak SWV test failed to start.\n");
        }
    } else if (test_mode == 28U || test_mode == 29U || test_mode == 30U ||
               test_mode == 32U) {
        if (App_SeqSWV_DummyCell_Test_Start(dummy_swv_profile,
                                            blank_peak_frequency_hz,
                                            blank_peak_tia_path,
                                            dummy_swv_resistor_ohms) != 0) {
            printk("ERROR: Dummy-cell SWV test failed to start.\n");
        }
    } else if (test_mode == 31U) {
        ad5941_app_enter_high_z_now();
    } else if (test_mode == 36U) {
        if (ad5941_app_start_high_z_validation() != 0) {
            printk("ERROR: High-Z validation test failed to start.\n");
        }
    } else if (test_mode == 33U) {
        if (App_AutoRepeat_DummySWV_Test_Start(dummy_swv_profile,
                                               blank_peak_frequency_hz,
                                               blank_peak_tia_path,
                                               dummy_swv_resistor_ohms,
                                               dummy_swv_repeat_count,
                                               5000U) != 0) {
            printk("ERROR: Auto-repeat dummy-cell SWV test failed to start.\n");
        }
    } else if (test_mode == 34U) {
        print_charger_sample();
    } else if (test_mode == 35U) {
        if (App_SeqSWV_DummyCellLegacyAIN2_Test_Start(dummy_swv_resistor_ohms) != 0) {
            printk("ERROR: Legacy dummy-cell SWV test failed to start.\n");
        }
    } else {
        ad5941_app_start_measurement();
    }

    printk("[TEST] To run another RTT test without MCU reset: wait for completion, then press 'y'.\n");
}
#endif

int main(void)
{
    bool power_is_ok = false;
    bool afe_ready = false;
    bool ble_ok = false;
    uint32_t oled_tick = 0;

    usb_cdc_start();

    printk("\n\n");
    printk("========================================\n");
    printk(" NanoStat Bring-up\n");
    printk("========================================\n");

    bool pmic_ok = device_is_ready(pmic);
    bool charger_ok = device_is_ready(charger);
    bool leds_ok = device_is_ready(npm1300_leds);

    if (!pmic_ok || !charger_ok || !leds_ok) {
        while (1) {
            printk("\n>>> HARDWARE INIT FAILURE <<<\n");
            if (!pmic_ok) {
                printk("ERROR: PMIC (npm1300) is NOT ready! Check I2C wiring.\n");
            }
            if (!charger_ok) {
                printk("ERROR: Charger (npm1300_charger) is NOT ready!\n");
            }
            if (!leds_ok) {
                printk("ERROR: LEDs (npm1300_leds) are NOT ready!\n");
            }
            printk("System halted to prevent further damage.\n");

            if (leds_ok) {
                (void)led_on(npm1300_leds, 2);
                k_sleep(K_MSEC(50));
                (void)led_off(npm1300_leds, 2);
            }
            k_sleep(K_MSEC(1950));
        }
    }
    printk("nPM1300 core devices ready.\n");

    if (ble_service_init() != 0) {
        printk("WARNING: BLE initialization failed. Continuing without BLE.\n");
    } else {
        ble_ok = true;
    }

    if (board_power_system_init() != 0) {
        printk("WARNING: Board power sequence failed. System might be unstable.\n");
        power_is_ok = false;
    } else {
        printk("SUCCESS: Power rails established & SLP Button Listener Active.\n");
        k_msleep(50);
        (void)oled_display_init();
        oled_display_show_boot("Power rails OK");
        power_is_ok = true;
    }

    if (power_is_ok) {
        int ret = ad5941_app_init();

        if (ret == 0) {
            printk("AFE Subsystem Bring-up SUCCESS!\n");
            oled_display_show_boot("AFE ready");
            afe_ready = true;
#if ENABLE_LINEARITY_TEST
            printk("[TEST] Idle BLE/OLED loop active. Press 'y' in RTT for AD5941 menu, or 'b' for battery status.\n");
#else
            ad5941_app_start_measurement();
#endif
        } else {
            printk("ERROR: AFE Subsystem Initialization FAILED!\n");
            oled_display_show_boot("AFE init failed");
            afe_ready = false;
        }
    } else {
        printk("SKIPPED: AD5941 initialization bypassed due to power failure.\n");
        oled_display_show_boot("Power failed");
    }

    /* uint32_t loop_count = 0; */

    while (1) {
        if (shutting_down) {
            printk("\n[SHUTDOWN] Shutdown signal received. Exiting main loop...\n");
            break;
        }

        /* Battery monitor output is temporarily disabled to keep AD5941 RTT logs readable. */
        /* printk("[Loop %u] ", loop_count++); */
        /* print_charger_sample(); */

#if ENABLE_LINEARITY_TEST
        if (power_is_ok && afe_ready) {
            int key = SEGGER_RTT_GetKey();

            if (key == 'b' || key == 'B') {
                print_charger_sample();
            } else if (key == 'y' || key == 'Y') {
                if (ad5941_app_is_measurement_busy()) {
                    printk("[TEST] AFE measurement is still running; wait for completion before returning to menu.\n");
                } else {
                    uint8_t test_mode;

                    test_mode = wait_for_measurement_selection();
                    if (test_mode == 35U) {
                        printk("[TEST] Legacy mode: skipping pre-test soft reset/high-Z for A/B comparison.\n");
                    } else {
                        ad5941_app_soft_reset_for_next_test();
                    }
                    run_selected_measurement(test_mode);
                }
            }
        }
#endif

        (void)led_on(npm1300_leds, 0);
        main_loop_sleep(10U);
        (void)led_off(npm1300_leds, 0);
        main_loop_sleep(100U);

        if (power_is_ok && afe_ready) {
            (void)led_on(npm1300_leds, 1);
            main_loop_sleep(10U);
            (void)led_off(npm1300_leds, 1);

            if (++oled_tick >= 5U) {
                int32_t latest_current_pa = 0;
                uint32_t sample_count = 0;

                oled_tick = 0;
                if (ad5941_app_get_latest_current(&latest_current_pa, &sample_count)) {
                    oled_display_show_afe_sample(sample_count, latest_current_pa);
                } else {
                    oled_display_show_boot("AFE sampling");
                }
            }
        }
        main_loop_sleep(100U);

        if (ble_ok) {
            if (ble_service_is_connected()) {
                (void)led_on(npm1300_leds, 2);
                main_loop_sleep(10U);
                (void)led_off(npm1300_leds, 2);
            } else {
                (void)led_off(npm1300_leds, 2);
            }
        } else {
            (void)led_off(npm1300_leds, 2);
        }

        for (int i = 0; i < 177; i++) {
            if (shutting_down) {
                break;
            }
            main_loop_sleep(10U);
        }
    }

    printk("Initiating hardware cleanup for Graceful Shutdown...\n");
    ad5941_app_prepare_shutdown();

    while (1) {
        k_sleep(K_MSEC(100));
    }

    return 0;
}
