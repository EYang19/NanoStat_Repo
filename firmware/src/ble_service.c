/*
 * NanoStat BLE service layer.
 */

#include "ble_service.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ad5941_app.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include <bluetooth/services/nus.h>

#define BLE_TX_CHUNK_FALLBACK_MAX 20U
#define BLE_TX_RETRY_COUNT 30U
#define BLE_TX_RETRY_DELAY_MS 10U
#define BLE_TX_INTER_CHUNK_DELAY_MS 5U
#define BLE_TX_INTER_LINE_DELAY_MS 8U
#define BLE_RX_CMD_MAX_LEN 160U
#define BLE_CMD_MAX_TOKENS 8U
#define BLE_RX_LINE_BUF_LEN BLE_RX_CMD_MAX_LEN
#define CHARGER_NODE DT_NODELABEL(npm1300_charger)

BUILD_ASSERT(DT_NODE_EXISTS(CHARGER_NODE), "Missing DT node label: npm1300_charger");

enum ble_pending_command {
    BLE_PENDING_NONE = 0,
    BLE_PENDING_START_BLANK,
    BLE_PENDING_START_DUMMY,
    BLE_PENDING_START_DUMMY_REPEAT,
    BLE_PENDING_START_HIGHZ_TEST,
    BLE_PENDING_HIGHZ,
    BLE_PENDING_RESET_HIGHZ_TEST,
    BLE_PENDING_BATTERY,
};

static struct bt_conn *current_conn;
static const struct device *const charger = DEVICE_DT_GET(CHARGER_NODE);
static K_MUTEX_DEFINE(ble_tx_mutex);
static atomic_t battery_status_active;
static bool ble_ready;
static bool nus_notify_enabled;
static struct k_work command_work;
static enum ble_pending_command pending_command;
static char ble_rx_line_buf[BLE_RX_LINE_BUF_LEN];
static size_t ble_rx_line_len;
static uint32_t pending_blank_frequency_hz;
static enum ad5941_blank_peak_tia_path pending_blank_tia_path;
static enum ad5941_swv_profile pending_dummy_profile;
static uint32_t pending_dummy_frequency_hz;
static enum ad5941_blank_peak_tia_path pending_dummy_tia_path;
static uint32_t pending_dummy_ohms;
static uint8_t pending_dummy_repeat_count;

static int ble_start_advertising(void);
static void ble_command_work_handler(struct k_work *work);
static void ble_rx_line_reset(void);
static void ble_rx_line_process(void);

static int32_t sensor_value_to_mv(const struct sensor_value *val)
{
    return (val->val1 * 1000) + (val->val2 / 1000);
}

static int32_t sensor_value_to_ua(const struct sensor_value *val)
{
    return (val->val1 * 1000000) + val->val2;
}

static int32_t sensor_value_to_mc(const struct sensor_value *val)
{
    return (val->val1 * 1000) + (val->val2 / 1000);
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
        return "very_low";
    }

    return "critical";
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0U) {
        printk("[BLE] Connection failed, err=0x%02x.\n", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    printk("[BLE] Central connected.\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    printk("[BLE] Central disconnected, reason=0x%02x.\n", reason);

    if (current_conn != NULL) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    nus_notify_enabled = false;
    ble_rx_line_reset();

    if (ble_ready) {
        (void)ble_start_advertising();
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static bool ascii_equal_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - ('a' - 'A'));
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

static void trim_line_end(char *cmd)
{
    size_t len = strlen(cmd);

    while (len > 0U) {
        char c = cmd[len - 1U];

        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            break;
        }
        cmd[len - 1U] = '\0';
        len--;
    }
}

static const char *blank_tia_path_text(enum ad5941_blank_peak_tia_path tia_path)
{
    switch (tia_path) {
    case AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K:
        return "HSTIA160K";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K:
        return "HSTIAEXT160K";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K:
        return "HSTIAEXT330K";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K:
        return "HSTIAEXT680K";
    case AD5941_BLANK_PEAK_TIA_LPTIA:
    default:
        return "LPTIA";
    }
}

static const char *swv_profile_text(enum ad5941_swv_profile profile)
{
    switch (profile) {
    case AD5941_SWV_PROFILE_5MV_FULL:
        return "5MV";
    case AD5941_SWV_PROFILE_2MV_FULL:
        return "2MV";
    case AD5941_SWV_PROFILE_1MV_SEGMENTED:
    default:
        return "1MVSEG";
    }
}

static bool parse_swv_profile(const char *arg, enum ad5941_swv_profile *profile)
{
    if (ascii_equal_ci(arg, "5MV") ||
        ascii_equal_ci(arg, "5") ||
        ascii_equal_ci(arg, "5MVFULL")) {
        *profile = AD5941_SWV_PROFILE_5MV_FULL;
        return true;
    }
    if (ascii_equal_ci(arg, "2MV") ||
        ascii_equal_ci(arg, "2") ||
        ascii_equal_ci(arg, "2MVFULL")) {
        *profile = AD5941_SWV_PROFILE_2MV_FULL;
        return true;
    }
    if (ascii_equal_ci(arg, "1MV") ||
        ascii_equal_ci(arg, "1") ||
        ascii_equal_ci(arg, "1MVSEG") ||
        ascii_equal_ci(arg, "1MVSEGMENTED")) {
        *profile = AD5941_SWV_PROFILE_1MV_SEGMENTED;
        return true;
    }

    return false;
}

static bool parse_blank_tia_path(const char *arg,
                                 enum ad5941_blank_peak_tia_path *tia_path)
{
    if (ascii_equal_ci(arg, "HSTIA") ||
        ascii_equal_ci(arg, "HSTIA160K") ||
        ascii_equal_ci(arg, "P")) {
        *tia_path = AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K;
        return true;
    }
    if (ascii_equal_ci(arg, "HSTIAEXT160K") ||
        ascii_equal_ci(arg, "EXT160K") ||
        ascii_equal_ci(arg, "X160") ||
        ascii_equal_ci(arg, "E160")) {
        *tia_path = AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K;
        return true;
    }
    if (ascii_equal_ci(arg, "HSTIAEXT330K") ||
        ascii_equal_ci(arg, "EXT330K") ||
        ascii_equal_ci(arg, "X330") ||
        ascii_equal_ci(arg, "E330")) {
        *tia_path = AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K;
        return true;
    }
    if (ascii_equal_ci(arg, "HSTIAEXT680K") ||
        ascii_equal_ci(arg, "EXT680K") ||
        ascii_equal_ci(arg, "X680") ||
        ascii_equal_ci(arg, "E680")) {
        *tia_path = AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K;
        return true;
    }
    if (ascii_equal_ci(arg, "LPTIA") ||
        ascii_equal_ci(arg, "L")) {
        *tia_path = AD5941_BLANK_PEAK_TIA_LPTIA;
        return true;
    }

    return false;
}

static bool parse_u32_arg(const char *arg,
                          uint32_t min_value,
                          uint32_t max_value,
                          uint32_t *value)
{
    char *endptr = NULL;
    unsigned long parsed;

    if (arg == NULL || arg[0] == '\0') {
        return false;
    }

    parsed = strtoul(arg, &endptr, 10);
    if (endptr == arg || *endptr != '\0' ||
        parsed < (unsigned long)min_value ||
        parsed > (unsigned long)max_value) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static size_t split_csv_tokens(char *cmd, char *tokens[], size_t max_tokens)
{
    size_t count = 0U;
    char *cursor = cmd;

    while (cursor != NULL && count < max_tokens) {
        char *comma = strchr(cursor, ',');

        if (comma != NULL) {
            *comma = '\0';
        }
        tokens[count++] = cursor;
        cursor = comma != NULL ? comma + 1 : NULL;
    }

    return count;
}

static void ble_send_latest_status(const char *prefix)
{
    int32_t current_pa = 0;
    uint32_t sample_count = 0;
    bool have_sample = ad5941_app_get_latest_current(&current_pa, &sample_count);
    int32_t abs_pa = current_pa < 0 ? -current_pa : current_pa;
    char line[96];

    if (have_sample) {
        snprintk(line,
                 sizeof(line),
                 "%s,AFE=1,BLE=1,SAMPLES=%u,I_NA=%s%d.%03d\r\n",
                 prefix,
                 sample_count,
                 current_pa < 0 ? "-" : "",
                 abs_pa / 1000,
                 abs_pa % 1000);
    } else {
        snprintk(line, sizeof(line), "%s,AFE=1,BLE=1,SAMPLES=0,I_NA=NA\r\n", prefix);
    }

    (void)ble_service_send_text(line);
}

static int ble_get_channel(enum sensor_channel chan, struct sensor_value *val)
{
    return sensor_channel_get(charger, chan, val);
}

static void ble_send_battery_status(void)
{
    struct sensor_value val;
    char line[128];
    int err;
    int32_t vbat_mv = 0;
    int32_t ibat_ua = 0;
    int32_t ntc_mc = 0;
    int32_t die_mc = 0;
    int32_t vbus_limit_ua = 0;
    int32_t charge_current_ua = 0;
    int32_t discharge_limit_ua = 0;
    int32_t chg_status = -1;
    int32_t chg_error = -1;
    bool have_vbat;
    bool have_ibat;
    bool have_ntc;
    bool have_die;
    bool have_vbus;
    bool have_charge_current;
    bool have_discharge_limit;
    bool have_chg_status;
    bool have_chg_error;

    if (!device_is_ready(charger)) {
        (void)ble_service_send_text("ERR,BATTERY_NOT_READY\r\n");
        return;
    }

    err = sensor_sample_fetch(charger);
    if (err != 0) {
        snprintk(line, sizeof(line), "ERR,BATTERY_FETCH,%d\r\n", err);
        (void)ble_service_send_text(line);
        return;
    }

    have_vbat = ble_get_channel(SENSOR_CHAN_GAUGE_VOLTAGE, &val) == 0;
    if (have_vbat) {
        vbat_mv = sensor_value_to_mv(&val);
    }

    have_ibat = ble_get_channel(SENSOR_CHAN_GAUGE_AVG_CURRENT, &val) == 0;
    if (have_ibat) {
        ibat_ua = sensor_value_to_ua(&val);
    }

    have_ntc = ble_get_channel(SENSOR_CHAN_GAUGE_TEMP, &val) == 0;
    if (have_ntc) {
        ntc_mc = sensor_value_to_mc(&val);
    }

    have_die = ble_get_channel(SENSOR_CHAN_DIE_TEMP, &val) == 0;
    if (have_die) {
        die_mc = sensor_value_to_mc(&val);
    }

    have_chg_status = ble_get_channel((enum sensor_channel)SENSOR_CHAN_NPM1300_CHARGER_STATUS, &val) == 0;
    if (have_chg_status) {
        chg_status = val.val1;
    }

    have_chg_error = ble_get_channel((enum sensor_channel)SENSOR_CHAN_NPM1300_CHARGER_ERROR, &val) == 0;
    if (have_chg_error) {
        chg_error = val.val1;
    }

    err = sensor_attr_get(charger, SENSOR_CHAN_CURRENT, SENSOR_ATTR_UPPER_THRESH, &val);
    have_vbus = err == 0;
    if (have_vbus) {
        vbus_limit_ua = sensor_value_to_ua(&val);
    }

    have_charge_current = ble_get_channel(SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &val) == 0;
    if (have_charge_current) {
        charge_current_ua = sensor_value_to_ua(&val);
    }

    have_discharge_limit = ble_get_channel(SENSOR_CHAN_GAUGE_MAX_LOAD_CURRENT, &val) == 0;
    if (have_discharge_limit) {
        discharge_limit_ua = sensor_value_to_ua(&val);
    }

    if (have_vbat) {
        snprintk(line,
                 sizeof(line),
                 "BAT,VBAT_MV=%d,SOC_PCT=%u,SOC_NOTE=%s\r\n",
                 vbat_mv,
                 rough_lipo_soc_percent(vbat_mv),
                 rough_lipo_soc_note(vbat_mv));
    } else {
        snprintk(line, sizeof(line), "BAT,VBAT_MV=NA,SOC_PCT=NA,SOC_NOTE=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_ibat) {
        snprintk(line,
                 sizeof(line),
                 "BAT,IBAT_UA=%d,IBAT_STATE=%s\r\n",
                 ibat_ua,
                 ibat_ua < 0 ? "charging" :
                 (ibat_ua > 0 ? "discharging" : "idle"));
    } else {
        snprintk(line, sizeof(line), "BAT,IBAT_UA=NA,IBAT_STATE=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_ntc) {
        snprintk(line,
                 sizeof(line),
                 "BAT,NTC_MC=%d,NTC_NOTE=FIXED_10K_NOT_CELL_TEMP\r\n",
                 ntc_mc);
    } else {
        snprintk(line, sizeof(line), "BAT,NTC_MC=NA,NTC_NOTE=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_die) {
        snprintk(line, sizeof(line), "BAT,DIE_MC=%d\r\n", die_mc);
    } else {
        snprintk(line, sizeof(line), "BAT,DIE_MC=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_chg_status) {
        snprintk(line, sizeof(line), "BAT,CHG_STATUS=0x%02x\r\n", chg_status);
    } else {
        snprintk(line, sizeof(line), "BAT,CHG_STATUS=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_chg_error) {
        snprintk(line,
                 sizeof(line),
                 "BAT,CHG_ERROR=0x%02x,CHG_ERROR_NOTE=%s\r\n",
                 chg_error,
                 chg_error == 0 ? "none" : "check_flags");
    } else {
        snprintk(line, sizeof(line), "BAT,CHG_ERROR=NA,CHG_ERROR_NOTE=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_vbus) {
        snprintk(line,
                 sizeof(line),
                 "BAT,VBUS_LIMIT_UA=%d,VBUS_STATE=%s\r\n",
                 vbus_limit_ua,
                 vbus_limit_ua == 0 ? "not_detected" : "detected");
    } else {
        snprintk(line, sizeof(line), "BAT,VBUS_LIMIT_UA=NA,VBUS_STATE=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_charge_current) {
        snprintk(line, sizeof(line), "BAT,CHARGE_CURRENT_UA=%d\r\n", charge_current_ua);
    } else {
        snprintk(line, sizeof(line), "BAT,CHARGE_CURRENT_UA=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_discharge_limit) {
        snprintk(line, sizeof(line), "BAT,DISCHARGE_LIMIT_UA=%d\r\n", discharge_limit_ua);
    } else {
        snprintk(line, sizeof(line), "BAT,DISCHARGE_LIMIT_UA=NA\r\n");
    }
    (void)ble_service_send_text(line);

    if (have_vbat && vbat_mv < 3500) {
        snprintk(line,
                 sizeof(line),
                 "BAT,WARNING=%s\r\n",
                 vbat_mv < 3300 ? "VBAT_CRITICAL" : "VBAT_VERY_LOW");
        (void)ble_service_send_text(line);
    }
    if (have_ibat && ibat_ua < 0) {
        (void)ble_service_send_text("BAT,NOTE=CHARGING_CURRENT_FLOWING\r\n");
    }

    (void)ble_service_send_text("BAT,DONE\r\n");
}

static void ble_handle_command(char *cmd)
{
    char *argv[BLE_CMD_MAX_TOKENS] = {0};
    size_t argc;

    trim_line_end(cmd);
    if (cmd[0] == '\0') {
        (void)ble_service_send_text("ERR,EMPTY_CMD\r\n");
        return;
    }

    argc = split_csv_tokens(cmd, argv, ARRAY_SIZE(argv));

    if (ascii_equal_ci(argv[0], "PING")) {
        (void)ble_service_send_text("OK,PONG\r\n");
    } else if (ascii_equal_ci(argv[0], "STATUS")) {
        ble_send_latest_status("STATUS");
    } else if (ascii_equal_ci(argv[0], "B") ||
               ascii_equal_ci(argv[0], "BATTERY") ||
               ascii_equal_ci(argv[0], "BAT") ||
               (ascii_equal_ci(argv[0], "START") &&
                argc >= 2U && ascii_equal_ci(argv[1], "BATTERY"))) {
        if (!atomic_cas(&battery_status_active, 0, 1)) {
            (void)ble_service_send_text("ERR,BATTERY_BUSY\r\n");
            return;
        }
        if (k_work_busy_get(&command_work) != 0U ||
            ad5941_app_is_measurement_busy()) {
            atomic_clear(&battery_status_active);
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_command = BLE_PENDING_BATTERY;
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "READ") &&
               argc >= 2U && ascii_equal_ci(argv[1], "LATEST")) {
        ble_send_latest_status("DATA,LATEST");
    } else if (ascii_equal_ci(argv[0], "START") &&
               argc >= 2U && ascii_equal_ci(argv[1], "IDLE")) {
        (void)ble_service_send_text("OK,IDLE\r\n");
    } else if (ascii_equal_ci(argv[0], "START") &&
               argc >= 2U && ascii_equal_ci(argv[1], "HIGHZ")) {
        if (k_work_busy_get(&command_work) != 0U) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_command = BLE_PENDING_HIGHZ;
        (void)ble_service_send_text("OK,QUEUED,HIGHZ\r\n");
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "START") &&
               argc >= 2U && ascii_equal_ci(argv[1], "HIGHZ_TEST")) {
        if (k_work_busy_get(&command_work) != 0U ||
            ad5941_app_is_measurement_busy()) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_command = BLE_PENDING_START_HIGHZ_TEST;
        (void)ble_service_send_text("OK,QUEUED,HIGHZ_TEST\r\n");
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "RESET") &&
               argc >= 2U && ascii_equal_ci(argv[1], "HIGHZ_TEST")) {
        if (k_work_busy_get(&command_work) != 0U) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_command = BLE_PENDING_RESET_HIGHZ_TEST;
        (void)ble_service_send_text("OK,QUEUED,RESET_HIGHZ_TEST\r\n");
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "START") &&
               argc >= 3U && ascii_equal_ci(argv[1], "BLANK")) {
        uint32_t freq;
        enum ad5941_blank_peak_tia_path tia_path = AD5941_BLANK_PEAK_TIA_LPTIA;

        if (!parse_u32_arg(argv[2], 1U, 250U, &freq)) {
            (void)ble_service_send_text("ERR,BAD_FREQ\r\n");
            return;
        }
        if (argc >= 4U && argv[3][0] != '\0') {
            if (!parse_blank_tia_path(argv[3], &tia_path)) {
                (void)ble_service_send_text("ERR,BAD_TIA_MODE\r\n");
                return;
            }
        }
        if (k_work_busy_get(&command_work) != 0U ||
            ad5941_app_is_measurement_busy()) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_blank_frequency_hz = freq;
        pending_blank_tia_path = tia_path;
        pending_command = BLE_PENDING_START_BLANK;
        (void)ble_service_send_text("OK,QUEUED,BLANK\r\n");
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "START") &&
               argc >= 6U &&
               (ascii_equal_ci(argv[1], "DUMMY") ||
                ascii_equal_ci(argv[1], "DUMMY_REPEAT"))) {
        bool repeat = ascii_equal_ci(argv[1], "DUMMY_REPEAT");
        enum ad5941_swv_profile profile;
        enum ad5941_blank_peak_tia_path tia_path;
        uint32_t freq;
        uint32_t dummy_ohms;
        uint32_t repeat_count = 1U;

        if (!parse_swv_profile(argv[2], &profile)) {
            (void)ble_service_send_text("ERR,BAD_PROFILE\r\n");
            return;
        }
        if (!parse_u32_arg(argv[3], 1U, 250U, &freq)) {
            (void)ble_service_send_text("ERR,BAD_FREQ\r\n");
            return;
        }
        if (!parse_blank_tia_path(argv[4], &tia_path)) {
            (void)ble_service_send_text("ERR,BAD_TIA_MODE\r\n");
            return;
        }
        if (!parse_u32_arg(argv[5], 1000U, 100000000U, &dummy_ohms)) {
            (void)ble_service_send_text("ERR,BAD_DUMMY_RESISTOR\r\n");
            return;
        }
        if (repeat) {
            if (argc < 7U ||
                !parse_u32_arg(argv[6], 1U, 50U, &repeat_count)) {
                (void)ble_service_send_text("ERR,BAD_REPEAT_COUNT\r\n");
                return;
            }
        }
        if (k_work_busy_get(&command_work) != 0U ||
            ad5941_app_is_measurement_busy()) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        pending_dummy_profile = profile;
        pending_dummy_frequency_hz = freq;
        pending_dummy_tia_path = tia_path;
        pending_dummy_ohms = dummy_ohms;
        pending_dummy_repeat_count = (uint8_t)repeat_count;
        pending_command = repeat ? BLE_PENDING_START_DUMMY_REPEAT :
                          BLE_PENDING_START_DUMMY;
        (void)ble_service_send_text(repeat ?
                                    "OK,QUEUED,DUMMY_REPEAT\r\n" :
                                    "OK,QUEUED,DUMMY_SWV\r\n");
        k_work_submit(&command_work);
    } else if (ascii_equal_ci(argv[0], "STOP")) {
        (void)ble_service_send_text("ERR,STOP_NOT_IMPLEMENTED\r\n");
    } else {
        (void)ble_service_send_text("ERR,BAD_CMD\r\n");
    }
}

static void ble_rx_line_reset(void)
{
    ble_rx_line_len = 0U;
    ble_rx_line_buf[0] = '\0';
}

static void ble_rx_line_process(void)
{
    if (ble_rx_line_len == 0U) {
        return;
    }

    ble_rx_line_buf[ble_rx_line_len] = '\0';
    printk("[BLE] RX command: %s\n", ble_rx_line_buf);
    ble_handle_command(ble_rx_line_buf);
    ble_rx_line_reset();
}

static void nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
    ARG_UNUSED(conn);

    for (uint16_t idx = 0U; idx < len; idx++) {
        char c = (char)data[idx];

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            ble_rx_line_process();
            continue;
        }

        if (ble_rx_line_len >= (sizeof(ble_rx_line_buf) - 1U)) {
            ble_rx_line_reset();
            (void)ble_service_send_text("ERR,CMD_TOO_LONG\r\n");
            return;
        }

        ble_rx_line_buf[ble_rx_line_len++] = c;
    }
}

static void nus_send_enabled(enum bt_nus_send_status status)
{
    nus_notify_enabled = (status == BT_NUS_SEND_STATUS_ENABLED);
    printk("[BLE] NUS TX notify %s.\n", nus_notify_enabled ? "enabled" : "disabled");

    if (nus_notify_enabled) {
        (void)ble_service_send_text("NanoStat BLE ready\r\n");
    }
}

static struct bt_nus_cb nus_cb = {
    .received = nus_received,
    .send_enabled = nus_send_enabled,
};

static void ble_command_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (pending_command == BLE_PENDING_RESET_HIGHZ_TEST) {
        pending_command = BLE_PENDING_NONE;
        ad5941_app_reset_high_z_validation();
        (void)ble_service_send_text("OK,RESET,HIGHZ_TEST\r\n");
    } else if (pending_command == BLE_PENDING_START_HIGHZ_TEST) {
        int err;

        pending_command = BLE_PENDING_NONE;
        ad5941_app_soft_reset_for_next_test();
        err = ad5941_app_start_high_z_validation();
        if (err == 0) {
            (void)ble_service_send_text("OK,STARTED,HIGHZ_TEST\r\n");
        } else {
            char line[48];

            snprintk(line, sizeof(line), "ERR,HIGHZ_TEST_START,%d\r\n", err);
            (void)ble_service_send_text(line);
        }
    } else if (pending_command == BLE_PENDING_START_BLANK) {
        uint32_t freq = pending_blank_frequency_hz;
        enum ad5941_blank_peak_tia_path tia_path = pending_blank_tia_path;
        const char *tia_text = blank_tia_path_text(tia_path);
        int err;
        char line[72];

        pending_command = BLE_PENDING_NONE;
        snprintk(line, sizeof(line), "EVT,STARTING,BLANK,%u,%s\r\n", freq, tia_text);
        (void)ble_service_send_text(line);

        if (ad5941_app_is_measurement_busy()) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        ad5941_app_soft_reset_for_next_test();
        err = App_SeqSWV_BlankPeakSelectableTIA_Test_Start(freq, tia_path);
        if (err == 0) {
            snprintk(line, sizeof(line), "OK,STARTED,BLANK,%u,%s\r\n", freq, tia_text);
            (void)ble_service_send_text(line);
        } else {
            snprintk(line, sizeof(line), "ERR,START_FAILED,%d\r\n", err);
            (void)ble_service_send_text(line);
        }
    } else if (pending_command == BLE_PENDING_START_DUMMY ||
               pending_command == BLE_PENDING_START_DUMMY_REPEAT) {
        enum ble_pending_command command = pending_command;
        enum ad5941_swv_profile profile = pending_dummy_profile;
        uint32_t freq = pending_dummy_frequency_hz;
        enum ad5941_blank_peak_tia_path tia_path = pending_dummy_tia_path;
        uint32_t dummy_ohms = pending_dummy_ohms;
        uint8_t repeat_count = pending_dummy_repeat_count;
        const char *profile_text = swv_profile_text(profile);
        const char *tia_text = blank_tia_path_text(tia_path);
        int err;
        char line[112];

        pending_command = BLE_PENDING_NONE;
        if (command == BLE_PENDING_START_DUMMY_REPEAT) {
            snprintk(line,
                     sizeof(line),
                     "EVT,STARTING,DUMMY_REPEAT,%s,%u,%s,%u,%u\r\n",
                     profile_text,
                     freq,
                     tia_text,
                     dummy_ohms,
                     repeat_count);
        } else {
            snprintk(line,
                     sizeof(line),
                     "EVT,STARTING,DUMMY_SWV,%s,%u,%s,%u\r\n",
                     profile_text,
                     freq,
                     tia_text,
                     dummy_ohms);
        }
        (void)ble_service_send_text(line);

        if (ad5941_app_is_measurement_busy()) {
            (void)ble_service_send_text("ERR,BUSY\r\n");
            return;
        }

        ad5941_app_soft_reset_for_next_test();
        if (command == BLE_PENDING_START_DUMMY_REPEAT) {
            err = App_AutoRepeat_DummySWV_Test_Start(profile,
                                                     freq,
                                                     tia_path,
                                                     dummy_ohms,
                                                     repeat_count,
                                                     5000U);
        } else {
            err = App_SeqSWV_DummyCell_Test_Start(profile,
                                                  freq,
                                                  tia_path,
                                                  dummy_ohms);
        }
        if (err == 0) {
            if (command == BLE_PENDING_START_DUMMY_REPEAT) {
                snprintk(line,
                         sizeof(line),
                         "OK,STARTED,DUMMY_REPEAT,%s,%u,%s,%u,%u\r\n",
                         profile_text,
                         freq,
                         tia_text,
                         dummy_ohms,
                         repeat_count);
            } else {
                snprintk(line,
                         sizeof(line),
                         "OK,STARTED,DUMMY_SWV,%s,%u,%s,%u\r\n",
                         profile_text,
                         freq,
                         tia_text,
                         dummy_ohms);
            }
            (void)ble_service_send_text(line);
        } else {
            snprintk(line, sizeof(line), "ERR,START_FAILED,%d\r\n", err);
            (void)ble_service_send_text(line);
        }
    } else if (pending_command == BLE_PENDING_HIGHZ) {
        pending_command = BLE_PENDING_NONE;
        ad5941_app_enter_high_z_now();
        (void)ble_service_send_text("OK,HIGHZ\r\n");
    } else if (pending_command == BLE_PENDING_BATTERY) {
        pending_command = BLE_PENDING_NONE;
        ble_send_battery_status();
        atomic_clear(&battery_status_active);
    }
}

static int ble_start_advertising(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN,
                              ad,
                              ARRAY_SIZE(ad),
                              sd,
                              ARRAY_SIZE(sd));

    if (err == -EALREADY) {
        printk("[BLE] Advertising already active.\n");
        return 0;
    }
    if (err != 0) {
        printk("[BLE] Advertising start failed: %d\n", err);
        return err;
    }

    printk("[BLE] Advertising as \"%s\" with Nordic UART Service.\n",
           CONFIG_BT_DEVICE_NAME);
    return 0;
}

int ble_service_init(void)
{
    int err;

    err = bt_enable(NULL);
    if (err != 0) {
        printk("[BLE] bt_enable failed: %d\n", err);
        return err;
    }

    err = bt_nus_init(&nus_cb);
    if (err != 0) {
        printk("[BLE] bt_nus_init failed: %d\n", err);
        return err;
    }

    k_work_init(&command_work, ble_command_work_handler);

    ble_ready = true;
    printk("[BLE] Bluetooth initialized.\n");

    return ble_start_advertising();
}

bool ble_service_is_connected(void)
{
    return current_conn != NULL;
}

int ble_service_send_text(const char *text)
{
    size_t len;
    size_t offset = 0U;
    int last_err = 0;

    if (!ble_ready || current_conn == NULL || text == NULL) {
        return -ENOTCONN;
    }
    if (!nus_notify_enabled) {
        return -EAGAIN;
    }

    k_mutex_lock(&ble_tx_mutex, K_FOREVER);

    len = strlen(text);
    while (offset < len) {
        uint32_t mtu_len = bt_nus_get_mtu(current_conn);
        size_t chunk_max = BLE_TX_CHUNK_FALLBACK_MAX;

        if (mtu_len > 0U) {
            chunk_max = MIN((size_t)mtu_len, (size_t)BLE_TX_CHUNK_FALLBACK_MAX);
        }
        uint16_t chunk_len = (uint16_t)MIN(len - offset, chunk_max);
        int err = 0;

        for (uint32_t attempt = 0U; attempt < BLE_TX_RETRY_COUNT; attempt++) {
            err = bt_nus_send(current_conn,
                              (const uint8_t *)&text[offset],
                              chunk_len);
            if (err == 0) {
                break;
            }

            last_err = err;
            if (err != -ENOMEM && err != -EAGAIN && err != -EBUSY) {
                printk("[BLE] TX failed: %d\n", err);
                k_mutex_unlock(&ble_tx_mutex);
                return err;
            }

            k_msleep(BLE_TX_RETRY_DELAY_MS);
        }

        if (err != 0) {
            printk("[BLE] TX failed after retries: %d\n", last_err);
            k_mutex_unlock(&ble_tx_mutex);
            return last_err;
        }

        offset += chunk_len;
        if (offset < len) {
            k_msleep(BLE_TX_INTER_CHUNK_DELAY_MS);
        }
    }

    k_msleep(BLE_TX_INTER_LINE_DELAY_MS);
    k_mutex_unlock(&ble_tx_mutex);
    return 0;
}

int ble_service_send_sample(uint32_t sample_count, int32_t current_pa)
{
    char line[48];
    int32_t abs_pa = current_pa < 0 ? -current_pa : current_pa;
    int len;

    len = snprintk(line,
                   sizeof(line),
                   "S,%u,%s%d.%03d\r\n",
                   sample_count,
                   current_pa < 0 ? "-" : "",
                   abs_pa / 1000,
                   abs_pa % 1000);
    if (len < 0) {
        return -EINVAL;
    }

    return ble_service_send_text(line);
}
