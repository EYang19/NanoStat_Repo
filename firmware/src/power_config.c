#include <zephyr/kernel.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "power_config.h"

/* ========================================================
 * 1. Devicetree bindings
 * ======================================================== */
static const struct device *ls1_dev  = DEVICE_DT_GET(DT_NODELABEL(npm1300_ldo1));
static const struct device *ldo2_dev = DEVICE_DT_GET(DT_NODELABEL(npm1300_ldo2));
static const struct device *pmic_regulators_dev = DEVICE_DT_GET(DT_NODELABEL(npm1300_regulators));
static const struct gpio_dt_spec slp_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios);

/* Zephyr interrupt callback, timer, and work-queue objects. */
static struct gpio_callback slp_btn_cb_data;
static struct k_timer btn_timer;

/* Two-stage shutdown work items. */
static struct k_work arm_shutdown_work;
static struct k_work execute_shutdown_work;

/* Volatile state is shared between the ISR, timer, and work queue. */
static volatile bool shutdown_armed = false;
volatile bool shutting_down = false;

/* ========================================================
 * 2. Stage one: a three-second hold disables peripherals
 * ======================================================== */
static void arm_shutdown_work_handler(struct k_work *work)
{
    printk("\n[Power] >>> Long Press Confirmed (3s) <<<\n");
    printk("[Power] Sensor power disabled. Blue/Green LEDs OFF.\n");
    
    /* Disabling the sensor rails provides visible feedback to release the button. */
    regulator_disable(ldo2_dev);
    regulator_disable(ls1_dev);
    
    shutdown_armed = true;
    printk("[Power] >>> Please RELEASE the button to completely power off <<<\n");
}

/* ========================================================
 * 3. Stage two: releasing the button cuts the main power
 * ======================================================== */
static void execute_shutdown_work_handler(struct k_work *work)
{
    printk("\n[Power] Entering Ship Mode NOW! System halting...\n");
    
    /* Allow C36 (100 nF) to charge and stabilise the PMIC SHPHLD input. */
    k_msleep(250); 
    
    /* Use the nPM1300 regulator driver ship-mode path.
     * In NCS, register 0x0B00 is hibernate, while ship mode is 0x0B02.
     */
    int err = regulator_parent_ship_mode(pmic_regulators_dev);
    if (err) {
        printk("[Power] ERROR: Failed to enter Ship Mode: %d\n", err);
    }
}

/* ========================================================
 * 4. Timer and button interrupt handling
 * ======================================================== */
static void btn_timer_handler(struct k_timer *timer_id)
{
    k_work_submit(&arm_shutdown_work);
}

static void slp_button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* Ignore subsequent contact or RC transients once shutdown begins. */
    if (shutting_down) {
        return; 
    }

    int state = gpio_pin_get_dt(&slp_button);

    if (state == 1) {
        if (!shutdown_armed) {
            printk("\n[Power] SLP Button PRESSED. Starting 3s timer...\n");
            k_timer_start(&btn_timer, K_MSEC(3000), K_NO_WAIT);
        }
    } 
    else if (state == 0) {
        if (shutdown_armed) {
            shutting_down = true;
            
            /* Disable further button interrupts before entering ship mode. */
            gpio_pin_interrupt_configure_dt(&slp_button, GPIO_INT_DISABLE);
            
            printk("\n[Power] SLP Button RELEASED. Executing final shutdown...\n");
            k_work_submit(&execute_shutdown_work);
        } else {
            /* The button was released before the hold interval elapsed. */
            k_timer_stop(&btn_timer);
            printk("\n[Power] SLP Button RELEASED early. Shutdown aborted.\n");
        }
    }
}

/* ========================================================
 * 5. Initialisation entry point
 * ======================================================== */
int board_power_system_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&slp_button)) {
        printk("[Power] ERROR: SLP Button device not ready!\n");
        return -ENODEV;
    }

    k_work_init(&arm_shutdown_work, arm_shutdown_work_handler);
    k_work_init(&execute_shutdown_work, execute_shutdown_work_handler);
    k_timer_init(&btn_timer, btn_timer_handler, NULL);

    /* Apply the devicetree input configuration and the MCU's internal pull-up. */
    gpio_pin_configure_dt(&slp_button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&slp_button, GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&slp_btn_cb_data, slp_button_isr, BIT(slp_button.pin));
    gpio_add_callback_dt(&slp_button, &slp_btn_cb_data);
    printk("[Power] SLP Button listener active (3s graceful shutdown).\n");

    printk("[Power] Starting analog front-end power sequence...\n");
    if (!device_is_ready(ls1_dev) || !device_is_ready(ldo2_dev) ||
        !device_is_ready(pmic_regulators_dev)) {
        printk("[Power] ERROR: Required PMIC devices not ready!\n");
        return -ENODEV;
    }

    ret = regulator_enable(ls1_dev);
    if (ret < 0) return ret;

    ret = regulator_enable(ldo2_dev);
    if (ret < 0) {
        regulator_disable(ls1_dev);
        return ret;
    }
    k_msleep(20);

    printk("[Power] Power sequence complete. All sensor rails stable.\n");
    return 0;
}
