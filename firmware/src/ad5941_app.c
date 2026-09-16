/*
 * ad5941_app.c
 * AD5941 subsystem bring-up and milestone-2 no-load baseline test.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "ad5940.h"
#include "ad5941_app.h"
#include "ble_service.h"

#define AFE_INT_NODE       DT_ALIAS(afe_int)
#define AFE_FIFO_WATERMARK 10U
#define AFE_FIFO_READ_CHUNK 64U
#define AFE_BASELINE_SAMPLES 32000U
#define AFE_SETTLE_TIME_MS 1000U
#define AFE_EST_SAMPLE_RATE_SPS 150U
#define AFE_RTIA_OHMS      512000.0f
#define AFE_VREF_1P82      1.82f
#define AFE_ADC_PGA        ADCPGA_1P5
#define AFE_ADC_PGA_TEXT   "1.5"
#define AFE_ADC_SINC3_OSR  ADCSINC3OSR_5
#define AFE_ADC_SINC2_OSR  ADCSINC2OSR_1067
#define AFE_SYSCLK_HZ      16000000.0f
#define AFE_ADCCLK_HZ      16000000.0f
#define AFE_LPDAC_12BIT    0x800U
#define AFE_LPDAC_6BIT     0x20U
#define AFE_CA_LPDAC_100MV 0x8BAU
#define AFE_LPTIA_SW       ENUM_AFE_LPTIASW0_NORM
#define AFE_LPTIA_EXT_SW   (AFE_LPTIA_SW | LPTIASW(9))
#define AFE_LPTIA_RF       LPTIARF_OPEN
#define AFE_LPTIA_RF_TEXT  "open"
#define AFE_FIFO_SOURCE    FIFOSRC_SINC2NOTCH
#define AFE_CA_SEQ_SECONDS 5U
#define AFE_CA_SEQ_BUFFER_WORDS 50U
#define AFE_CA_SEQ_WAIT_CLKS ((uint32_t)(16000000UL * AFE_CA_SEQ_SECONDS))
#define AFE_SEQ_FINALIZE_MARGIN_MS 1000U
#define AFE_CA_SEQ_FINALIZE_MS ((AFE_CA_SEQ_SECONDS * 1000U) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_CV_SEQ_BUFFER_WORDS 512U
#define AFE_CV_CODE_LOW   0x746U
#define AFE_CV_CODE_HIGH  0x8BAU
#define AFE_CV_CODE_STEP  10U
#define AFE_CV_DWELL_MS   50U
#define AFE_CV_STEP_COUNT 78U
#define AFE_CV_SEQ_WAIT_CLKS ((uint32_t)(16000UL * AFE_CV_DWELL_MS))
#define AFE_CV_SEQ_FINALIZE_MS ((AFE_CV_STEP_COUNT * AFE_CV_DWELL_MS) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_SEQ_BUFFER_WORDS 1024U
#define AFE_SWV_MAX_STEP_COUNT 512U
#define AFE_SWV_CODE_LOW   0x746U
#define AFE_SWV_CODE_HIGH  0x8BAU
#define AFE_SWV_CODE_STEP  10U
#define AFE_SWV_PULSE_CODE 93U
#define AFE_SWV_HALF_MS    25U
#define AFE_SWV_STEP_COUNT 39U
#define AFE_SWV_SEQ_WAIT_CLKS ((uint32_t)(16000UL * AFE_SWV_HALF_MS))
#define AFE_SWV_SEQ_FINALIZE_MS ((AFE_SWV_STEP_COUNT * AFE_SWV_HALF_MS * 2U) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_SP_STEP_COUNT 37U
#define AFE_SWV_SP_SETTLE_MS  15U
#define AFE_SWV_SP_SAMPLE_MS  10U
#define AFE_SWV_SP_SETTLE_CLKS ((uint32_t)(16000UL * AFE_SWV_SP_SETTLE_MS))
#define AFE_SWV_SP_SAMPLE_CLKS ((uint32_t)(16000UL * AFE_SWV_SP_SAMPLE_MS))
#define AFE_SWV_SP_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * (AFE_SWV_SP_SETTLE_MS + AFE_SWV_SP_SAMPLE_MS) * 2U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_KDM_DIAG_HALF_MS 100U
#define AFE_SWV_KDM_FAST_HALF_MS 25U
#define AFE_SWV_HSTIA_HALF_MS 20U
#define AFE_SWV_KDM_DIAG_WAIT_CLKS ((uint32_t)(16000UL * AFE_SWV_KDM_DIAG_HALF_MS))
#define AFE_SWV_KDM_FAST_WAIT_CLKS ((uint32_t)(16000UL * AFE_SWV_KDM_FAST_HALF_MS))
#define AFE_SWV_HSTIA_WAIT_CLKS ((uint32_t)(16000UL * AFE_SWV_HSTIA_HALF_MS))
#define AFE_SWV_KDM_DIAG_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * AFE_SWV_KDM_DIAG_HALF_MS * 2U) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_KDM_FAST_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * AFE_SWV_KDM_FAST_HALF_MS * 2U) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_HSTIA_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * AFE_SWV_HSTIA_HALF_MS * 2U) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_KDM_DIAG_SINC2_OSR ADCSINC2OSR_178
#define AFE_SWV_KDM_DIAG_SINC2_OSR_TEXT "178"
#define AFE_SWV_KDM_FAST_SINC2_OSR ADCSINC2OSR_22
#define AFE_SWV_KDM_FAST_SINC2_OSR_TEXT "22"
#define AFE_SWV_SP_SINC2_OSR ADCSINC2OSR_178
#define AFE_SWV_SP_SINC2_OSR_TEXT "178"
#define AFE_SWV_SINGLE_POINT_COUNT (AFE_SWV_SP_STEP_COUNT * 2U)
#define AFE_SWV_SLOW_SETTLE_MS 248U
#define AFE_SWV_FAST_SETTLE_MS 23U
#define AFE_SWV_SAMPLE_MS 2U
#define AFE_SWV_SLOW_SETTLE_CLKS ((uint32_t)(16000UL * AFE_SWV_SLOW_SETTLE_MS))
#define AFE_SWV_FAST_SETTLE_CLKS ((uint32_t)(16000UL * AFE_SWV_FAST_SETTLE_MS))
#define AFE_SWV_SAMPLE_CLKS ((uint32_t)(16000UL * AFE_SWV_SAMPLE_MS))
#define AFE_SWV_ENDPOINT_AVG_PCT 5U
#define AFE_SWV_ENDPOINT_GUARD_PCT 2U
#define AFE_SWV_LPTIA_EXT_ENDPOINT_AVG_PCT 20U
#define AFE_SWV_LPTIA_EXT_ENDPOINT_GUARD_PCT 5U
#define AFE_SWV_SLOW_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * ((AFE_SWV_SLOW_SETTLE_MS + AFE_SWV_SAMPLE_MS) * 2U)) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_LOWZ_FINALIZE_MS \
    ((AFE_SWV_SP_STEP_COUNT * ((AFE_SWV_FAST_SETTLE_MS + AFE_SWV_SAMPLE_MS) * 2U)) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_RTIA_LOWZ_OHMS 20000.0f
#define AFE_HSTIA_RTIA_OHMS 160000.0f
#define AFE_HSTIA_EXT_160K_RTIA_OHMS 160000.0f
#define AFE_HSTIA_EXT_330K_RTIA_OHMS 330000.0f
#define AFE_HSTIA_EXT_680K_RTIA_OHMS 680000.0f
#define AFE_HSTIA_CTIA_CODE_1PF 0x00U
#define AFE_HSTIA_CTIA_CODE_4PF 0x02U
#define AFE_HSTIA_CTIA_CODE_8PF 0x04U
#define AFE_HSTIA_CTIA_CODE_16PF 0x08U
#define AFE_HSTIA_CTIA_CODE_32PF 0x10U
#define AFE_HSTIA_CTIA_PF 16U
#define AFE_HSTIA_EXT_AIN1_RTIA_OHMS 2000000.0f
#define AFE_HSTIA_EXT_AIN1_CTIA_CODE AFE_HSTIA_CTIA_CODE_4PF
#define AFE_HSTIA_EXT_AIN1_CTIA_PF 4U
#define AFE_SWV_EXT_HSTIA_PULSE_CODE 9U
#define AFE_SWV_EXT_HSTIA_CODE_LOW 0x7DEU
#define AFE_SWV_EXT_HSTIA_CODE_STEP 2U
#define AFE_LPTIA_EXT_RTIA_OHMS 2000000.0f
#define AFE_LPTIA_512K_PARALLEL_2M_OHMS 407643.0f
#define AFE_LPTIA_160K_PARALLEL_2M_OHMS 148148.0f
#define AFE_LPTIA_EXT_BOOST_MODE LPAMPPWR_BOOST2
#define AFE_SWV_LPTIA_EXT_PULSE_CODE 9U
#define AFE_SWV_LPTIA_EXT_CODE_LOW 0x7DEU
#define AFE_SWV_LPTIA_EXT_CODE_STEP 2U
#define AFE_SWV_LPTIA_EXT_CODE_HIGH \
    (AFE_SWV_LPTIA_EXT_CODE_LOW + ((AFE_SWV_SP_STEP_COUNT - 1U) * AFE_SWV_LPTIA_EXT_CODE_STEP))
#define AFE_SWV_LPTIA_EXT_150_HALF_US 3333U
#define AFE_SWV_LPTIA_EXT_160_HALF_US 3125U
#define AFE_SWV_LPTIA_EXT_210_HALF_US 2381U
#define AFE_SWV_LPTIA_EXT_150_WAIT_CLKS ((uint32_t)(16UL * AFE_SWV_LPTIA_EXT_150_HALF_US))
#define AFE_SWV_LPTIA_EXT_160_WAIT_CLKS ((uint32_t)(16UL * AFE_SWV_LPTIA_EXT_160_HALF_US))
#define AFE_SWV_LPTIA_EXT_210_WAIT_CLKS ((uint32_t)(16UL * AFE_SWV_LPTIA_EXT_210_HALF_US))
#define AFE_SWV_LPTIA_EXT_150_FINALIZE_MS \
    (((AFE_SWV_SP_STEP_COUNT * AFE_SWV_LPTIA_EXT_150_HALF_US * 2U) / 1000U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_LPTIA_EXT_160_FINALIZE_MS \
    (((AFE_SWV_SP_STEP_COUNT * AFE_SWV_LPTIA_EXT_160_HALF_US * 2U) / 1000U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_LPTIA_EXT_210_FINALIZE_MS \
    (((AFE_SWV_SP_STEP_COUNT * AFE_SWV_LPTIA_EXT_210_HALF_US * 2U) / 1000U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_KDM_PAPER_PULSE_CODE 65U
#define AFE_SWV_KDM_PAPER_CODE_LOW 0x4BAU
#define AFE_SWV_KDM_PAPER_CODE_HIGH 0x800U
#define AFE_SWV_KDM_PAPER_CODE_STEP 10U
#define AFE_SWV_KDM_PAPER_STEP_COUNT \
    (((AFE_SWV_KDM_PAPER_CODE_HIGH - AFE_SWV_KDM_PAPER_CODE_LOW) / AFE_SWV_KDM_PAPER_CODE_STEP) + 1U)
#define AFE_SWV_KDM_PAPER_150_HALF_US 3333U
#define AFE_SWV_KDM_PAPER_10_HALF_US 50000U
#define AFE_SWV_KDM_PAPER_150_WAIT_CLKS ((uint32_t)(16UL * AFE_SWV_KDM_PAPER_150_HALF_US))
#define AFE_SWV_KDM_PAPER_10_WAIT_CLKS ((uint32_t)(16UL * AFE_SWV_KDM_PAPER_10_HALF_US))
#define AFE_SWV_KDM_PAPER_150_FINALIZE_MS \
    (((AFE_SWV_KDM_PAPER_STEP_COUNT * AFE_SWV_KDM_PAPER_150_HALF_US * 2U) / 1000U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_SWV_KDM_PAPER_10_FINALIZE_MS \
    (((AFE_SWV_KDM_PAPER_STEP_COUNT * AFE_SWV_KDM_PAPER_10_HALF_US * 2U) / 1000U) + \
     AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_BLANK_PEAK_PULSE_CODE 65U
#define AFE_BLANK_PEAK_CODE_LOW 0x800U
#define AFE_BLANK_PEAK_CODE_HIGH 0xB46U
#define AFE_BLANK_PEAK_CODE_STEP 2U
#define AFE_BLANK_PEAK_5MV_CODE_STEP 10U
#define AFE_BLANK_PEAK_2MV_CODE_STEP 4U
#define AFE_BLANK_PEAK_1MV_CODE_STEP 2U
#define AFE_BLANK_PEAK_STEP_COUNT \
    ((((AFE_BLANK_PEAK_CODE_HIGH - AFE_BLANK_PEAK_CODE_LOW) + AFE_BLANK_PEAK_CODE_STEP - 1U) / \
      AFE_BLANK_PEAK_CODE_STEP) + 1U)
#define AFE_BLANK_PEAK_SEGMENT_MAX_STEPS 150U
#define AFE_BLANK_PEAK_SEGMENT_MARGIN_MS 50U
#define AFE_BLANK_PEAK_SEARCH_MIN_UV (-400000)
#define AFE_BLANK_PEAK_SEARCH_MAX_UV (-80000)
#define AFE_BLANK_PEAK_QUIET_MS 3000U
#define AFE_BLANK_PEAK_QUIET_CODE AFE_BLANK_PEAK_CODE_HIGH
#define AFE_STEP_PRE_MS 20U
#define AFE_STEP_POST_MS 50U
#define AFE_STEP_PRE_CLKS ((uint32_t)(16000UL * AFE_STEP_PRE_MS))
#define AFE_STEP_POST_CLKS ((uint32_t)(16000UL * AFE_STEP_POST_MS))
#define AFE_STEP_FINALIZE_MS ((AFE_STEP_PRE_MS + AFE_STEP_POST_MS) + AFE_SEQ_FINALIZE_MARGIN_MS)
#define AFE_STEP_LPDAC_100MV AFE_CA_LPDAC_100MV
#define AFE_LPDAC_ZERO_CODE 0x800U
#define AFE_LPDAC_LSB_UV_X10 5372
#define AFE_HIGHZ_TEST_START_UV (-400000)
#define AFE_HIGHZ_TEST_HOLD_UV (-200000)
#define AFE_HIGHZ_TEST_STEP_UV 5000
#define AFE_HIGHZ_TEST_POINTS (((-AFE_HIGHZ_TEST_START_UV + AFE_HIGHZ_TEST_HOLD_UV) / AFE_HIGHZ_TEST_STEP_UV) + 1U)
#define AFE_HIGHZ_TEST_DWELL_MS 100U
#define AFE_HIGHZ_TEST_WAIT_CLKS ((uint32_t)(16000UL * AFE_HIGHZ_TEST_DWELL_MS))
#define AFE_HIGHZ_TEST_FINALIZE_MS ((AFE_HIGHZ_TEST_POINTS * AFE_HIGHZ_TEST_DWELL_MS) + 250U)
#define AFE_KDM_WORKQ_STACK_SIZE 6144
#define AFE_KDM_WORKQ_PRIORITY 5
#define AFE_REPEAT_SWV_RUNS 5U
#define AFE_REPEAT_SWV_INTERVAL_MS 10000U
#define AFE_DUMMY_REPEAT_SWV_RUNS 10U
#define AFE_DUMMY_REPEAT_SWV_INTERVAL_MS 5000U
#define AFE_SWV_DUMMY_RESISTOR_OHMS 1100000.0f
#define AFE_SWV_PHASE_MIN_ABS_PCT 50U
#define AFE_OFFSET_DIAG_CAPTURE_MS 2000U
#define AFE_OFFSET_DIAG_SETTLE_MS 300U

BUILD_ASSERT(DT_NODE_EXISTS(AFE_INT_NODE), "Missing devicetree alias: afe-int");
BUILD_ASSERT(AFE_SWV_KDM_PAPER_STEP_COUNT <= AFE_SWV_MAX_STEP_COUNT,
             "KDM paper SWV step count exceeds endpoint extraction buffer");
BUILD_ASSERT(AFE_BLANK_PEAK_STEP_COUNT <= AFE_SWV_MAX_STEP_COUNT,
             "Blank peak SWV step count exceeds endpoint extraction buffer");
BUILD_ASSERT((AFE_BLANK_PEAK_SEGMENT_MAX_STEPS * 4U + 64U) <= AFE_SWV_SEQ_BUFFER_WORDS,
             "Blank peak SWV segment exceeds sequencer generation buffer");

extern void AD5940_MCUResourceDeInit(void);

static const struct gpio_dt_spec afe_int =
    GPIO_DT_SPEC_GET(AFE_INT_NODE, gpios);

struct offset_diag_stats {
    uint32_t samples;
    uint32_t raw_avg;
    int32_t avg_pa;
    int32_t std_pa;
    int32_t min_pa;
    int32_t max_pa;
};

struct swv_profile_cfg {
    enum ad5941_swv_profile profile;
    const char *token;
    const char *label;
    uint32_t code_step;
    bool segmented;
};

struct dummy_delta_stats {
    uint32_t count;
    int32_t mean_pa;
    int32_t std_pa;
};

static const struct swv_profile_cfg swv_profiles[] = {
    {
        .profile = AD5941_SWV_PROFILE_5MV_FULL,
        .token = "5MV",
        .label = "5.37mV full",
        .code_step = AFE_BLANK_PEAK_5MV_CODE_STEP,
        .segmented = false,
    },
    {
        .profile = AD5941_SWV_PROFILE_2MV_FULL,
        .token = "2MV",
        .label = "2.15mV full",
        .code_step = AFE_BLANK_PEAK_2MV_CODE_STEP,
        .segmented = false,
    },
    {
        .profile = AD5941_SWV_PROFILE_1MV_SEGMENTED,
        .token = "1MVSEG",
        .label = "1.07mV segmented",
        .code_step = AFE_BLANK_PEAK_1MV_CODE_STEP,
        .segmented = true,
    },
};

static struct gpio_callback afe_int_cb;
static struct k_work afe_fifo_work;
static struct k_work_delayable afe_seq_finish_work;
static struct k_work_delayable afe_kdm_finish_work;
static struct k_work_delayable afe_repeat_work;
static struct k_work_delayable afe_highz_validation_done_work;
static struct k_work_q afe_kdm_work_q;
K_THREAD_STACK_DEFINE(afe_kdm_work_q_stack, AFE_KDM_WORKQ_STACK_SIZE);
static atomic_t afe_fifo_work_pending;
static atomic_t afe_measurement_busy;
static int32_t afe_baseline_pa[AFE_BASELINE_SAMPLES];
static uint32_t afe_baseline_count;
static int32_t afe_latest_current_pa;
static const char *afe_capture_name = "Baseline";
static bool afe_capture_is_cv;
static bool afe_capture_is_swv_kdm;
static bool afe_capture_is_swv_single_point;
static bool afe_capture_is_blank_peak;
static bool afe_blank_peak_emit_smoothing;
static bool afe_blank_peak_invert_current_sign;
static bool afe_blank_peak_segmented_active;
static uint32_t afe_blank_peak_segment_next_step;
static uint32_t afe_blank_peak_segment_half_us;
static uint32_t afe_blank_peak_segment_half_wait_clks;
static uint32_t afe_blank_peak_segment_max_steps = AFE_BLANK_PEAK_SEGMENT_MAX_STEPS;
static bool afe_blank_peak_insert_seq_highz_tail = true;
static bool afe_capture_is_dummy_swv;
static enum ad5941_swv_profile afe_current_swv_profile = AD5941_SWV_PROFILE_1MV_SEGMENTED;
static uint32_t afe_dummy_resistor_ohms = (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS;
static uint32_t afe_dummy_scan_ms;
static bool afe_capture_is_step_response;
static const char *afe_step_response_setup_text = "not configured";
static uint32_t afe_swv_kdm_half_ms = AFE_SWV_KDM_DIAG_HALF_MS;
static uint32_t afe_swv_kdm_step_ms = AFE_SWV_KDM_DIAG_HALF_MS * 2U;
static uint32_t afe_swv_kdm_max_lag_divisor = 1U;
static uint32_t afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
static uint32_t afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
static uint32_t afe_swv_sp_code_low = AFE_SWV_CODE_LOW;
static uint32_t afe_swv_sp_code_high = AFE_SWV_CODE_HIGH;
static uint32_t afe_swv_sp_code_step = AFE_SWV_CODE_STEP;
static uint32_t afe_swv_sp_step_count = AFE_SWV_SP_STEP_COUNT;
static bool afe_swv_sp_descending_codes;
static bool afe_swv_phase_expectation_enabled;
static int32_t afe_swv_expected_delta_pa;
static int32_t afe_swv_last_phase_shift;
static uint32_t afe_swv_last_phase_half_samples;
static uint32_t afe_swv_last_phase_max_shift;
static int32_t afe_swv_last_phase_mean_delta_pa;
static int64_t afe_swv_last_phase_smoothness_pa;
static float afe_active_rtia_ohms = AFE_RTIA_OHMS;
static bool afe_kdm_paper_active;
static uint8_t afe_kdm_paper_stage;
static int32_t afe_kdm_signal_on_peak_pa;
static int32_t afe_kdm_signal_off_peak_pa;
static int32_t afe_kdm_signal_on_mean_pa;
static int32_t afe_kdm_signal_off_mean_pa;
static bool afe_repeat_active;
static bool afe_repeat_is_dummy;
static uint8_t afe_repeat_current_run;
static uint8_t afe_repeat_total_runs;
static uint32_t afe_repeat_interval_ms = AFE_REPEAT_SWV_INTERVAL_MS;
static enum ad5941_swv_profile afe_repeat_dummy_profile;
static uint32_t afe_repeat_dummy_frequency_hz;
static enum ad5941_blank_peak_tia_path afe_repeat_dummy_tia_path;
static uint32_t afe_repeat_dummy_ohms;
static uint32_t ca_seq_gen_buffer[AFE_CA_SEQ_BUFFER_WORDS];
static uint32_t cv_seq_gen_buffer[AFE_CV_SEQ_BUFFER_WORDS];
static uint32_t swv_seq_gen_buffer[AFE_SWV_SEQ_BUFFER_WORDS];

static void ad5941_hs_switch_matrix_open(void);
static void ad5941_lptia_default_frontend_config(void);
static void ad5941_lptia_external_frontend_prepare(bool boost_enable);
static void ad5941_enter_safe_idle(const char *source);
static void ad5941_seq_insert_safe_idle_tail(void);
static int ad5941_blank_peak_start_next_segment(void);
static int ad5941_hstia_mixed_frontend_config(void);
static int ad5941_hstia_external_frontend_config(enum ad5941_blank_peak_tia_path tia_path,
                                                 float rtia_ohms);
static void ad5941_hstia_external_switch_matrix_config(enum ad5941_blank_peak_tia_path tia_path);
static int ad5941_seq_swv_blank_peak_start(uint32_t frequency_hz,
                                           bool emit_smoothing,
                                           enum ad5941_blank_peak_tia_path tia_path,
                                           enum ad5941_swv_profile profile,
                                           bool dummy_mode,
                                           uint32_t dummy_ohms);
static int ad5941_seq_swv_blank_peak_start_common(uint32_t frequency_hz,
                                                  bool emit_smoothing,
                                                  enum ad5941_blank_peak_tia_path tia_path,
                                                  enum ad5941_swv_profile profile,
                                                  bool dummy_mode,
                                                  uint32_t dummy_ohms,
                                                  bool insert_seq_highz_tail,
                                                  bool finish_highz);
static bool afe_finish_highz_enabled = true;
static bool afe_highz_validation_active;
#if ENABLE_LINEARITY_TEST
static uint8_t linearity_test_mode;
#endif

static int ad5941_seq_swv_kdm_paper_scan_start(uint8_t stage);
int App_SeqSWV_LPTIAExternal2M_150Hz_Test_Start(void);
int App_SeqSWV_HSTIAExternalAIN1_Test_Start(void);

static void afe_repeat_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!afe_repeat_active || afe_repeat_current_run >= afe_repeat_total_runs) {
        return;
    }

    afe_repeat_current_run++;
    if (afe_repeat_is_dummy) {
        printk("\n[REPEAT] Starting dummy run %u/%u.\n",
               afe_repeat_current_run, afe_repeat_total_runs);
        if (ble_service_is_connected()) {
            char ble_line[48];

            snprintk(ble_line,
                     sizeof(ble_line),
                     "EVT,REPEAT,RUN,%u,%u\r\n",
                     afe_repeat_current_run,
                     afe_repeat_total_runs);
            (void)ble_service_send_text(ble_line);
        }
        if (App_SeqSWV_DummyCell_Test_Start(afe_repeat_dummy_profile,
                                            afe_repeat_dummy_frequency_hz,
                                            afe_repeat_dummy_tia_path,
                                            afe_repeat_dummy_ohms) != 0) {
            printk("[REPEAT] ERROR: failed to start dummy run %u/%u. Aborting repeat test.\n",
                   afe_repeat_current_run, afe_repeat_total_runs);
            afe_repeat_active = false;
        }
        return;
    }

    printk("\n[REPEAT] Starting run %u/%u: HSTIA external AIN1 2M, CTIA=4pF, 150Hz SWV.\n",
           afe_repeat_current_run, afe_repeat_total_runs);
    if (App_SeqSWV_HSTIAExternalAIN1_Test_Start() != 0) {
        printk("[REPEAT] ERROR: failed to start run %u/%u. Aborting repeat test.\n",
               afe_repeat_current_run, afe_repeat_total_runs);
        afe_repeat_active = false;
    }
}

static void afe_highz_validation_done_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!afe_highz_validation_active) {
        return;
    }

    printk("[HIGHZ-TEST] Sweep complete. Holding E_WE-RE at approximately -200 mV; press High-Z to release.\n");
    if (ble_service_is_connected()) {
        /* Keep the state event short so it remains robust with small NUS MTUs. */
        (void)ble_service_send_text("EVT,HZ,HOLD\r\n");
    }
}

static void ad5941_schedule_seq_finish(uint32_t finalize_ms)
{
    int ret = k_work_schedule_for_queue(&afe_kdm_work_q,
                                        &afe_seq_finish_work,
                                        K_MSEC(finalize_ms));

    if (ret >= 0) {
        atomic_set(&afe_measurement_busy, 1);
    }
    printk("[AD5941] SEQ finish work scheduled in %u ms, ret=%d.\n",
           finalize_ms, ret);
}

static void ad5941_fifo_reset(void)
{
    AD5940_FIFOCtrlS(AFE_FIFO_SOURCE, bFALSE);
    AD5940_FIFOThrshSet(AFE_FIFO_WATERMARK);
    AD5940_FIFOCtrlS(AFE_FIFO_SOURCE, bTRUE);
    AD5940_INTCClrFlag(AFEINTSRC_DATAFIFOTHRESH);
}

static void ad5941_hw_reset(void)
{
    AD5940_RstClr();
    k_msleep(10);
    AD5940_RstSet();
    k_msleep(20);
}

static int32_t fifo_word_to_current_pa(uint32_t raw)
{
    uint16_t adc_code = (uint16_t)(raw & 0xFFFFU);
    float volts = AD5940_ADCCode2Volt(adc_code, AFE_ADC_PGA, AFE_VREF_1P82);
    float current_pa = volts * (1000000000000.0f / afe_active_rtia_ohms);

    return (int32_t)(current_pa + (current_pa >= 0.0f ? 0.5f : -0.5f));
}

static uint32_t isqrt64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t)result;
}

static void print_current_na(const char *label, int32_t current_pa)
{
    int32_t abs_pa = current_pa < 0 ? -current_pa : current_pa;
    const char *sign = current_pa < 0 ? "-" : "";

    printk("%s%s%d.%03d nA", label, sign, abs_pa / 1000, abs_pa % 1000);
}

static uint16_t cv_dac_code_for_sample(uint32_t sample_index)
{
    uint32_t elapsed_ms = (sample_index * 1000U) / AFE_EST_SAMPLE_RATE_SPS;
    uint32_t step_index = elapsed_ms / AFE_CV_DWELL_MS;

    if (step_index < 38U) {
        return (uint16_t)(AFE_CV_CODE_LOW + (step_index * AFE_CV_CODE_STEP));
    }
    if (step_index == 38U || step_index == 39U) {
        return AFE_CV_CODE_HIGH;
    }
    if (step_index < (AFE_CV_STEP_COUNT - 1U)) {
        return (uint16_t)(AFE_CV_CODE_HIGH -
                          ((step_index - 39U) * AFE_CV_CODE_STEP));
    }

    return AFE_CV_CODE_LOW;
}

static int32_t lpdac_code_to_polarization_uv(uint16_t code)
{
    int32_t delta_code = (int32_t)code - (int32_t)AFE_LPDAC_ZERO_CODE;

    return (delta_code * AFE_LPDAC_LSB_UV_X10) / 10;
}

static int32_t lpdac_code_to_we_re_uv(uint16_t code)
{
    /* LP loop: WE(SE0) is held at VZERO, RE follows VBIAS through LPPA. */
    return -lpdac_code_to_polarization_uv(code);
}

static uint32_t swv_single_point_count(void)
{
    return afe_swv_sp_step_count * 2U;
}

static uint32_t swv_sp_base_code_for_step(uint32_t step)
{
    if (afe_swv_sp_descending_codes) {
        uint32_t offset = step * afe_swv_sp_code_step;

        if (offset >= (afe_swv_sp_code_high - afe_swv_sp_code_low)) {
            return afe_swv_sp_code_low;
        }

        return afe_swv_sp_code_high - offset;
    }

    uint32_t code = afe_swv_sp_code_low + (step * afe_swv_sp_code_step);

    return MIN(code, afe_swv_sp_code_high);
}

static void print_voltage_mv(const char *label, int32_t voltage_uv)
{
    int32_t abs_uv = voltage_uv < 0 ? -voltage_uv : voltage_uv;
    const char *sign = voltage_uv < 0 ? "-" : "";

    printk("%s%s%d.%03d mV", label, sign, abs_uv / 1000, abs_uv % 1000);
}

static void print_csv_decimal3(int32_t milli_units)
{
    int32_t abs_value = milli_units < 0 ? -milli_units : milli_units;

    printk("%s%d.%03d", milli_units < 0 ? "-" : "", abs_value / 1000, abs_value % 1000);
}

static int format_decimal3(char *buf, size_t buf_size, int32_t milli_units)
{
    int32_t abs_value = milli_units < 0 ? -milli_units : milli_units;

    return snprintk(buf,
                    buf_size,
                    "%s%d.%03d",
                    milli_units < 0 ? "-" : "",
                    abs_value / 1000,
                    abs_value % 1000);
}

static const struct swv_profile_cfg *swv_profile_get(enum ad5941_swv_profile profile)
{
    for (size_t i = 0; i < ARRAY_SIZE(swv_profiles); i++) {
        if (swv_profiles[i].profile == profile) {
            return &swv_profiles[i];
        }
    }

    return &swv_profiles[ARRAY_SIZE(swv_profiles) - 1U];
}

static uint32_t swv_profile_step_count(uint32_t code_step)
{
    uint32_t span = AFE_BLANK_PEAK_CODE_HIGH - AFE_BLANK_PEAK_CODE_LOW;

    if (code_step == 0U) {
        return 0U;
    }

    return ((span + code_step - 1U) / code_step) + 1U;
}

static int32_t swv_profile_step_uv(uint32_t code_step)
{
    return (int32_t)((code_step * AFE_LPDAC_LSB_UV_X10) / 10U);
}

static int32_t swv_pulse_uv(void)
{
    return (int32_t)((AFE_BLANK_PEAK_PULSE_CODE * AFE_LPDAC_LSB_UV_X10) / 10U);
}

static int32_t swv_dummy_expected_delta_pa(uint32_t dummy_ohms)
{
    float delta_uv;
    float delta_pa;

    if (dummy_ohms == 0U) {
        return 0;
    }

    delta_uv = (float)(2U * AFE_BLANK_PEAK_PULSE_CODE) *
               ((float)AFE_LPDAC_LSB_UV_X10 / 10.0f);
    delta_pa = (delta_uv * 1000000.0f) / (float)dummy_ohms;

    return (int32_t)(delta_pa + 0.5f);
}

static uint32_t swv_kdm_sample_index_at_ms(uint32_t time_ms, uint32_t total_ms)
{
    uint32_t scaled;

    if (afe_baseline_count == 0U) {
        return 0U;
    }
    if (time_ms >= total_ms) {
        return afe_baseline_count - 1U;
    }

    scaled = (time_ms * afe_baseline_count) / total_ms;
    if (scaled == 0U) {
        return 0U;
    }

    return scaled - 1U;
}

static uint32_t swv_kdm_apply_lag(uint32_t index, uint32_t lag)
{
    uint32_t lagged = index + lag;

    if (afe_baseline_count == 0U) {
        return 0U;
    }

    return MIN(lagged, afe_baseline_count - 1U);
}

static void print_swv_kdm_extract(void)
{
    uint32_t total_ms = AFE_SWV_SP_STEP_COUNT * afe_swv_kdm_step_ms;
    uint32_t half_samples;
    uint32_t max_lag;
    uint32_t best_lag = 0;
    uint64_t best_score = 0;

    printk("[SWV-KDM] Extracting end-of-pulse differential current from %u raw samples.\n",
           afe_baseline_count);
    printk("[SWV-KDM] Adaptive time map: %u samples over %u ms.\n",
           afe_baseline_count, total_ms);

    half_samples = afe_baseline_count / (AFE_SWV_SP_STEP_COUNT * 2U);
    if (half_samples == 0U) {
        printk("[SWV-KDM] Not enough samples for phase extraction.\n");
        return;
    }

    max_lag = half_samples / afe_swv_kdm_max_lag_divisor;
    if (max_lag == 0U) {
        max_lag = 1U;
    }
    max_lag = MIN(max_lag, half_samples);
    printk("[SWV-KDM] Phase search limited to %u/%u samples.\n", max_lag, half_samples);

    for (uint32_t lag = 0; lag <= max_lag; lag++) {
        uint64_t score = 0;
        uint32_t valid_steps = 0;

        for (uint32_t step = 2U; step < AFE_SWV_SP_STEP_COUNT; step++) {
            uint32_t forward_end_ms = (step * afe_swv_kdm_step_ms) + afe_swv_kdm_half_ms;
            uint32_t reverse_end_ms = (step + 1U) * afe_swv_kdm_step_ms;
            uint32_t forward_index = swv_kdm_apply_lag(
                swv_kdm_sample_index_at_ms(forward_end_ms, total_ms), lag);
            uint32_t reverse_index = swv_kdm_apply_lag(
                swv_kdm_sample_index_at_ms(reverse_end_ms, total_ms), lag);
            int32_t delta_pa;

            if (forward_index >= afe_baseline_count || reverse_index >= afe_baseline_count) {
                continue;
            }

            delta_pa = afe_baseline_pa[forward_index] - afe_baseline_pa[reverse_index];
            score += (uint32_t)(delta_pa < 0 ? -delta_pa : delta_pa);
            valid_steps++;
        }

        if (valid_steps > 0U) {
            score /= valid_steps;
            if (score > best_score) {
                best_score = score;
                best_lag = lag;
            }
        }
    }

    printk("[SWV-KDM] Auto phase compensation: lag=%u samples, score=%u.%03u nA.\n",
           best_lag, (uint32_t)(best_score / 1000U), (uint32_t)(best_score % 1000U));

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_CODE_LOW + (step * AFE_SWV_CODE_STEP);
        uint32_t forward_end_ms = (step * afe_swv_kdm_step_ms) + afe_swv_kdm_half_ms;
        uint32_t reverse_end_ms = (step + 1U) * afe_swv_kdm_step_ms;
        uint32_t forward_index = swv_kdm_apply_lag(
            swv_kdm_sample_index_at_ms(forward_end_ms, total_ms), best_lag);
        uint32_t reverse_index = swv_kdm_apply_lag(
            swv_kdm_sample_index_at_ms(reverse_end_ms, total_ms), best_lag);
        int32_t voltage_uv = lpdac_code_to_polarization_uv((uint16_t)base_code);
        int32_t delta_pa;

        if (forward_index >= afe_baseline_count || reverse_index >= afe_baseline_count) {
            printk("[SWV-KDM] Step %02u: insufficient samples (f=%u, r=%u, total=%u).\n",
                   step + 1U, forward_index, reverse_index, afe_baseline_count);
            continue;
        }

        delta_pa = afe_baseline_pa[forward_index] - afe_baseline_pa[reverse_index];

        printk("[SWV-KDM] Step %02u: ", step + 1U);
        print_voltage_mv("Vbase=", voltage_uv);
        printk(" f_idx=%u r_idx=%u ", forward_index, reverse_index);
        print_current_na("I_forward=", afe_baseline_pa[forward_index]);
        print_current_na(", I_reverse=", afe_baseline_pa[reverse_index]);
        print_current_na(", Delta_I=", delta_pa);
        printk("\n");
    }
}

static int32_t swv_endpoint_average_for_slot(uint32_t slot,
                                             int32_t phase_shift,
                                             uint32_t *first_index,
                                             uint32_t *last_index)
{
    int64_t shifted_start;
    int64_t shifted_end;
    uint32_t start;
    uint32_t end;
    uint32_t width;
    uint32_t guard;
    uint32_t count;
    int64_t sum_pa = 0;

    if (afe_baseline_count == 0U) {
        if (first_index != NULL) {
            *first_index = 0U;
        }
        if (last_index != NULL) {
            *last_index = 0U;
        }
        return 0;
    }

    shifted_start = (int64_t)((slot * afe_baseline_count) / swv_single_point_count()) +
                    (int64_t)phase_shift;
    shifted_end = (int64_t)(((slot + 1U) * afe_baseline_count) / swv_single_point_count()) +
                  (int64_t)phase_shift;

    if (shifted_start < 0) {
        shifted_start = 0;
    }
    if (shifted_end < 1) {
        shifted_end = 1;
    }
    if (shifted_start >= (int64_t)afe_baseline_count) {
        shifted_start = (int64_t)afe_baseline_count - 1;
    }
    if (shifted_end > (int64_t)afe_baseline_count) {
        shifted_end = (int64_t)afe_baseline_count;
    }

    start = (uint32_t)shifted_start;
    end = (uint32_t)shifted_end;
    if (end <= start) {
        end = MIN(start + 1U, afe_baseline_count);
    }

    width = end - start;
    guard = (width * afe_swv_endpoint_guard_pct) / 100U;
    if (afe_swv_endpoint_guard_pct > 0U && guard == 0U && width > 4U) {
        guard = 1U;
    }
    guard = MIN(guard, width - 1U);
    end -= guard;
    width = end - start;

    count = (width * afe_swv_endpoint_avg_pct) / 100U;
    if (count == 0U) {
        count = MIN(3U, width);
    }
    count = CLAMP(count, 1U, width);
    start = end - count;

    if (first_index != NULL) {
        *first_index = start;
    }
    if (last_index != NULL) {
        *last_index = end - 1U;
    }

    for (uint32_t i = start; i < end; i++) {
        sum_pa += afe_baseline_pa[i];
    }

    return (int32_t)(sum_pa / (int64_t)count);
}

static int32_t swv_expected_dummy_delta_pa(uint32_t pulse_code, float dummy_ohms)
{
    float delta_uv = (float)(2U * pulse_code) * ((float)AFE_LPDAC_LSB_UV_X10 / 10.0f);
    float delta_pa = (delta_uv * 1000000.0f) / dummy_ohms;

    return -(int32_t)(delta_pa + 0.5f);
}

static void swv_sp_clear_phase_expectation(void)
{
    afe_swv_phase_expectation_enabled = false;
    afe_swv_expected_delta_pa = 0;
}

static void swv_sp_set_dummy_phase_expectation(uint32_t pulse_code, float dummy_ohms)
{
    afe_swv_expected_delta_pa = swv_expected_dummy_delta_pa(pulse_code, dummy_ohms);
    afe_swv_phase_expectation_enabled = true;

    print_current_na("[SWV-SP] Phase guard enabled: expected dummy Delta_I=",
                     afe_swv_expected_delta_pa);
    printk(" for %u ohm dummy.\n", (uint32_t)dummy_ohms);
}

static void swv_sp_phase_stats(int32_t phase_shift,
                               int64_t *smoothness_out,
                               int32_t *mean_delta_pa_out,
                               uint32_t *count_out)
{
    int64_t smoothness = 0;
    int64_t sum_pa = 0;
    int32_t previous_delta = 0;
    bool has_previous = false;
    uint32_t count = 0;

    for (uint32_t step = 1U; step < (afe_swv_sp_step_count - 1U); step++) {
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        int32_t forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift, NULL, NULL);
        int32_t reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift, NULL, NULL);
        int32_t delta_pa = forward_pa - reverse_pa;

        if (has_previous) {
            int32_t diff = delta_pa - previous_delta;

            smoothness += diff < 0 ? -(int64_t)diff : (int64_t)diff;
        }

        sum_pa += delta_pa;
        count++;
        previous_delta = delta_pa;
        has_previous = true;
    }

    if (smoothness_out != NULL) {
        *smoothness_out = smoothness;
    }
    if (mean_delta_pa_out != NULL) {
        *mean_delta_pa_out = count > 0U ? (int32_t)(sum_pa / (int64_t)count) : 0;
    }
    if (count_out != NULL) {
        *count_out = count;
    }
}

static int64_t swv_sp_phase_score(int64_t smoothness, int32_t mean_delta_pa)
{
    int64_t score = smoothness;

    if (afe_swv_phase_expectation_enabled) {
        int64_t expected = afe_swv_expected_delta_pa;
        int64_t expected_abs = expected < 0 ? -expected : expected;
        int64_t mean_abs = mean_delta_pa < 0 ? -(int64_t)mean_delta_pa :
                           (int64_t)mean_delta_pa;
        int64_t magnitude_error = mean_abs > expected_abs ?
                                  mean_abs - expected_abs :
                                  expected_abs - mean_abs;
        int64_t min_abs = (expected_abs * AFE_SWV_PHASE_MIN_ABS_PCT) / 100LL;

        score += magnitude_error * (int64_t)afe_swv_sp_step_count * 4LL;

        if ((expected < 0 && mean_delta_pa >= 0) ||
            (expected > 0 && mean_delta_pa <= 0)) {
            score += expected_abs * (int64_t)afe_swv_sp_step_count * 20LL;
        }
        if (mean_abs < min_abs) {
            score += (min_abs - mean_abs) * (int64_t)afe_swv_sp_step_count * 20LL;
        }
    }

    return score;
}

static int32_t swv_sp_find_best_phase_shift(void)
{
    uint32_t half_samples = afe_baseline_count / swv_single_point_count();
    uint32_t max_shift_u = half_samples / 4U;
    int32_t best_shift = 0;
    int64_t best_score = INT64_MAX;
    int64_t best_smoothness = 0;
    int32_t best_mean_delta_pa = 0;

    afe_swv_last_phase_shift = 0;
    afe_swv_last_phase_half_samples = half_samples;
    afe_swv_last_phase_max_shift = max_shift_u;
    afe_swv_last_phase_mean_delta_pa = 0;
    afe_swv_last_phase_smoothness_pa = 0;

    if (max_shift_u == 0U) {
        return 0;
    }

    if (afe_swv_phase_expectation_enabled) {
        /*
         * Dummy-cell validation has a known ohmic Delta_I. Search almost the
         * whole half-period so ADC/filter group delay cannot hide a valid
         * response behind the normal narrow smoothness window.
         */
        max_shift_u = half_samples > 2U ? half_samples - 2U : half_samples;
        afe_swv_last_phase_max_shift = max_shift_u;
    }

    for (int32_t shift = -(int32_t)max_shift_u; shift <= (int32_t)max_shift_u; shift++) {
        int64_t smoothness;
        int32_t mean_delta_pa;
        uint32_t count;
        int64_t score;

        swv_sp_phase_stats(shift, &smoothness, &mean_delta_pa, &count);
        if (count == 0U) {
            continue;
        }

        score = swv_sp_phase_score(smoothness, mean_delta_pa);

        if (score < best_score) {
            best_score = score;
            best_shift = shift;
            best_smoothness = smoothness;
            best_mean_delta_pa = mean_delta_pa;
        }
    }

    afe_swv_last_phase_shift = best_shift;
    afe_swv_last_phase_mean_delta_pa = best_mean_delta_pa;
    afe_swv_last_phase_smoothness_pa = best_smoothness;

    printk("[SWV-SP] Auto endpoint phase shift=%d samples, smoothness_score=%d.%03d nA",
           best_shift,
           (int32_t)(best_smoothness / 1000),
           (int32_t)(best_smoothness % 1000));
    print_current_na(", mean_delta=", best_mean_delta_pa);
    if (afe_swv_phase_expectation_enabled) {
        print_current_na(", expected=", afe_swv_expected_delta_pa);
        printk(", guarded_score=%d.%03d nA",
               (int32_t)(best_score / 1000),
               (int32_t)(best_score % 1000));
    }
    printk(".\n");

    return best_shift;
}

static void print_swv_single_point_extract(void)
{
    int32_t phase_shift = 0;
    int32_t delta_pa_values[AFE_SWV_MAX_STEP_COUNT] = {0};
    bool delta_valid[AFE_SWV_MAX_STEP_COUNT] = {0};

    printk("[SWV-SP] Extracting pulse endpoints from %u continuous FIFO samples.\n",
           afe_baseline_count);
    printk("[SWV-SP] Logical endpoints=%u (forward/reverse per step), endpoint avg=%u%%, guard=%u%%.\n",
           swv_single_point_count(),
           afe_swv_endpoint_avg_pct,
           afe_swv_endpoint_guard_pct);
    printk("[SWV-SP] Delta_I_avg3 is a centered 3-step moving average for 50Hz phase smoothing.\n");

    if (afe_baseline_count < swv_single_point_count()) {
        printk("[SWV-SP] WARNING: only %u samples captured; Delta_I extraction may be incomplete.\n",
               afe_baseline_count);
    }

    if (afe_baseline_count >= swv_single_point_count()) {
        phase_shift = swv_sp_find_best_phase_shift();
    }
    if (ble_service_is_connected()) {
        char mean_buf[16];
        char expected_buf[16];
        char phase_line[160];

        (void)format_decimal3(mean_buf,
                              sizeof(mean_buf),
                              afe_swv_last_phase_mean_delta_pa);
        (void)format_decimal3(expected_buf,
                              sizeof(expected_buf),
                              afe_swv_expected_delta_pa);
        snprintk(phase_line,
                 sizeof(phase_line),
                 "EVT,PHASE,SHIFT=%d,HALF_SAMPLES=%u,MAX_SHIFT=%u,MEAN_DELTA_NA=%s,EXPECTED_DELTA_NA=%s\r\n",
                 afe_swv_last_phase_shift,
                 afe_swv_last_phase_half_samples,
                 afe_swv_last_phase_max_shift,
                 mean_buf,
                 afe_swv_phase_expectation_enabled ? expected_buf : "NA");
        (void)ble_service_send_text(phase_line);
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        int32_t forward_pa;
        int32_t reverse_pa;

        if (afe_baseline_count == 0U || forward_slot >= afe_baseline_count) {
            continue;
        }

        forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift, NULL, NULL);
        reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift, NULL, NULL);
        delta_pa_values[step] = reverse_pa - forward_pa;
        delta_valid[step] = true;
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        uint32_t base_code = swv_sp_base_code_for_step(step);
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        uint32_t forward_first;
        uint32_t forward_last;
        uint32_t reverse_first;
        uint32_t reverse_last;
        int32_t voltage_uv = lpdac_code_to_polarization_uv((uint16_t)base_code);
        int32_t forward_pa;
        int32_t reverse_pa;
        int32_t delta_avg3_pa = 0;
        uint32_t avg3_count = 0;

        if (afe_baseline_count == 0U || forward_slot >= afe_baseline_count) {
            printk("[SWV-SP] Step %02u: insufficient samples.\n", step + 1U);
            continue;
        }

        forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift,
                                                   &forward_first, &forward_last);
        reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift,
                                                   &reverse_first, &reverse_last);

        if (step > 0U && delta_valid[step - 1U]) {
            delta_avg3_pa += delta_pa_values[step - 1U];
            avg3_count++;
        }
        if (delta_valid[step]) {
            delta_avg3_pa += delta_pa_values[step];
            avg3_count++;
        }
        if ((step + 1U) < afe_swv_sp_step_count && delta_valid[step + 1U]) {
            delta_avg3_pa += delta_pa_values[step + 1U];
            avg3_count++;
        }
        if (avg3_count > 0U) {
            delta_avg3_pa /= (int32_t)avg3_count;
        }

        printk("[SWV-SP] Step %02u: ", step + 1U);
        print_voltage_mv("Vbase=", voltage_uv);
        printk(" f_idx=%u-%u r_idx=%u-%u ", forward_first, forward_last, reverse_first, reverse_last);
        print_current_na("I_forward=", forward_pa);
        print_current_na(", I_reverse=", reverse_pa);
        print_current_na(", Delta_I=", delta_pa_values[step]);
        print_current_na(", Delta_I_avg3=", delta_avg3_pa);
        printk("\n");
    }
}

static int32_t swv_centered_mean(const int32_t *values,
                                 const bool *valid,
                                 uint32_t count,
                                 uint32_t center,
                                 uint32_t radius)
{
    int64_t sum = 0;
    uint32_t n = 0;
    uint32_t start = center > radius ? center - radius : 0U;
    uint32_t end = MIN(count - 1U, center + radius);

    for (uint32_t i = start; i <= end; i++) {
        if (valid[i]) {
            sum += values[i];
            n++;
        }
    }

    if (n == 0U) {
        return 0;
    }

    return (int32_t)(sum / (int64_t)n);
}

static int32_t swv_savgol5_quadratic(const int32_t *values,
                                     const bool *valid,
                                     uint32_t count,
                                     uint32_t center)
{
    int64_t filtered;

    if (center < 2U || (center + 2U) >= count) {
        return swv_centered_mean(values, valid, count, center, 2U);
    }

    for (uint32_t i = center - 2U; i <= center + 2U; i++) {
        if (!valid[i]) {
            return swv_centered_mean(values, valid, count, center, 2U);
        }
    }

    filtered = (-3LL * values[center - 2U]) +
               (12LL * values[center - 1U]) +
               (17LL * values[center]) +
               (12LL * values[center + 1U]) -
               (3LL * values[center + 2U]);

    if (filtered >= 0) {
        filtered += 17LL;
    } else {
        filtered -= 17LL;
    }

    return (int32_t)(filtered / 35LL);
}

static struct dummy_delta_stats swv_dummy_delta_stats_compute(const int32_t *values,
                                                              const bool *valid,
                                                              uint32_t count)
{
    struct dummy_delta_stats stats = {0};
    int64_t sum_pa = 0;
    int64_t sum_sq_pa = 0;
    uint32_t start = count > 4U ? 2U : 0U;
    uint32_t end = count > 4U ? count - 2U : count;

    for (uint32_t i = start; i < end; i++) {
        int64_t value;

        if (!valid[i]) {
            continue;
        }

        value = values[i];
        sum_pa += value;
        sum_sq_pa += value * value;
        stats.count++;
    }

    if (stats.count == 0U) {
        return stats;
    }

    stats.mean_pa = (int32_t)(sum_pa / (int64_t)stats.count);
    if (stats.count > 1U) {
        int64_t mean_sq = (int64_t)stats.mean_pa * (int64_t)stats.mean_pa;
        int64_t variance = (sum_sq_pa / (int64_t)stats.count) - mean_sq;

        if (variance < 0) {
            variance = 0;
        }
        stats.std_pa = (int32_t)isqrt64((uint64_t)variance);
    }

    return stats;
}

static void print_blank_peak_extract(void)
{
    const struct swv_profile_cfg *profile_cfg = swv_profile_get(afe_current_swv_profile);
    int32_t phase_shift = 0;
    int32_t delta_pa_values[AFE_SWV_MAX_STEP_COUNT] = {0};
    bool delta_valid[AFE_SWV_MAX_STEP_COUNT] = {0};
    int32_t peak_pa = 0;
    int32_t peak_voltage_uv = 0;
    int64_t peak_abs_pa = -1;
    uint32_t peak_step = 0;

    printk("[BLANK-PEAK] Extracting single-frequency SWV peak from %u FIFO samples.\n",
           afe_baseline_count);
    printk("[BLANK-PEAK] Peak search window: E_WE-RE=%d..%d mV; edge transients outside this window are ignored.\n",
           AFE_BLANK_PEAK_SEARCH_MIN_UV / 1000,
           AFE_BLANK_PEAK_SEARCH_MAX_UV / 1000);
    printk("[BLANK-PEAK] Delta_I polarity: DropSens-style I_forward - I_reverse.\n");
    if (afe_blank_peak_invert_current_sign) {
        printk("[BLANK-PEAK] TIA raw-current polarity normalized for DropSens-style/electrochemical display.\n");
    }
    if (afe_blank_peak_emit_smoothing) {
        if (!nanostat_battery_trace_is_active()) {
            printk("[BLANK-PEAK] CSV columns: E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,Delta_I_smooth5_nA,Delta_I_smooth7_nA,Delta_I_sg5_nA,I_forward_nA,I_reverse_nA\n");
            printk("[BLANK-PEAK-CSV] E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,Delta_I_smooth5_nA,Delta_I_smooth7_nA,Delta_I_sg5_nA,I_forward_nA,I_reverse_nA\n");
        }
        if (ble_service_is_connected()) {
            if (afe_capture_is_dummy_swv) {
                char step_buf[16];
                char pulse_buf[16];
                char expected_buf[16];
                char dummy_hdr[224];

                (void)format_decimal3(step_buf,
                                      sizeof(step_buf),
                                      swv_profile_step_uv(profile_cfg->code_step));
                (void)format_decimal3(pulse_buf,
                                      sizeof(pulse_buf),
                                      swv_pulse_uv());
                (void)format_decimal3(expected_buf,
                                      sizeof(expected_buf),
                                      swv_dummy_expected_delta_pa(afe_dummy_resistor_ohms));
                snprintk(dummy_hdr,
                         sizeof(dummy_hdr),
                         "HDR,DUMMY_SWV,PROFILE=%s,FREQ=%u,STEP_MV=%s,PULSE_MV=%s,RTIA=%lu,RDUMMY=%u,EXPECTED_DELTA_NA=%s,POINTS=%u,SCAN_MS=%u,SEGMENTED=%u\r\n",
                         profile_cfg->token,
                         afe_blank_peak_segment_half_us > 0U ?
                             (uint32_t)(500000U / afe_blank_peak_segment_half_us) : 0U,
                         step_buf,
                         pulse_buf,
                         (unsigned long)(afe_active_rtia_ohms + 0.5f),
                         afe_dummy_resistor_ohms,
                         expected_buf,
                         afe_swv_sp_step_count,
                         afe_dummy_scan_ms,
                         profile_cfg->segmented ? 1U : 0U);
                (void)ble_service_send_text(dummy_hdr);
            }
            (void)ble_service_send_text("HDR,SWV,E_WE_RE_MV,DELTA_NA,DELTA3_NA,SMOOTH5_NA,SMOOTH7_NA,SG5_NA,IF_NA,IR_NA\r\n");
        }
    } else {
        if (!nanostat_battery_trace_is_active()) {
            printk("[BLANK-PEAK] CSV columns: E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,I_forward_nA,I_reverse_nA\n");
            printk("[BLANK-PEAK-CSV] E_WE_RE_mV,Delta_I_nA,Delta_I_avg3_nA,I_forward_nA,I_reverse_nA\n");
        }
        if (ble_service_is_connected()) {
            (void)ble_service_send_text("HDR,SWV,E_WE_RE_MV,DELTA_NA,DELTA3_NA,IF_NA,IR_NA\r\n");
        }
    }

    if (afe_baseline_count < swv_single_point_count()) {
        printk("[BLANK-PEAK] WARNING: only %u samples captured for %u logical endpoints.\n",
               afe_baseline_count, swv_single_point_count());
    }
    if (ble_service_is_connected()) {
        char debug_line[112];

        snprintk(debug_line,
                 sizeof(debug_line),
                 "EVT,DEBUG,RAW_SAMPLES=%u,LOGICAL_ENDPOINTS=%u,LOGICAL_STEPS=%u\r\n",
                 afe_baseline_count,
                 swv_single_point_count(),
                 afe_swv_sp_step_count);
        (void)ble_service_send_text(debug_line);
    }

    if (afe_baseline_count >= swv_single_point_count()) {
        phase_shift = swv_sp_find_best_phase_shift();
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        int32_t forward_pa;
        int32_t reverse_pa;

        if (afe_baseline_count == 0U || forward_slot >= afe_baseline_count) {
            continue;
        }

        forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift, NULL, NULL);
        reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift, NULL, NULL);
        if (afe_blank_peak_invert_current_sign) {
            forward_pa = -forward_pa;
            reverse_pa = -reverse_pa;
        }
        delta_pa_values[step] = forward_pa - reverse_pa;
        delta_valid[step] = true;
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        uint32_t base_code = swv_sp_base_code_for_step(step);
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        int32_t voltage_uv = lpdac_code_to_we_re_uv((uint16_t)base_code);
        int32_t forward_pa;
        int32_t reverse_pa;
        int32_t delta_avg3_pa = 0;
        int32_t delta_smooth5_pa = 0;
        int32_t delta_smooth7_pa = 0;
        int32_t delta_sg5_pa = 0;
        uint32_t avg3_count = 0;
        int32_t peak_metric_pa;
        int64_t abs_peak_metric_pa;

        if (afe_baseline_count == 0U || forward_slot >= afe_baseline_count) {
            continue;
        }

        forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift, NULL, NULL);
        reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift, NULL, NULL);
        if (afe_blank_peak_invert_current_sign) {
            forward_pa = -forward_pa;
            reverse_pa = -reverse_pa;
        }

        if (step > 0U && delta_valid[step - 1U]) {
            delta_avg3_pa += delta_pa_values[step - 1U];
            avg3_count++;
        }
        if (delta_valid[step]) {
            delta_avg3_pa += delta_pa_values[step];
            avg3_count++;
        }
        if ((step + 1U) < afe_swv_sp_step_count && delta_valid[step + 1U]) {
            delta_avg3_pa += delta_pa_values[step + 1U];
            avg3_count++;
        }
        if (avg3_count > 0U) {
            delta_avg3_pa /= (int32_t)avg3_count;
        }

        if (afe_blank_peak_emit_smoothing) {
            delta_smooth5_pa = swv_centered_mean(delta_pa_values,
                                                 delta_valid,
                                                 afe_swv_sp_step_count,
                                                 step,
                                                 2U);
            delta_smooth7_pa = swv_centered_mean(delta_pa_values,
                                                 delta_valid,
                                                 afe_swv_sp_step_count,
                                                 step,
                                                 3U);
            delta_sg5_pa = swv_savgol5_quadratic(delta_pa_values,
                                                 delta_valid,
                                                 afe_swv_sp_step_count,
                                                 step);
        }

        peak_metric_pa = afe_blank_peak_emit_smoothing ? delta_sg5_pa : delta_avg3_pa;
        abs_peak_metric_pa = peak_metric_pa < 0 ? -(int64_t)peak_metric_pa :
                             (int64_t)peak_metric_pa;
        if (voltage_uv >= AFE_BLANK_PEAK_SEARCH_MIN_UV &&
            voltage_uv <= AFE_BLANK_PEAK_SEARCH_MAX_UV &&
            abs_peak_metric_pa > peak_abs_pa) {
            peak_abs_pa = abs_peak_metric_pa;
            peak_pa = peak_metric_pa;
            peak_step = step + 1U;
            peak_voltage_uv = voltage_uv;
        }

        if (!nanostat_battery_trace_is_active()) {
            printk("[BLANK-PEAK-CSV] ");
            print_csv_decimal3(voltage_uv);
            printk(",");
            print_csv_decimal3(delta_pa_values[step]);
            printk(",");
            print_csv_decimal3(delta_avg3_pa);
            printk(",");
            if (afe_blank_peak_emit_smoothing) {
                print_csv_decimal3(delta_smooth5_pa);
                printk(",");
                print_csv_decimal3(delta_smooth7_pa);
                printk(",");
                print_csv_decimal3(delta_sg5_pa);
                printk(",");
            }
            print_csv_decimal3(forward_pa);
            printk(",");
            print_csv_decimal3(reverse_pa);
            printk("\n");
        }

        if (ble_service_is_connected()) {
            char v_buf[16];
            char delta_buf[16];
            char delta_avg3_buf[16];
            char delta_smooth5_buf[16];
            char delta_smooth7_buf[16];
            char delta_sg5_buf[16];
            char forward_buf[16];
            char reverse_buf[16];
            char ble_line[176];

            (void)format_decimal3(v_buf, sizeof(v_buf), voltage_uv);
            (void)format_decimal3(delta_buf, sizeof(delta_buf), delta_pa_values[step]);
            (void)format_decimal3(delta_avg3_buf, sizeof(delta_avg3_buf), delta_avg3_pa);
            (void)format_decimal3(delta_smooth5_buf, sizeof(delta_smooth5_buf), delta_smooth5_pa);
            (void)format_decimal3(delta_smooth7_buf, sizeof(delta_smooth7_buf), delta_smooth7_pa);
            (void)format_decimal3(delta_sg5_buf, sizeof(delta_sg5_buf), delta_sg5_pa);
            (void)format_decimal3(forward_buf, sizeof(forward_buf), forward_pa);
            (void)format_decimal3(reverse_buf, sizeof(reverse_buf), reverse_pa);
            if (afe_blank_peak_emit_smoothing) {
                snprintk(ble_line,
                         sizeof(ble_line),
                         "DATA,SWV,%s,%s,%s,%s,%s,%s,%s,%s\r\n",
                         v_buf,
                         delta_buf,
                         delta_avg3_buf,
                         delta_smooth5_buf,
                         delta_smooth7_buf,
                         delta_sg5_buf,
                         forward_buf,
                         reverse_buf);
            } else {
                snprintk(ble_line,
                         sizeof(ble_line),
                         "DATA,SWV,%s,%s,%s,%s,%s\r\n",
                         v_buf,
                         delta_buf,
                         delta_avg3_buf,
                         forward_buf,
                         reverse_buf);
            }
            (void)ble_service_send_text(ble_line);
        }
    }

    if (afe_capture_is_dummy_swv) {
        struct dummy_delta_stats stats =
            swv_dummy_delta_stats_compute(delta_pa_values,
                                          delta_valid,
                                          afe_swv_sp_step_count);
        int32_t expected_pa = swv_dummy_expected_delta_pa(afe_dummy_resistor_ohms);
        int32_t error_x100 = 0;
        int32_t abs_expected = expected_pa < 0 ? -expected_pa : expected_pa;

        if (abs_expected > 0) {
            int32_t diff = stats.mean_pa - expected_pa;
            if (diff < 0) {
                diff = -diff;
            }
            error_x100 = (int32_t)(((int64_t)diff * 10000LL) / (int64_t)abs_expected);
        }

        printk("[DUMMY-SWV] Ohmic response summary: n=%u ", stats.count);
        print_current_na("mean_delta=", stats.mean_pa);
        printk(" ");
        print_current_na("sd_delta=", stats.std_pa);
        printk(" ");
        print_current_na("expected_delta=", expected_pa);
        printk(", error=%d.%02d%%\n", error_x100 / 100, error_x100 % 100);

        if (ble_service_is_connected()) {
            char step_buf[16];
            char pulse_buf[16];
            char mean_buf[16];
            char sd_buf[16];
            char expected_buf[16];
            char ble_line[320];

            (void)format_decimal3(step_buf,
                                  sizeof(step_buf),
                                  swv_profile_step_uv(profile_cfg->code_step));
            (void)format_decimal3(pulse_buf,
                                  sizeof(pulse_buf),
                                  swv_pulse_uv());
            (void)format_decimal3(mean_buf, sizeof(mean_buf), stats.mean_pa);
            (void)format_decimal3(sd_buf, sizeof(sd_buf), stats.std_pa);
            (void)format_decimal3(expected_buf, sizeof(expected_buf), expected_pa);
            snprintk(ble_line,
                     sizeof(ble_line),
                     "EVT,DONE,DUMMY_SWV,PROFILE=%s,FREQ=%u,RTIA=%lu,RDUMMY=%u,STEP_MV=%s,PULSE_MV=%s,POINTS=%u,RAW_SAMPLES=%u,VALID_DELTAS=%u,SCAN_MS=%u,MEAN_DELTA_NA=%s,SD_DELTA_NA=%s,EXPECTED_DELTA_NA=%s,ERROR_PCT=%d.%02d,HIGHZ=1\r\n",
                     profile_cfg->token,
                     afe_blank_peak_segment_half_us > 0U ?
                         (uint32_t)(500000U / afe_blank_peak_segment_half_us) : 0U,
                     (unsigned long)(afe_active_rtia_ohms + 0.5f),
                     afe_dummy_resistor_ohms,
                     step_buf,
                     pulse_buf,
                     afe_swv_sp_step_count,
                     afe_baseline_count,
                     stats.count,
                     afe_dummy_scan_ms,
                     mean_buf,
                     sd_buf,
                     expected_buf,
                     error_x100 / 100,
                     error_x100 % 100);
            (void)ble_service_send_text(ble_line);
        }
        return;
    }

    printk("[BLANK-PEAK] Peak(abs %s, windowed): step=%u ",
           afe_blank_peak_emit_smoothing ? "SG5" : "avg3",
           peak_step);
    print_voltage_mv("E_WE-RE=", peak_voltage_uv);
    printk(" ");
    print_current_na(afe_blank_peak_emit_smoothing ? "Delta_I_sg5=" : "Delta_I_avg3=", peak_pa);
    printk("\n");

    if (ble_service_is_connected()) {
        char v_buf[16];
        char peak_buf[16];
        char ble_line[80];

        (void)format_decimal3(v_buf, sizeof(v_buf), peak_voltage_uv);
        (void)format_decimal3(peak_buf, sizeof(peak_buf), peak_pa);
        snprintk(ble_line,
                 sizeof(ble_line),
                 "EVT,DONE,BLANK,PEAK,%u,%s,%s\r\n",
                 peak_step,
                 v_buf,
                 peak_buf);
        (void)ble_service_send_text(ble_line);
    }
}

static void swv_single_point_avg3_stats(const char *label,
                                        int32_t *peak_pa_out,
                                        int32_t *mean_pa_out)
{
    int32_t phase_shift = 0;
    int32_t delta_pa_values[AFE_SWV_MAX_STEP_COUNT] = {0};
    bool delta_valid[AFE_SWV_MAX_STEP_COUNT] = {0};
    int32_t peak_pa = 0;
    int64_t peak_abs_pa = -1;
    uint32_t peak_step = 0;
    int32_t peak_voltage_uv = 0;
    int64_t mean_sum_pa = 0;
    uint32_t mean_count = 0;

    if (afe_baseline_count >= swv_single_point_count()) {
        phase_shift = swv_sp_find_best_phase_shift();
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        uint32_t forward_slot = step * 2U;
        uint32_t reverse_slot = forward_slot + 1U;
        int32_t forward_pa;
        int32_t reverse_pa;

        if (afe_baseline_count == 0U || forward_slot >= afe_baseline_count) {
            continue;
        }

        forward_pa = swv_endpoint_average_for_slot(forward_slot, phase_shift, NULL, NULL);
        reverse_pa = swv_endpoint_average_for_slot(reverse_slot, phase_shift, NULL, NULL);
        delta_pa_values[step] = forward_pa - reverse_pa;
        delta_valid[step] = true;
    }

    for (uint32_t step = 0; step < afe_swv_sp_step_count; step++) {
        int32_t delta_avg3_pa = 0;
        uint32_t avg3_count = 0;
        int64_t abs_pa;

        if (step > 0U && delta_valid[step - 1U]) {
            delta_avg3_pa += delta_pa_values[step - 1U];
            avg3_count++;
        }
        if (delta_valid[step]) {
            delta_avg3_pa += delta_pa_values[step];
            avg3_count++;
        }
        if ((step + 1U) < afe_swv_sp_step_count && delta_valid[step + 1U]) {
            delta_avg3_pa += delta_pa_values[step + 1U];
            avg3_count++;
        }
        if (avg3_count == 0U) {
            continue;
        }

        delta_avg3_pa /= (int32_t)avg3_count;
        mean_sum_pa += delta_avg3_pa;
        mean_count++;

        abs_pa = delta_avg3_pa < 0 ? -(int64_t)delta_avg3_pa : (int64_t)delta_avg3_pa;
        if (abs_pa > peak_abs_pa) {
            uint32_t base_code = swv_sp_base_code_for_step(step);

            peak_abs_pa = abs_pa;
            peak_pa = delta_avg3_pa;
            peak_step = step + 1U;
            peak_voltage_uv = lpdac_code_to_polarization_uv((uint16_t)base_code);
        }
    }

    if (peak_pa_out != NULL) {
        *peak_pa_out = peak_pa;
    }
    if (mean_pa_out != NULL) {
        *mean_pa_out = mean_count > 0U ? (int32_t)(mean_sum_pa / (int64_t)mean_count) : 0;
    }

    printk("[KDM] %s peak(avg3): step=%u ", label, peak_step);
    print_voltage_mv("Vbase=", peak_voltage_uv);
    printk(" ");
    print_current_na("I_peak=", peak_pa);
    if (mean_pa_out != NULL) {
        print_current_na(", I_mean=", *mean_pa_out);
    }
    printk("\n");
}

static void print_baseline_summary(void)
{
    int64_t sum_pa = 0;
    int32_t min_pa = INT32_MAX;
    int32_t max_pa = INT32_MIN;

    if (afe_baseline_count == 0U) {
        printk("[AD5941] %s capture complete: 0 samples stored in MCU RAM.\n",
               afe_capture_name);
        return;
    }

    for (uint32_t i = 0; i < afe_baseline_count; i++) {
        int32_t sample_pa = afe_baseline_pa[i];

        sum_pa += sample_pa;
        if (sample_pa < min_pa) {
            min_pa = sample_pa;
        }
        if (sample_pa > max_pa) {
            max_pa = sample_pa;
        }
    }

    int32_t avg_pa = afe_baseline_count > 0 ?
                     (int32_t)(sum_pa / (int64_t)afe_baseline_count) : 0;

    printk("[AD5941] %s capture complete: %u samples stored in MCU RAM.\n",
           afe_capture_name, afe_baseline_count);
    printk("[AD5941] %s ", afe_capture_name);
    print_current_na("avg=", avg_pa);
    print_current_na(", min=", min_pa);
    print_current_na(", max=", max_pa);
    printk("\n");
}

static void store_and_print_fifo_samples(const uint32_t *buffer,
                                         uint32_t read_count,
                                         uint32_t fifo_count_before)
{
    for (uint32_t i = 0; i < read_count; i++) {
        int32_t current_pa = fifo_word_to_current_pa(buffer[i]);
        uint32_t sample_index = afe_baseline_count;

        afe_baseline_pa[afe_baseline_count++] = current_pa;
        afe_latest_current_pa = current_pa;
        if (afe_capture_is_swv_kdm || afe_capture_is_swv_single_point ||
            afe_capture_is_step_response) {
            continue;
        }
        printk("[AD5941] RAW[%03u] fifo_before=%u raw=0x%08X adc=0x%04X ",
               sample_index, fifo_count_before, buffer[i], (uint16_t)(buffer[i] & 0xFFFFU));
        if (afe_capture_is_cv) {
            uint16_t dac_code = cv_dac_code_for_sample(sample_index);
            int32_t voltage_uv = lpdac_code_to_polarization_uv(dac_code);

            printk("dac=0x%03X ", dac_code);
            print_voltage_mv("V=", voltage_uv);
            printk(" ");
        }
        print_current_na("I=", current_pa);
        printk("\n");
    }
}

static uint32_t step_response_crossing_us(int32_t i0_pa,
                                          int32_t iinf_pa,
                                          uint32_t step_index,
                                          uint32_t sample_us,
                                          uint32_t pct_x10)
{
    int64_t delta = (int64_t)iinf_pa - (int64_t)i0_pa;
    int64_t target = (int64_t)i0_pa + ((delta * (int64_t)pct_x10) / 1000LL);

    if (afe_baseline_count == 0U || step_index >= afe_baseline_count || delta == 0) {
        return UINT32_MAX;
    }

    for (uint32_t i = step_index; i < afe_baseline_count; i++) {
        int64_t y = afe_baseline_pa[i];

        if ((delta > 0 && y >= target) || (delta < 0 && y <= target)) {
            if (i <= step_index) {
                return 0U;
            }
            return (i - step_index) * sample_us;
        }
    }

    return UINT32_MAX;
}

static int32_t step_response_average(uint32_t start, uint32_t end)
{
    int64_t sum_pa = 0;
    uint32_t count;

    if (afe_baseline_count == 0U) {
        return 0;
    }

    start = MIN(start, afe_baseline_count - 1U);
    end = MIN(end, afe_baseline_count);
    if (end <= start) {
        end = MIN(start + 1U, afe_baseline_count);
    }

    count = end - start;
    for (uint32_t i = start; i < end; i++) {
        sum_pa += afe_baseline_pa[i];
    }

    return (int32_t)(sum_pa / (int64_t)count);
}

static void print_step_time(const char *label, uint32_t t_us)
{
    if (t_us == UINT32_MAX) {
        printk("%s=N/A", label);
    } else {
        printk("%s=%u us", label, t_us);
    }
}

static void print_step_response_extract(void)
{
    uint32_t total_us = (AFE_STEP_PRE_MS + AFE_STEP_POST_MS) * 1000U;
    uint32_t sample_us;
    uint32_t step_index;
    uint32_t pre_count;
    uint32_t post_start;
    uint32_t final_start;
    uint32_t final_width;
    uint32_t win75_start;
    uint32_t win95_end;
    int32_t i0_pa;
    int32_t iinf_pa;
    int32_t win_pa;
    int32_t err_pa;
    int32_t err_x100;
    uint32_t t63_us;
    uint32_t t90_us;
    uint32_t t95_us;
    uint32_t t99_us;

    printk("[STEP] Endpoint step response extraction.\n");
    printk("[STEP] Setup: %s\n", afe_step_response_setup_text);
    printk("[STEP] DAC step: 0mV for %u ms, then +100mV for %u ms.\n",
           AFE_STEP_PRE_MS, AFE_STEP_POST_MS);

    if (afe_baseline_count < 20U) {
        printk("[STEP] ERROR: not enough samples for step analysis: %u.\n", afe_baseline_count);
        return;
    }

    sample_us = MAX(1U, total_us / afe_baseline_count);
    pre_count = (AFE_STEP_PRE_MS * 1000U) / sample_us;
    pre_count = CLAMP(pre_count, 1U, afe_baseline_count - 1U);
    step_index = pre_count;
    post_start = step_index;

    i0_pa = step_response_average(pre_count / 2U, pre_count);

    final_width = MAX(5U, afe_baseline_count / 20U);
    final_start = afe_baseline_count > final_width ? afe_baseline_count - final_width : post_start;
    final_start = MAX(final_start, post_start);
    iinf_pa = step_response_average(final_start, afe_baseline_count);

    t63_us = step_response_crossing_us(i0_pa, iinf_pa, step_index, sample_us, 632U);
    t90_us = step_response_crossing_us(i0_pa, iinf_pa, step_index, sample_us, 900U);
    t95_us = step_response_crossing_us(i0_pa, iinf_pa, step_index, sample_us, 950U);
    t99_us = step_response_crossing_us(i0_pa, iinf_pa, step_index, sample_us, 990U);

    win75_start = step_index + ((AFE_SWV_LPTIA_EXT_150_HALF_US * 75U) / (100U * sample_us));
    win95_end = step_index + ((AFE_SWV_LPTIA_EXT_150_HALF_US * 95U) / (100U * sample_us));
    if (win95_end <= win75_start) {
        win95_end = win75_start + 1U;
    }
    win75_start = MIN(win75_start, afe_baseline_count - 1U);
    win95_end = MIN(win95_end, afe_baseline_count);
    win_pa = step_response_average(win75_start, win95_end);
    err_pa = win_pa - iinf_pa;
    if (iinf_pa != 0) {
        int64_t denom = iinf_pa < 0 ? -(int64_t)iinf_pa : (int64_t)iinf_pa;

        err_x100 = (int32_t)(((int64_t)err_pa * 10000LL) / denom);
    } else {
        err_x100 = 0;
    }

    printk("[STEP] Captured %u samples, estimated sample interval=%u us, step_index=%u.\n",
           afe_baseline_count, sample_us, step_index);
    print_current_na("[STEP] I0=", i0_pa);
    print_current_na(", Iinf=", iinf_pa);
    print_current_na(", delta=", iinf_pa - i0_pa);
    printk("\n");
    printk("[STEP] ");
    print_step_time("t63.2", t63_us);
    printk(", ");
    print_step_time("t90", t90_us);
    printk(", ");
    print_step_time("t95", t95_us);
    printk(", ");
    print_step_time("t99", t99_us);
    printk("\n");
    printk("[STEP] 150Hz SWV endpoint window maps to sample %u..%u (75%%..95%% of half-period).\n",
           win75_start, win95_end > 0U ? win95_end - 1U : win95_end);
    print_current_na("[STEP] endpoint_window_avg=", win_pa);
    print_current_na(", endpoint_error=", err_pa);
    printk(", endpoint_error=%d.%02d%% vs Iinf\n",
           err_x100 / 100,
           err_x100 < 0 ? -(err_x100 % 100) : err_x100 % 100);

    printk("[STEP-CSV] index,t_us,I_nA\n");
    for (uint32_t i = 0; i < afe_baseline_count; i++) {
        int32_t current_pa = afe_baseline_pa[i];
        int32_t abs_pa = current_pa < 0 ? -current_pa : current_pa;

        printk("[STEP-CSV] %u,%u,%s%d.%03d\n",
               i,
               i < step_index ? 0U : (i - step_index) * sample_us,
               current_pa < 0 ? "-" : "",
               abs_pa / 1000,
               abs_pa % 1000);
    }
}

static void afe_fifo_work_handler(struct k_work *work)
{
    uint32_t buffer[AFE_FIFO_READ_CHUNK];
    uint32_t fifo_count;

    ARG_UNUSED(work);

    while (afe_baseline_count < AFE_BASELINE_SAMPLES) {
        uint32_t read_count;

        fifo_count = AD5940_FIFOGetCnt();
        if (fifo_count < AFE_FIFO_WATERMARK) {
            break;
        }

        read_count = MIN(fifo_count, AFE_FIFO_READ_CHUNK);
        read_count = MIN(read_count, AFE_BASELINE_SAMPLES - afe_baseline_count);
        if (read_count == 0U) {
            break;
        }

        AD5940_FIFORd(buffer, read_count);
        store_and_print_fifo_samples(buffer, read_count, fifo_count);
    }

    AD5940_INTCClrFlag(AFEINTSRC_DATAFIFOTHRESH);

    if (afe_baseline_count >= AFE_BASELINE_SAMPLES) {
        AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
        AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
        print_baseline_summary();
        ad5941_enter_safe_idle("baseline capture");
    }

    atomic_clear(&afe_fifo_work_pending);
}

static void ad5941_finalize_sequencer_capture(const char *source)
{
    uint32_t buffer[AFE_FIFO_READ_CHUNK];
    uint32_t drained_count = 0;
    uint32_t int_flags;

    printk("[AD5941] %s finish work entered: capture=%s, kdm_stage=%u, current_total=%u.\n",
           source, afe_capture_name, afe_kdm_paper_stage, afe_baseline_count);

    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);

    while (afe_baseline_count < AFE_BASELINE_SAMPLES) {
        uint32_t fifo_count = AD5940_FIFOGetCnt();
        uint32_t read_count;

        if (fifo_count == 0U) {
            break;
        }

        read_count = MIN(fifo_count, AFE_FIFO_READ_CHUNK);
        read_count = MIN(read_count, AFE_BASELINE_SAMPLES - afe_baseline_count);
        if (read_count == 0U) {
            break;
        }

        AD5940_FIFORd(buffer, read_count);
        store_and_print_fifo_samples(buffer, read_count, fifo_count);
        drained_count += read_count;
    }

    int_flags = AD5940_INTCGetFlag(AFEINTC_0);
    if ((int_flags & AFEINTSRC_DATAFIFOOF) != 0U) {
        printk("[AD5941] WARNING: DATA FIFO overflow flag set; consider larger FIFO or lower sample rate.\n");
    }

    AD5940_INTCClrFlag(AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_DATAFIFOOF);
    atomic_clear(&afe_fifo_work_pending);

    printk("[AD5941] Sequencer capture finalized: drained_tail=%u, total=%u/%u samples.\n",
           drained_count, afe_baseline_count, AFE_BASELINE_SAMPLES);

    if (afe_blank_peak_segmented_active &&
        afe_blank_peak_segment_next_step < afe_swv_sp_step_count) {
        int ret;

        printk("[BLANK-PEAK] Segment finalized; cumulative samples=%u. Starting next segment.\n",
               afe_baseline_count);
        ret = ad5941_blank_peak_start_next_segment();
        if (ret != 0) {
            printk("[BLANK-PEAK] ERROR: failed to start next SWV segment: %d\n", ret);
            afe_blank_peak_segmented_active = false;
            ad5941_enter_safe_idle("blank peak segment error");
            atomic_clear(&afe_measurement_busy);
            printk("[AD5941] Measurement session aborted after segmented SWV error.\n");
        }
        return;
    }

    if (afe_blank_peak_segmented_active) {
        printk("[BLANK-PEAK] All %u segmented SWV steps captured; extracting complete curve.\n",
               afe_swv_sp_step_count);
        afe_blank_peak_segmented_active = false;
    }

    if (afe_capture_is_swv_kdm) {
        print_swv_kdm_extract();
    } else if (afe_capture_is_blank_peak) {
        print_blank_peak_extract();
    } else if (afe_capture_is_swv_single_point) {
        print_swv_single_point_extract();
    } else if (afe_capture_is_step_response) {
        print_step_response_extract();
    }
    print_baseline_summary();

    if (afe_kdm_paper_active && afe_kdm_paper_stage == 1U) {
        swv_single_point_avg3_stats("signal-on 150Hz",
                                    &afe_kdm_signal_on_peak_pa,
                                    &afe_kdm_signal_on_mean_pa);
        printk("[KDM] Signal-on scan complete. Starting signal-off 10Hz scan automatically...\n");
        if (ad5941_seq_swv_kdm_paper_scan_start(2U) != 0) {
            printk("[KDM] ERROR: failed to start signal-off 10Hz scan.\n");
            afe_kdm_paper_active = false;
            afe_kdm_paper_stage = 0U;
        }
    } else if (afe_kdm_paper_active && afe_kdm_paper_stage == 2U) {
        int32_t numerator_pa;
        int32_t denominator_avg_pa;
        int32_t mean_numerator_pa;
        int32_t mean_denominator_avg_pa;

        swv_single_point_avg3_stats("signal-off 10Hz",
                                    &afe_kdm_signal_off_peak_pa,
                                    &afe_kdm_signal_off_mean_pa);
        numerator_pa = afe_kdm_signal_on_peak_pa - afe_kdm_signal_off_peak_pa;
        denominator_avg_pa = (afe_kdm_signal_on_peak_pa + afe_kdm_signal_off_peak_pa) / 2;
        mean_numerator_pa = afe_kdm_signal_on_mean_pa - afe_kdm_signal_off_mean_pa;
        mean_denominator_avg_pa = (afe_kdm_signal_on_mean_pa + afe_kdm_signal_off_mean_pa) / 2;

        printk("[KDM] Dual-frequency SWV complete.\n");
        print_current_na("[KDM] Peak I_signal_on=", afe_kdm_signal_on_peak_pa);
        print_current_na(", Peak I_signal_off=", afe_kdm_signal_off_peak_pa);
        print_current_na(", numerator=", numerator_pa);
        print_current_na(", denominator_avg=", denominator_avg_pa);
        printk("\n");
        if (denominator_avg_pa != 0) {
            int32_t kdm_x1000 = (int32_t)(((int64_t)numerator_pa * 1000LL) /
                                          (int64_t)denominator_avg_pa);
            int32_t abs_kdm = kdm_x1000 < 0 ? -kdm_x1000 : kdm_x1000;

            printk("[KDM] Peak_KDM_Value=%s%d.%03d (dimensionless)\n",
                   kdm_x1000 < 0 ? "-" : "",
                   abs_kdm / 1000,
                   abs_kdm % 1000);
        } else {
            printk("[KDM] ERROR: peak denominator is zero; Peak_KDM_Value not computed.\n");
        }

        print_current_na("[KDM] Mean I_signal_on=", afe_kdm_signal_on_mean_pa);
        print_current_na(", Mean I_signal_off=", afe_kdm_signal_off_mean_pa);
        print_current_na(", numerator=", mean_numerator_pa);
        print_current_na(", denominator_avg=", mean_denominator_avg_pa);
        printk("\n");
        if (mean_denominator_avg_pa != 0) {
            int32_t kdm_x1000 = (int32_t)(((int64_t)mean_numerator_pa * 1000LL) /
                                          (int64_t)mean_denominator_avg_pa);
            int32_t abs_kdm = kdm_x1000 < 0 ? -kdm_x1000 : kdm_x1000;

            printk("[KDM] Mean_KDM_Value=%s%d.%03d (dimensionless, dummy-cell preferred)\n",
                   kdm_x1000 < 0 ? "-" : "",
                   abs_kdm / 1000,
                   abs_kdm % 1000);
        } else {
            printk("[KDM] ERROR: mean denominator is zero; Mean_KDM_Value not computed.\n");
        }

        afe_kdm_paper_active = false;
        afe_kdm_paper_stage = 0U;
    }

    if (afe_repeat_active && !afe_kdm_paper_active) {
        if (afe_repeat_current_run >= afe_repeat_total_runs) {
            printk("[REPEAT] Completed %u/%u runs.\n",
                   afe_repeat_current_run, afe_repeat_total_runs);
            afe_repeat_active = false;
            afe_repeat_is_dummy = false;
        } else {
            uint32_t next_run = (uint32_t)afe_repeat_current_run + 1U;
            int ret = k_work_schedule_for_queue(&afe_kdm_work_q,
                                                &afe_repeat_work,
                                                K_MSEC(afe_repeat_interval_ms));

            printk("[REPEAT] Run %u/%u complete. Waiting %u ms before run %u/%u, ret=%d.\n",
                   afe_repeat_current_run,
                   afe_repeat_total_runs,
                   afe_repeat_interval_ms,
                   next_run,
                   afe_repeat_total_runs,
                   ret);
        }
    }

    if (!afe_kdm_paper_active && afe_finish_highz_enabled) {
        ad5941_enter_safe_idle(source);
    } else if (!afe_kdm_paper_active) {
        printk("[AD5941] Legacy finish: post-measurement high-Z skipped; AFE path left at DAC-zero for comparison.\n");
    }

    if (!afe_kdm_paper_active && !afe_repeat_active) {
        atomic_clear(&afe_measurement_busy);
        printk("[AD5941] Measurement session complete. Press 'y' in RTT for the next menu, or start a new BLE/Web run.\n");
    }
}

static void afe_seq_finish_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    ad5941_finalize_sequencer_capture("SEQ");
}

static void afe_kdm_finish_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    ad5941_finalize_sequencer_capture("KDM");
}

static void afe_int_isr(const struct device *dev,
                        struct gpio_callback *cb,
                        uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    if (atomic_cas(&afe_fifo_work_pending, 0, 1)) {
        k_work_submit_to_queue(&afe_kdm_work_q, &afe_fifo_work);
    }
}

static int ad5941_gpio_int_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&afe_int)) {
        printk("[AD5941] ERROR: AFE GP0 interrupt GPIO is not ready.\n");
        return -ENODEV;
    }

    k_work_init(&afe_fifo_work, afe_fifo_work_handler);
    k_work_init_delayable(&afe_seq_finish_work, afe_seq_finish_work_handler);
    k_work_init_delayable(&afe_kdm_finish_work, afe_kdm_finish_work_handler);
    k_work_init_delayable(&afe_repeat_work, afe_repeat_work_handler);
    k_work_init_delayable(&afe_highz_validation_done_work,
                          afe_highz_validation_done_work_handler);
    k_work_queue_start(&afe_kdm_work_q,
                       afe_kdm_work_q_stack,
                       K_THREAD_STACK_SIZEOF(afe_kdm_work_q_stack),
                       AFE_KDM_WORKQ_PRIORITY,
                       NULL);
#if defined(CONFIG_THREAD_NAME)
    k_thread_name_set(&afe_kdm_work_q.thread, "afe_kdm_wq");
#endif

    ret = gpio_pin_configure_dt(&afe_int, GPIO_INPUT);
    if (ret != 0) {
        printk("[AD5941] ERROR: Failed to configure AFE GP0 interrupt GPIO: %d\n", ret);
        return ret;
    }

    gpio_init_callback(&afe_int_cb, afe_int_isr, BIT(afe_int.pin));
    ret = gpio_add_callback_dt(&afe_int, &afe_int_cb);
    if (ret != 0) {
        printk("[AD5941] ERROR: Failed to add AFE GP0 interrupt callback: %d\n", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&afe_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("[AD5941] ERROR: Failed to enable AFE GP0 falling-edge interrupt: %d\n", ret);
        return ret;
    }

    printk("[AD5941] Zephyr GP0 interrupt configured on P0.29.\n");
    return 0;
}

static void ad5941_fifo_and_int_config(void)
{
    FIFOCfg_Type fifo_cfg;
    AGPIOCfg_Type agpio_cfg;

    AD5940_StructInit(&fifo_cfg, sizeof(fifo_cfg));
    fifo_cfg.FIFOEn = bTRUE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_2KB;
    fifo_cfg.FIFOSrc = AFE_FIFO_SOURCE;
    fifo_cfg.FIFOThresh = AFE_FIFO_WATERMARK;
    AD5940_FIFOCfg(&fifo_cfg);

    AD5940_StructInit(&agpio_cfg, sizeof(agpio_cfg));
    agpio_cfg.FuncSet = GP0_INT;
    agpio_cfg.OutputEnSet = AGPIO_Pin0;
    agpio_cfg.InputEnSet = 0;
    agpio_cfg.PullEnSet = 0;
    agpio_cfg.OutVal = 0;
    AD5940_AGPIOCfg(&agpio_cfg);

    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] FIFO configured: source=SINC2/Notch path, threshold=%u.\n",
           AFE_FIFO_WATERMARK);
    printk("[AD5941] GP0 routed to AD5941 INTC0 output.\n");
}

static void ad5941_sram_fifo_seq_config(void)
{
    FIFOCfg_Type fifo_cfg;
    SEQCfg_Type seq_cfg;

    AD5940_StructInit(&fifo_cfg, sizeof(fifo_cfg));
    fifo_cfg.FIFOEn = bTRUE;
    fifo_cfg.FIFOMode = FIFOMODE_FIFO;
    fifo_cfg.FIFOSize = FIFOSIZE_2KB;
    fifo_cfg.FIFOSrc = AFE_FIFO_SOURCE;
    fifo_cfg.FIFOThresh = AFE_FIFO_WATERMARK;
    AD5940_FIFOCfg(&fifo_cfg);

    AD5940_StructInit(&seq_cfg, sizeof(seq_cfg));
    seq_cfg.SeqMemSize = SEQMEMSIZE_4KB;
    seq_cfg.SeqEnable = bTRUE;
    seq_cfg.SeqBreakEn = bFALSE;
    seq_cfg.SeqIgnoreEn = bFALSE;
    seq_cfg.SeqCntCRCClr = bTRUE;
    seq_cfg.SeqWrTimer = 0;
    AD5940_SEQCfg(&seq_cfg);

    printk("[AD5941] SRAM allocated for sequencer test: FIFO=2KB, SEQ=4KB.\n");
}

static void ad5941_swv_kdm_filter_config_ex(uint32_t sinc2_osr,
                                            const char *sinc2_osr_text,
                                            bool notch_enable)
{
    ADCFilterCfg_Type adc_filter;

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = sinc2_osr;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = notch_enable ? bFALSE : bTRUE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

    printk("[AD5941] SWV-KDM fast filter configured: SINC2 output, Notch %s, OSR=%s.\n",
           notch_enable ? "enabled" : "bypassed", sinc2_osr_text);
}

static void ad5941_swv_kdm_filter_config(uint32_t sinc2_osr, const char *sinc2_osr_text)
{
    ad5941_swv_kdm_filter_config_ex(sinc2_osr, sinc2_osr_text, false);
}

static int ad5941_lptia_offset_calibrate(void)
{
    LPTIAOffsetCal_Type cal_cfg;
    AD5940Err err;

    AD5940_StructInit(&cal_cfg, sizeof(cal_cfg));
    cal_cfg.LpAmpSel = LPAMP0;
    cal_cfg.SysClkFreq = AFE_SYSCLK_HZ;
    cal_cfg.AdcClkFreq = AFE_ADCCLK_HZ;
    cal_cfg.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    cal_cfg.ADCSinc2Osr = AFE_SWV_KDM_FAST_SINC2_OSR;
    cal_cfg.ADCPga = AFE_ADC_PGA;
    cal_cfg.DacData12Bit = AFE_LPDAC_12BIT;
    cal_cfg.DacData6Bit = AFE_LPDAC_6BIT;
    cal_cfg.LpDacVzeroMux = LPDACVZERO_6BIT;
    cal_cfg.LpAmpPwrMod = AFE_LPTIA_EXT_BOOST_MODE;
    cal_cfg.LpTiaSW = AFE_LPTIA_EXT_SW;
    cal_cfg.LpTiaRtia = LPTIARTIA_OPEN;
    cal_cfg.SettleTime10us = 2000;
    cal_cfg.TimeOut10us = 200000;

    printk("[AD5941] Running LPTIA0 hardware offset calibration on external 2M path...\n");
    err = AD5940_LPTIAOffsetCal(&cal_cfg);
    if (err != AD5940ERR_OK) {
        printk("[AD5941] ERROR: LPTIA offset calibration failed: %d\n", err);
        return -1;
    }

    printk("[AD5941] LPTIA0 offset calibration completed.\n");
    return 0;
}

#if ENABLE_LINEARITY_TEST
void ad5941_linearity_test_setup(uint8_t mode)
{
    ADCFilterCfg_Type adc_filter;
    LPDACCfg_Type lp_dac;
    uint16_t vbias_code = AFE_LPDAC_12BIT;

    if (mode == 1U) {
        vbias_code = 0x85D;
        printk("[TEST] Linearity Mode 1: +50mV polarization selected.\n");
    } else if (mode == 2U) {
        vbias_code = 0x8BA;
        printk("[TEST] Linearity Mode 2: +100mV polarization selected.\n");
    } else {
        mode = 0U;
        printk("[TEST] Linearity disabled: 0mV polarization selected.\n");
    }

    linearity_test_mode = mode;

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = AFE_ADC_SINC2_OSR;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bFALSE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bTRUE;
    lp_dac.DacData12Bit = vbias_code;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    printk("[TEST] Linearity DAC configured: VBIAS code=0x%03X, VZERO code=0x%02X, Notch enabled.\n",
           vbias_code, AFE_LPDAC_6BIT);
}
#endif

static void ad5941_hs_switch_matrix_open(void)
{
    SWMatrixCfg_Type sw_cfg;

    AD5940_StructInit(&sw_cfg, sizeof(sw_cfg));
    sw_cfg.Dswitch = SWD_OPEN;
    sw_cfg.Pswitch = SWP_OPEN;
    sw_cfg.Nswitch = SWN_OPEN;
    sw_cfg.Tswitch = SWT_OPEN;
    AD5940_SWMatrixCfgS(&sw_cfg);

    printk("[AD5941] HS switch matrix opened.\n");
}

static void ad5941_lptia_default_frontend_config(void)
{
    LPLoopCfg_Type lp_loop;

    AD5940_StructInit(&lp_loop, sizeof(lp_loop));

    lp_loop.LpDacCfg.LpdacSel = LPDAC0;
    lp_loop.LpDacCfg.LpDacSrc = LPDACSRC_MMR;
    lp_loop.LpDacCfg.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_loop.LpDacCfg.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_loop.LpDacCfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA;
    lp_loop.LpDacCfg.LpDacRef = LPDACREF_2P5;
    lp_loop.LpDacCfg.DataRst = bFALSE;
    lp_loop.LpDacCfg.PowerEn = bTRUE;
    lp_loop.LpDacCfg.DacData12Bit = AFE_LPDAC_12BIT;
    lp_loop.LpDacCfg.DacData6Bit = AFE_LPDAC_6BIT;

    lp_loop.LpAmpCfg.LpAmpSel = LPAMP0;
    lp_loop.LpAmpCfg.LpAmpPwrMod = LPAMPPWR_NORM;
    lp_loop.LpAmpCfg.LpPaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaRtia = LPTIARTIA_512K;
    lp_loop.LpAmpCfg.LpTiaRload = LPTIARLOAD_100R;
    lp_loop.LpAmpCfg.LpTiaRf = AFE_LPTIA_RF;
    lp_loop.LpAmpCfg.LpTiaSW = AFE_LPTIA_SW;

    AD5940_LPLoopCfgS(&lp_loop);
    afe_active_rtia_ohms = AFE_RTIA_OHMS;
}

static void ad5941_lptia_external_frontend_prepare(bool boost_enable)
{
    LPLoopCfg_Type lp_loop;

    AD5940_StructInit(&lp_loop, sizeof(lp_loop));

    lp_loop.LpDacCfg.LpdacSel = LPDAC0;
    lp_loop.LpDacCfg.LpDacSrc = LPDACSRC_MMR;
    lp_loop.LpDacCfg.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_loop.LpDacCfg.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_loop.LpDacCfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA;
    lp_loop.LpDacCfg.LpDacRef = LPDACREF_2P5;
    lp_loop.LpDacCfg.DataRst = bFALSE;
    lp_loop.LpDacCfg.PowerEn = bTRUE;
    lp_loop.LpDacCfg.DacData12Bit = AFE_LPDAC_12BIT;
    lp_loop.LpDacCfg.DacData6Bit = AFE_LPDAC_6BIT;

    lp_loop.LpAmpCfg.LpAmpSel = LPAMP0;
    lp_loop.LpAmpCfg.LpAmpPwrMod = boost_enable ? AFE_LPTIA_EXT_BOOST_MODE : LPAMPPWR_NORM;
    lp_loop.LpAmpCfg.LpPaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaRtia = LPTIARTIA_OPEN;
    lp_loop.LpAmpCfg.LpTiaRload = LPTIARLOAD_100R;
    lp_loop.LpAmpCfg.LpTiaRf = AFE_LPTIA_RF;
    lp_loop.LpAmpCfg.LpTiaSW = AFE_LPTIA_EXT_SW;

    AD5940_LPLoopCfgS(&lp_loop);
}

static void ad5941_hstia_lp_bias_path_prepare(void)
{
    LPAmpCfg_Type lp_amp;

    /*
     * HSTIA readout still uses the low-power PA to drive the VBIAS electrode
     * path. Safe-idle powers LPPA down, so restore it explicitly before each
     * HSTIA scan instead of relying on the boot-time LPLoop configuration.
     */
    AD5940_StructInit(&lp_amp, sizeof(lp_amp));
    lp_amp.LpAmpSel = LPAMP0;
    lp_amp.LpAmpPwrMod = LPAMPPWR_NORM;
    lp_amp.LpPaPwrEn = bTRUE;
    lp_amp.LpTiaPwrEn = bFALSE;
    lp_amp.LpTiaRtia = LPTIARTIA_OPEN;
    lp_amp.LpTiaRload = LPTIARLOAD_100R;
    lp_amp.LpTiaRf = LPTIARF_OPEN;
    lp_amp.LpTiaSW = ENUM_AFE_LPTIASW0_1;
    AD5940_LPAMPCfgS(&lp_amp);
}

static void ad5941_enter_safe_idle(const char *source)
{
    LPAmpCfg_Type lp_amp;
    LPDACCfg_Type lp_dac;
    HSTIACfg_Type hstia_cfg;
    AD5940Err hstia_err;

    AD5940_AFECtrlS(AFECTRL_ADCCNV | AFECTRL_DFT, bFALSE);
    AD5940_SEQCtrlS(bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    AD5940_FIFOCtrlS(AFE_FIFO_SOURCE, bFALSE);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

    ad5941_hs_switch_matrix_open();

    AD5940_StructInit(&lp_amp, sizeof(lp_amp));
    lp_amp.LpAmpSel = LPAMP0;
    lp_amp.LpAmpPwrMod = LPAMPPWR_NORM;
    lp_amp.LpPaPwrEn = bFALSE;
    lp_amp.LpTiaPwrEn = bFALSE;
    lp_amp.LpTiaRtia = LPTIARTIA_OPEN;
    lp_amp.LpTiaRload = LPTIARLOAD_100R;
    lp_amp.LpTiaRf = LPTIARF_OPEN;
    lp_amp.LpTiaSW = 0U;
    AD5940_LPAMPCfgS(&lp_amp);

    AD5940_StructInit(&hstia_cfg, sizeof(hstia_cfg));
    hstia_cfg.HstiaBias = HSTIABIAS_1P1;
    hstia_cfg.HstiaRtiaSel = HSTIARTIA_OPEN;
    hstia_cfg.ExtRtia = 0U;
    hstia_cfg.HstiaCtia = AFE_HSTIA_CTIA_CODE_1PF;
    hstia_cfg.DiodeClose = bFALSE;
    hstia_cfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hstia_cfg.HstiaDe1Rtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDe1Rload = HSTIADERLOAD_OPEN;
    hstia_err = AD5940_HSTIACfgS(&hstia_cfg);
    if (hstia_err != AD5940ERR_OK) {
        printk("[AD5941] WARNING: HSTIA open config failed during safe idle: %d\n",
               hstia_err);
    }

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = 0U;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bFALSE;
    lp_dac.DacData12Bit = AFE_LPDAC_ZERO_CODE;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    AD5940_AFECtrlS(AFECTRL_ALL, bFALSE);

    printk("[AD5941] Safe idle/high-Z entered%s%s: SW matrix open, LPTIA/HSTIA opened, LPDAC/LPPA/TIA/ADC off.\n",
           source != NULL ? " after " : "",
           source != NULL ? source : "");
    printk("EVT,HIGHZ,SW_MATRIX_OPEN=1,LPDAC_OFF=1,LPTIA_OPEN=1,HSTIA_OPEN=1,ADC_OFF=1\n");
    if (ble_service_is_connected()) {
        (void)ble_service_send_text("EVT,HIGHZ,SW_MATRIX_OPEN=1,LPDAC_OFF=1,LPTIA_OPEN=1,HSTIA_OPEN=1,ADC_OFF=1\r\n");
    }
}

static void ad5941_seq_insert_safe_idle_tail(void)
{
    SWMatrixCfg_Type sw_cfg;
    LPAmpCfg_Type lp_amp;
    LPDACCfg_Type lp_dac;
    HSTIACfg_Type hstia_cfg;

    /*
     * Called only while AD5940 sequence generation is enabled. These register
     * writes become the last sequencer commands, so the electrode path opens
     * immediately after the scan instead of waiting for MCU finish work.
     */
    AD5940_StructInit(&sw_cfg, sizeof(sw_cfg));
    sw_cfg.Dswitch = SWD_OPEN;
    sw_cfg.Pswitch = SWP_OPEN;
    sw_cfg.Nswitch = SWN_OPEN;
    sw_cfg.Tswitch = SWT_OPEN;
    AD5940_SWMatrixCfgS(&sw_cfg);

    AD5940_StructInit(&lp_amp, sizeof(lp_amp));
    lp_amp.LpAmpSel = LPAMP0;
    lp_amp.LpAmpPwrMod = LPAMPPWR_NORM;
    lp_amp.LpPaPwrEn = bFALSE;
    lp_amp.LpTiaPwrEn = bFALSE;
    lp_amp.LpTiaRtia = LPTIARTIA_OPEN;
    lp_amp.LpTiaRload = LPTIARLOAD_100R;
    lp_amp.LpTiaRf = LPTIARF_OPEN;
    lp_amp.LpTiaSW = 0U;
    AD5940_LPAMPCfgS(&lp_amp);

    AD5940_StructInit(&hstia_cfg, sizeof(hstia_cfg));
    hstia_cfg.HstiaBias = HSTIABIAS_1P1;
    hstia_cfg.HstiaRtiaSel = HSTIARTIA_OPEN;
    hstia_cfg.ExtRtia = 0U;
    hstia_cfg.HstiaCtia = AFE_HSTIA_CTIA_CODE_1PF;
    hstia_cfg.DiodeClose = bFALSE;
    hstia_cfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hstia_cfg.HstiaDe1Rtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDe1Rload = HSTIADERLOAD_OPEN;
    (void)AD5940_HSTIACfgS(&hstia_cfg);

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = 0U;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bFALSE;
    lp_dac.DacData12Bit = AFE_LPDAC_ZERO_CODE;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    AD5940_AFECtrlS(AFECTRL_ALL, bFALSE);
}

int ad5941_app_init(void)
{
    uint16_t chip_id;
    int ret;

    printk("[AD5941] Starting AD5941 App Initialization...\n");

    if (AD5940_MCUResourceInit(NULL) != 0) {
        printk("[AD5941] ERROR: MCU Resource (SPI/GPIO) Init Failed!\n");
        return -1;
    }

    ad5941_hw_reset();

    chip_id = (uint16_t)AD5940_ReadReg(REG_AFECON_ADIID);
    if (chip_id != AD5940_ADIID) {
        printk("[AD5941] ERROR: ID check failed. Read ID: 0x%04X, expected: 0x%04X\n",
               chip_id, AD5940_ADIID);
        return -1;
    }
    printk("[AD5941] ID check passed. Read ID: 0x%04X\n", chip_id);

    AD5940_Initialize();
    printk("[AD5941] AD5940 Library Initialized.\n");

    CLKCfg_Type clk_cfg;
    AD5940_StructInit(&clk_cfg, sizeof(clk_cfg));
    clk_cfg.ADCClkDiv = ADCCLKDIV_1;
    clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
    clk_cfg.SysClkDiv = SYSCLKDIV_1;
    clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
    clk_cfg.HfOSC32MHzMode = bFALSE;
    clk_cfg.HFOSCEn = bTRUE;
    clk_cfg.HFXTALEn = bFALSE;
    clk_cfg.LFOSCEn = bTRUE;
    AD5940_CLKCfg(&clk_cfg);
    printk("[AD5941] Clock Configured (16MHz HFOSC).\n");

    ad5941_lptia_default_frontend_config();
    printk("[AD5941] Low Power Loop Configured (RTIA = 512k).\n");

    /* Temporarily disabled for raw baseline comparison. */
    printk("[AD5941] LPTIA0 hardware offset calibration is temporarily disabled.\n");
    /*
    ret = ad5941_lptia_offset_calibrate();
    if (ret != 0) {
        return -1;
    }
    */

    ad5941_lptia_default_frontend_config();
    ad5941_hs_switch_matrix_open();

    ad5941_fifo_and_int_config();

    ret = ad5941_gpio_int_init();
    if (ret != 0) {
        return -1;
    }

    ad5941_enter_safe_idle("init");
    printk("[AD5941] AD5941 subsystem initialization completed successfully.\n");
    return 0;
}

bool ad5941_app_get_latest_current(int32_t *current_pa, uint32_t *sample_count)
{
    if (current_pa != NULL) {
        *current_pa = afe_latest_current_pa;
    }
    if (sample_count != NULL) {
        *sample_count = afe_baseline_count;
    }

    return afe_baseline_count > 0U;
}

bool ad5941_app_is_measurement_busy(void)
{
    return atomic_get(&afe_measurement_busy) != 0;
}

void ad5941_app_soft_reset_for_next_test(void)
{
    printk("[AD5941] Soft-resetting measurement session state.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    (void)k_work_cancel_delayable(&afe_kdm_finish_work);
    (void)k_work_cancel_delayable(&afe_repeat_work);
    (void)k_work_cancel_delayable(&afe_highz_validation_done_work);
    afe_highz_validation_active = false;
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    afe_blank_peak_insert_seq_highz_tail = true;
    afe_finish_highz_enabled = true;
    ad5941_enter_safe_idle("soft reset");
    atomic_clear(&afe_fifo_work_pending);
    atomic_clear(&afe_measurement_busy);

    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Idle";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_blank_peak_emit_smoothing = false;
    afe_blank_peak_invert_current_sign = false;
    afe_blank_peak_segmented_active = false;
    afe_blank_peak_segment_next_step = 0U;
    afe_blank_peak_segment_half_us = 0U;
    afe_blank_peak_segment_half_wait_clks = 0U;
    afe_blank_peak_segment_max_steps = AFE_BLANK_PEAK_SEGMENT_MAX_STEPS;
    afe_capture_is_dummy_swv = false;
    afe_current_swv_profile = AD5941_SWV_PROFILE_1MV_SEGMENTED;
    afe_dummy_resistor_ohms = (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS;
    afe_dummy_scan_ms = 0U;
    afe_capture_is_step_response = false;
    afe_kdm_paper_active = false;
    afe_kdm_paper_stage = 0U;
    afe_repeat_active = false;
    afe_repeat_is_dummy = false;
    afe_repeat_current_run = 0U;
    afe_repeat_total_runs = 0U;
    afe_repeat_interval_ms = AFE_REPEAT_SWV_INTERVAL_MS;
}

void ad5941_app_start_measurement(void)
{
    ADCBaseCfg_Type adc_base;
    ADCFilterCfg_Type adc_filter;

    printk("[AD5941] Starting milestone-2 no-load baseline measurement.\n");
    printk("[AD5941] Range: internal LPTIA RTIA=512k, RLOAD=100R, RF=%s, ADC PGA=%s.\n",
           AFE_LPTIA_RF_TEXT, AFE_ADC_PGA_TEXT);
    printk("[AD5941] External RTIA/resistors are not selected in this firmware path.\n");
#if ENABLE_LINEARITY_TEST
    if (linearity_test_mode != 0U) {
        printk("[TEST] Linearity mode %u active: SINC2+Notch DC measurement.\n",
               linearity_test_mode);
    }
#endif

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    (void)k_work_cancel_delayable(&afe_kdm_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Linearity/Baseline";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_active_rtia_ohms = AFE_RTIA_OHMS;

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = AFE_ADC_SINC2_OSR;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
#if ENABLE_LINEARITY_TEST
    adc_filter.BpNotch = linearity_test_mode != 0U ? bFALSE : bTRUE;
#else
    adc_filter.BpNotch = bTRUE;
#endif
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

#if ENABLE_LINEARITY_TEST
    if (linearity_test_mode != 0U) {
        ad5941_linearity_test_setup(linearity_test_mode);
    }
#endif

    ad5941_hs_switch_matrix_open();

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    printk("[AD5941] ADC settling for %u ms with FIFO interrupt disabled...\n",
           AFE_SETTLE_TIME_MS);
    k_msleep(AFE_SETTLE_TIME_MS);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

#if ENABLE_LINEARITY_TEST
    if (linearity_test_mode != 0U) {
        printk("[AD5941] Capturing %u raw SINC2+Notch samples for linearity test (~%u ms).\n",
               AFE_BASELINE_SAMPLES,
               (AFE_BASELINE_SAMPLES * 1000U) / AFE_EST_SAMPLE_RATE_SPS);
    } else
#endif
    {
        printk("[AD5941] Capturing %u raw SINC2 samples for antenna touch test (~%u ms).\n",
           AFE_BASELINE_SAMPLES,
           (AFE_BASELINE_SAMPLES * 1000U) / AFE_EST_SAMPLE_RATE_SPS);
    }
}

int App_SeqCA_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    ADCFilterCfg_Type adc_filter;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Starting hardware Sequencer CA test: +100mV, %u seconds.\n",
           AFE_CA_SEQ_SECONDS);
    printk("[AD5941] MCU will only read FIFO watermark IRQs; CA timing is owned by AD5941 sequencer.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Sequencer CA";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_active_rtia_ohms = AFE_RTIA_OHMS;

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = AFE_ADC_SINC2_OSR;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bFALSE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(ca_seq_gen_buffer, AFE_CA_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);
    AD5940_LPDAC0WriteS(AFE_CA_LPDAC_100MV, AFE_LPDAC_6BIT);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);
    AD5940_SEQGenInsert(SEQ_WAIT(AFE_CA_SEQ_WAIT_CLKS));
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: CA sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] CA sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer CA now. Expected data window: %u seconds.\n",
           AFE_CA_SEQ_SECONDS);

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_CA_SEQ_FINALIZE_MS);
    return 0;
}

int App_SeqCV_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    ADCFilterCfg_Type adc_filter;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    uint32_t last_code;
    AD5940Err err;

    printk("[AD5941] Starting hardware Sequencer CV test: 0x%03X -> 0x%03X -> 0x%03X, step=%u, dwell=%ums.\n",
           AFE_CV_CODE_LOW, AFE_CV_CODE_HIGH, AFE_CV_CODE_LOW,
           AFE_CV_CODE_STEP, AFE_CV_DWELL_MS);
    printk("[AD5941] FIFO IRQ readout remains MCU-side; voltage staircase timing is owned by AD5941 sequencer.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Sequencer CV";
    afe_capture_is_cv = true;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_active_rtia_ohms = AFE_RTIA_OHMS;

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = AFE_ADC_SINC2_OSR;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bFALSE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(cv_seq_gen_buffer, AFE_CV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    last_code = AFE_CV_CODE_LOW;
    for (uint32_t code = AFE_CV_CODE_LOW; code <= AFE_CV_CODE_HIGH; code += AFE_CV_CODE_STEP) {
        AD5940_LPDAC0WriteS((uint16_t)code, AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_CV_SEQ_WAIT_CLKS));
        last_code = code;

        if ((AFE_CV_CODE_HIGH - code) < AFE_CV_CODE_STEP) {
            break;
        }
    }
    if (last_code != AFE_CV_CODE_HIGH) {
        AD5940_LPDAC0WriteS((uint16_t)AFE_CV_CODE_HIGH, AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_CV_SEQ_WAIT_CLKS));
    }

    last_code = AFE_CV_CODE_HIGH;
    for (int32_t code = (int32_t)AFE_CV_CODE_HIGH;
         code >= (int32_t)AFE_CV_CODE_LOW;
         code -= (int32_t)AFE_CV_CODE_STEP) {
        AD5940_LPDAC0WriteS((uint16_t)code, AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_CV_SEQ_WAIT_CLKS));
        last_code = (uint32_t)code;

        if (((uint32_t)code - AFE_CV_CODE_LOW) < AFE_CV_CODE_STEP) {
            break;
        }
    }
    if (last_code != AFE_CV_CODE_LOW) {
        AD5940_LPDAC0WriteS((uint16_t)AFE_CV_CODE_LOW, AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_CV_SEQ_WAIT_CLKS));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: CV sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] CV sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer CV now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_CV_SEQ_FINALIZE_MS);
    return 0;
}

int App_SeqSWV_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    ADCFilterCfg_Type adc_filter;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    uint32_t last_base_code;
    AD5940Err err;

    printk("[AD5941] Starting hardware Sequencer SWV test: base 0x%03X -> 0x%03X, step=%u, pulse=+/-0x%02X, half=%ums.\n",
           AFE_SWV_CODE_LOW, AFE_SWV_CODE_HIGH, AFE_SWV_CODE_STEP,
           AFE_SWV_PULSE_CODE, AFE_SWV_HALF_MS);
    printk("[AD5941] FIFO IRQ readout remains MCU-side; SWV pulse timing is owned by AD5941 sequencer.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Sequencer SWV";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_active_rtia_ohms = AFE_RTIA_OHMS;

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    AD5940_StructInit(&adc_filter, sizeof(adc_filter));
    adc_filter.ADCSinc3Osr = AFE_ADC_SINC3_OSR;
    adc_filter.ADCSinc2Osr = AFE_ADC_SINC2_OSR;
    adc_filter.ADCAvgNum = ADCAVGNUM_2;
    adc_filter.ADCRate = ADCRATE_800KHZ;
    adc_filter.BpNotch = bFALSE;
    adc_filter.BpSinc3 = bFALSE;
    adc_filter.Sinc3ClkEnable = bTRUE;
    adc_filter.Sinc2NotchClkEnable = bTRUE;
    adc_filter.Sinc2NotchEnable = bTRUE;
    adc_filter.DFTClkEnable = bFALSE;
    adc_filter.WGClkEnable = bFALSE;
    AD5940_ADCFilterCfgS(&adc_filter);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    last_base_code = AFE_SWV_CODE_LOW;
    for (uint32_t base_code = AFE_SWV_CODE_LOW;
         base_code <= AFE_SWV_CODE_HIGH;
         base_code += AFE_SWV_CODE_STEP) {
        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SEQ_WAIT_CLKS));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SEQ_WAIT_CLKS));

        last_base_code = base_code;
        if ((AFE_SWV_CODE_HIGH - base_code) < AFE_SWV_CODE_STEP) {
            break;
        }
    }

    if (last_base_code != AFE_SWV_CODE_HIGH) {
        AD5940_LPDAC0WriteS((uint16_t)(AFE_SWV_CODE_HIGH + AFE_SWV_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SEQ_WAIT_CLKS));

        AD5940_LPDAC0WriteS((uint16_t)(AFE_SWV_CODE_HIGH - AFE_SWV_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SEQ_WAIT_CLKS));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: SWV sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] SWV sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer SWV now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_SWV_SEQ_FINALIZE_MS);
    return 0;
}

static int ad5941_seq_swv_kdm_start(const char *capture_name,
                                    uint32_t half_ms,
                                    uint32_t wait_clks,
                                    uint32_t sinc2_osr,
                                    const char *sinc2_osr_text,
                                    uint32_t finalize_ms,
                                    uint32_t max_lag_divisor)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Starting hardware %s test: steps=%u, pulse=+/-0x%02X, half=%ums, SINC2_OSR=%s.\n",
           capture_name, AFE_SWV_SP_STEP_COUNT, AFE_SWV_PULSE_CODE, half_ms, sinc2_osr_text);
    printk("[AD5941] ADC runs continuously; MCU extracts end-of-pulse Delta_I after capture.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = capture_name;
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = true;
    afe_capture_is_swv_single_point = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_active_rtia_ohms = AFE_RTIA_OHMS;
    afe_swv_kdm_half_ms = half_ms;
    afe_swv_kdm_step_ms = half_ms * 2U;
    afe_swv_kdm_max_lag_divisor = max_lag_divisor;

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config(sinc2_osr, sinc2_osr_text);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_CODE_LOW + (step * AFE_SWV_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(wait_clks));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(wait_clks));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: SWV-KDM sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] SWV-KDM sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer SWV-KDM now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(finalize_ms);
    return 0;
}

int App_SeqSWV_SinglePoint_Test_Start(void)
{
    printk("[AD5941] Mode 6 sealed: long-half SWV-KDM diagnostic keeps the verified 100ms timing.\n");
    return ad5941_seq_swv_kdm_start("Sequencer SWV-KDM-Diag",
                                    AFE_SWV_KDM_DIAG_HALF_MS,
                                    AFE_SWV_KDM_DIAG_WAIT_CLKS,
                                    AFE_SWV_KDM_DIAG_SINC2_OSR,
                                    AFE_SWV_KDM_DIAG_SINC2_OSR_TEXT,
                                    AFE_SWV_KDM_DIAG_FINALIZE_MS,
                                    1U);
}

int App_SeqSWV_FastKDM_Test_Start(void)
{
    printk("[AD5941] Mode 7: fast SWV-KDM uses 25ms half-period with much faster SINC2 OSR.\n");
    return ad5941_seq_swv_kdm_start("Sequencer SWV-KDM-Fast",
                                    AFE_SWV_KDM_FAST_HALF_MS,
                                    AFE_SWV_KDM_FAST_WAIT_CLKS,
                                    AFE_SWV_KDM_FAST_SINC2_OSR,
                                    AFE_SWV_KDM_FAST_SINC2_OSR_TEXT,
                                    AFE_SWV_KDM_FAST_FINALIZE_MS,
                                    4U);
}

static void ad5941_lptia_gain_set(uint32_t tiagain_bits,
                                  float rtia_ohms,
                                  const char *rtia_text)
{
    uint32_t reg = AD5940_ReadReg(REG_AFE_LPTIACON0);

    reg &= ~BITM_AFE_LPTIACON0_TIAGAIN;
    reg |= tiagain_bits & BITM_AFE_LPTIACON0_TIAGAIN;
    AD5940_WriteReg(REG_AFE_LPTIACON0, reg);
    afe_active_rtia_ohms = rtia_ohms;

    printk("[AD5941] LPTIA0 TIAGAIN forced to %s, LPTIACON0=0x%08X.\n",
           rtia_text, AD5940_ReadReg(REG_AFE_LPTIACON0));
}

static void ad5941_lptia_boost_gain_set(uint32_t tiagain_bits,
                                        float rtia_ohms,
                                        const char *rtia_text,
                                        bool boost_enable)
{
    uint32_t reg = AD5940_ReadReg(REG_AFE_LPTIACON0);

    reg &= ~BITM_AFE_LPTIACON0_TIAGAIN;
    reg |= tiagain_bits & BITM_AFE_LPTIACON0_TIAGAIN;
    reg &= ~BITM_AFE_LPTIACON0_IBOOST;
    if (boost_enable) {
        reg |= (AFE_LPTIA_EXT_BOOST_MODE << BITP_AFE_LPTIACON0_IBOOST) &
               BITM_AFE_LPTIACON0_IBOOST;
    }
    AD5940_WriteReg(REG_AFE_LPTIACON0, reg);
    afe_active_rtia_ohms = rtia_ohms;

    printk("[AD5941] LPTIA0 mode: %s, boost=%s, LPTIACON0=0x%08X.\n",
           rtia_text, boost_enable ? "BOOST2" : "off",
           AD5940_ReadReg(REG_AFE_LPTIACON0));
}

static void ad5941_lptia_offset_register_write(uint32_t value)
{
    AD5940_WriteReg(REG_AFE_CALDATLOCK, KEY_CALDATLOCK);
    AD5940_WriteReg(REG_AFE_ADCOFFSETLPTIA0, value & BITM_AFE_ADCOFFSETLPTIA0_VALUE);
    AD5940_WriteReg(REG_AFE_CALDATLOCK, 0);
}

static void ad5941_lptia_external_zero_path_restore(void)
{
    LPLoopCfg_Type lp_loop;
    ADCBaseCfg_Type adc_base;

    AD5940_StructInit(&lp_loop, sizeof(lp_loop));
    lp_loop.LpDacCfg.LpdacSel = LPDAC0;
    lp_loop.LpDacCfg.LpDacSrc = LPDACSRC_MMR;
    lp_loop.LpDacCfg.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_loop.LpDacCfg.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_loop.LpDacCfg.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2LPTIA;
    lp_loop.LpDacCfg.LpDacRef = LPDACREF_2P5;
    lp_loop.LpDacCfg.DataRst = bFALSE;
    lp_loop.LpDacCfg.PowerEn = bTRUE;
    lp_loop.LpDacCfg.DacData12Bit = AFE_LPDAC_12BIT;
    lp_loop.LpDacCfg.DacData6Bit = AFE_LPDAC_6BIT;

    lp_loop.LpAmpCfg.LpAmpSel = LPAMP0;
    lp_loop.LpAmpCfg.LpAmpPwrMod = AFE_LPTIA_EXT_BOOST_MODE;
    lp_loop.LpAmpCfg.LpPaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaPwrEn = bTRUE;
    lp_loop.LpAmpCfg.LpTiaRtia = LPTIARTIA_OPEN;
    lp_loop.LpAmpCfg.LpTiaRload = LPTIARLOAD_100R;
    lp_loop.LpAmpCfg.LpTiaRf = AFE_LPTIA_RF;
    lp_loop.LpAmpCfg.LpTiaSW = AFE_LPTIA_EXT_SW;
    AD5940_LPLoopCfgS(&lp_loop);

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config(AFE_SWV_KDM_FAST_SINC2_OSR,
                                  AFE_SWV_KDM_FAST_SINC2_OSR_TEXT);
    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();
    AD5940_WriteReg(REG_AFE_LPTIASW0, AFE_LPTIA_EXT_SW);
    ad5941_lptia_boost_gain_set(ENUM_AFE_LPTIACON0_DISCONTIA,
                                AFE_LPTIA_EXT_RTIA_OHMS,
                                "internal RTIA=open, external 2M + 220pF expected",
                                true);
    AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
}

static int ad5941_offset_diag_capture(const char *label, struct offset_diag_stats *stats)
{
    uint32_t buffer[AFE_FIFO_READ_CHUNK];
    int64_t sum_pa = 0;
    uint64_t sum_sq_pa = 0;
    uint64_t sum_raw = 0;
    int64_t start_ms;

    if (stats == NULL) {
        return -EINVAL;
    }

    stats->samples = 0;
    stats->raw_avg = 0;
    stats->avg_pa = 0;
    stats->std_pa = 0;
    stats->min_pa = INT32_MAX;
    stats->max_pa = INT32_MIN;

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    start_ms = k_uptime_get();
    while ((k_uptime_get() - start_ms) < AFE_OFFSET_DIAG_CAPTURE_MS &&
           stats->samples < AFE_BASELINE_SAMPLES) {
        uint32_t fifo_count = AD5940_FIFOGetCnt();
        uint32_t read_count = MIN(fifo_count, AFE_FIFO_READ_CHUNK);

        read_count = MIN(read_count, AFE_BASELINE_SAMPLES - stats->samples);
        if (read_count == 0U) {
            k_msleep(2);
            continue;
        }

        AD5940_FIFORd(buffer, read_count);
        for (uint32_t i = 0; i < read_count; i++) {
            int32_t current_pa = fifo_word_to_current_pa(buffer[i]);
            int64_t current64 = current_pa;

            sum_pa += current_pa;
            sum_sq_pa += (uint64_t)(current64 * current64);
            sum_raw += (uint16_t)(buffer[i] & 0xFFFFU);
            if (current_pa < stats->min_pa) {
                stats->min_pa = current_pa;
            }
            if (current_pa > stats->max_pa) {
                stats->max_pa = current_pa;
            }
            stats->samples++;
        }
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);

    while (stats->samples < AFE_BASELINE_SAMPLES) {
        uint32_t fifo_count = AD5940_FIFOGetCnt();
        uint32_t read_count = MIN(fifo_count, AFE_FIFO_READ_CHUNK);

        read_count = MIN(read_count, AFE_BASELINE_SAMPLES - stats->samples);
        if (read_count == 0U) {
            break;
        }

        AD5940_FIFORd(buffer, read_count);
        for (uint32_t i = 0; i < read_count; i++) {
            int32_t current_pa = fifo_word_to_current_pa(buffer[i]);
            int64_t current64 = current_pa;

            sum_pa += current_pa;
            sum_sq_pa += (uint64_t)(current64 * current64);
            sum_raw += (uint16_t)(buffer[i] & 0xFFFFU);
            if (current_pa < stats->min_pa) {
                stats->min_pa = current_pa;
            }
            if (current_pa > stats->max_pa) {
                stats->max_pa = current_pa;
            }
            stats->samples++;
        }
    }

    if (stats->samples == 0U) {
        printk("[OFFSET] %s captured 0 samples.\n", label);
        return -EIO;
    }

    stats->avg_pa = (int32_t)(sum_pa / (int64_t)stats->samples);
    stats->raw_avg = (uint32_t)(sum_raw / stats->samples);
    {
        int64_t mean = stats->avg_pa;
        uint64_t mean_sq = (uint64_t)(mean * mean);
        uint64_t avg_sq = sum_sq_pa / stats->samples;
        uint64_t variance = avg_sq > mean_sq ? avg_sq - mean_sq : 0U;

        stats->std_pa = (int32_t)isqrt64(variance);
    }

    return 0;
}

static void ad5941_offset_diag_print_stats(const char *label,
                                           uint32_t offset_reg,
                                           const struct offset_diag_stats *stats)
{
    printk("[OFFSET] %s: ADCOFFSETLPTIA0=0x%08X, samples=%u, raw_avg=0x%04X ",
           label, offset_reg, stats->samples, stats->raw_avg);
    print_current_na("avg=", stats->avg_pa);
    print_current_na(", std=", stats->std_pa);
    print_current_na(", min=", stats->min_pa);
    print_current_na(", max=", stats->max_pa);
    printk("\n");
}

static int ad5941_seq_swv_single_point_start(const char *capture_name,
                                             uint32_t settle_ms,
                                             uint32_t settle_clks,
                                             uint32_t tiagain_bits,
                                             float rtia_ohms,
                                             const char *rtia_text,
                                             uint32_t finalize_ms)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Starting hardware %s test: steps=%u, pulse=+/-0x%02X, settle=%ums, sample=%ums, RTIA=%s.\n",
           capture_name, AFE_SWV_SP_STEP_COUNT, AFE_SWV_PULSE_CODE,
           settle_ms, AFE_SWV_SAMPLE_MS, rtia_text);
    printk("[AD5941] ADC/FIFO run continuously; MCU extracts each pulse endpoint by time index.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = capture_name;
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_SWV_CODE_LOW;
    afe_swv_sp_code_high = AFE_SWV_CODE_HIGH;
    afe_swv_sp_code_step = AFE_SWV_CODE_STEP;
    afe_swv_sp_step_count = AFE_SWV_SP_STEP_COUNT;
    afe_swv_sp_descending_codes = false;
    swv_sp_clear_phase_expectation();

    ad5941_lptia_default_frontend_config();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config(AFE_SWV_SP_SINC2_OSR, AFE_SWV_SP_SINC2_OSR_TEXT);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();
    ad5941_lptia_gain_set(tiagain_bits, rtia_ohms, rtia_text);

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_CODE_LOW + (step * AFE_SWV_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(settle_clks));
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SAMPLE_CLKS));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(settle_clks));
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_SAMPLE_CLKS));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: SWV-SP sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] SWV-SP sequence loaded to SRAM: SeqId=0, addr=0, len=%u words, expected_logical_points=%u.\n",
           seq_len, AFE_SWV_SINGLE_POINT_COUNT);
    printk("[AD5941] Triggering Sequencer SWV-SP now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(finalize_ms);
    return 0;
}

int App_SeqSWV_SlowMotion_Test_Start(void)
{
    printk("[AD5941] Mode 8: slow-motion SWV-SP keeps 512k RTIA and stretches each half-pulse to 250ms.\n");
    return ad5941_seq_swv_single_point_start("Sequencer SWV-SP-Slow",
                                             AFE_SWV_SLOW_SETTLE_MS,
                                             AFE_SWV_SLOW_SETTLE_CLKS,
                                             ENUM_AFE_LPTIACON0_TIAGAIN512K,
                                             AFE_RTIA_OHMS,
                                             "512k",
                                             AFE_SWV_SLOW_FINALIZE_MS);
}

int App_SeqSWV_LowImpedance_Test_Start(void)
{
    printk("[AD5941] Mode 9: low-impedance SWV-SP uses 20k RTIA with the original 25ms half-pulse.\n");
    return ad5941_seq_swv_single_point_start("Sequencer SWV-SP-LowZ",
                                             AFE_SWV_FAST_SETTLE_MS,
                                             AFE_SWV_FAST_SETTLE_CLKS,
                                             ENUM_AFE_LPTIACON0_TIAGAIN20K,
                                             AFE_RTIA_LOWZ_OHMS,
                                             "20k",
                                             AFE_SWV_LOWZ_FINALIZE_MS);
}

static int ad5941_seq_swv_lptia_external_2m_start(const char *capture_name,
                                                  uint32_t frequency_hz,
                                                  uint32_t half_us,
                                                  uint32_t half_wait_clks,
                                                  uint32_t finalize_ms,
                                                  uint32_t tiagain_bits,
                                                  float rtia_ohms,
                                                  const char *path_text,
                                                  bool boost_enable,
                                                  bool notch_enable)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Starting hardware %s test: f=%uHz, half=%uus, base=0x%03X..0x%03X, step=%u, pulse=+/-0x%02X.\n",
           capture_name,
           frequency_hz,
           half_us,
           AFE_SWV_LPTIA_EXT_CODE_LOW,
           AFE_SWV_LPTIA_EXT_CODE_LOW + ((AFE_SWV_SP_STEP_COUNT - 1U) * AFE_SWV_LPTIA_EXT_CODE_STEP),
           AFE_SWV_LPTIA_EXT_CODE_STEP,
           AFE_SWV_LPTIA_EXT_PULSE_CODE);
    printk("[AD5941] LPTIA path: %s, LPAMP boost=%s.\n",
           path_text, boost_enable ? "BOOST2" : "off");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = capture_name;
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_SWV_LPTIA_EXT_CODE_LOW;
    afe_swv_sp_code_high = AFE_SWV_LPTIA_EXT_CODE_HIGH;
    afe_swv_sp_code_step = AFE_SWV_LPTIA_EXT_CODE_STEP;
    afe_swv_sp_step_count = AFE_SWV_SP_STEP_COUNT;
    afe_swv_sp_descending_codes = false;
    swv_sp_set_dummy_phase_expectation(AFE_SWV_LPTIA_EXT_PULSE_CODE,
                                       AFE_SWV_DUMMY_RESISTOR_OHMS);

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(AFE_SWV_KDM_FAST_SINC2_OSR,
                                    AFE_SWV_KDM_FAST_SINC2_OSR_TEXT,
                                    notch_enable);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();
    ad5941_lptia_external_frontend_prepare(boost_enable);
    AD5940_WriteReg(REG_AFE_LPTIASW0, AFE_LPTIA_EXT_SW);
    ad5941_lptia_boost_gain_set(tiagain_bits, rtia_ohms, path_text, boost_enable);
    printk("[AD5941] LPTIA external feedback switch enabled: LPTIASW0=0x%08X (NORM|SW9).\n",
           AD5940_ReadReg(REG_AFE_LPTIASW0));

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_LPTIA_EXT_CODE_LOW +
                             (step * AFE_SWV_LPTIA_EXT_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_LPTIA_EXT_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(half_wait_clks));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_LPTIA_EXT_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(half_wait_clks));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: LPTIA external SWV sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] LPTIA external SWV sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering LPTIA external SWV now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(finalize_ms);
    return 0;
}

int App_SeqSWV_LPTIAExternal2M_150Hz_Test_Start(void)
{
    printk("[AD5941] Mode B: LPTIA external 2M + 220pF, boost, 150Hz SWV comparison test.\n");
    return ad5941_seq_swv_lptia_external_2m_start("Sequencer SWV-LPTIA-EXT2M-150Hz",
                                                  150U,
                                                  AFE_SWV_LPTIA_EXT_150_HALF_US,
                                                  AFE_SWV_LPTIA_EXT_150_WAIT_CLKS,
                                                  AFE_SWV_LPTIA_EXT_150_FINALIZE_MS,
                                                  ENUM_AFE_LPTIACON0_DISCONTIA,
                                                  AFE_LPTIA_EXT_RTIA_OHMS,
                                                  "internal RTIA=open, external 2M + 220pF expected",
                                                  true,
                                                  false);
}

int App_AutoRepeat_LPTIAExternal2M_150Hz_Test_Start(void)
{
    (void)k_work_cancel_delayable(&afe_repeat_work);

    afe_repeat_active = true;
    afe_repeat_is_dummy = false;
    afe_repeat_current_run = 1U;
    afe_repeat_total_runs = AFE_REPEAT_SWV_RUNS;
    afe_repeat_interval_ms = AFE_REPEAT_SWV_INTERVAL_MS;

    printk("[REPEAT] Auto repeat selected: HSTIA external AIN1 2M, CTIA=4pF, 150Hz SWV.\n");
    printk("[REPEAT] Total runs=%u, interval=%u ms. Run 1 starts now.\n",
           afe_repeat_total_runs, AFE_REPEAT_SWV_INTERVAL_MS);

    if (App_SeqSWV_HSTIAExternalAIN1_Test_Start() != 0) {
        printk("[REPEAT] ERROR: failed to start run 1/%u. Aborting repeat test.\n",
               afe_repeat_total_runs);
        afe_repeat_active = false;
        return -EIO;
    }

    return 0;
}

int App_SeqSWV_LPTIAExternal2M_200Hz_Test_Start(void)
{
    printk("[AD5941] Mode C: LPTIA external 2M + 220pF, boost, 210Hz SWV mains-detune diagnostic.\n");
    return ad5941_seq_swv_lptia_external_2m_start("Sequencer SWV-LPTIA-EXT2M-210Hz",
                                                  210U,
                                                  AFE_SWV_LPTIA_EXT_210_HALF_US,
                                                  AFE_SWV_LPTIA_EXT_210_WAIT_CLKS,
                                                  AFE_SWV_LPTIA_EXT_210_FINALIZE_MS,
                                                  ENUM_AFE_LPTIACON0_DISCONTIA,
                                                  AFE_LPTIA_EXT_RTIA_OHMS,
                                                  "internal RTIA=open, external 2M + 220pF expected",
                                                  true,
                                                  false);
}

int App_SeqSWV_LPTIA512KParallel2MBoost150Hz_Test_Start(void)
{
    printk("[AD5941] Mode D: LPTIA 512k||2M + 220pF, boost, 150Hz SWV diagnostic.\n");
    return ad5941_seq_swv_lptia_external_2m_start("Sequencer SWV-LPTIA-512KPAR-150Hz",
                                                  150U,
                                                  AFE_SWV_LPTIA_EXT_150_HALF_US,
                                                  AFE_SWV_LPTIA_EXT_150_WAIT_CLKS,
                                                  AFE_SWV_LPTIA_EXT_150_FINALIZE_MS,
                                                  ENUM_AFE_LPTIACON0_TIAGAIN512K,
                                                  AFE_LPTIA_512K_PARALLEL_2M_OHMS,
                                                  "internal 512k enabled || external 2M + 220pF expected",
                                                  true,
                                                  false);
}

int App_SeqSWV_LPTIAExternal2M_NoBoost150Hz_Test_Start(void)
{
    printk("[AD5941] Mode E: LPTIA external 2M + 220pF, no boost, 150Hz SWV diagnostic.\n");
    return ad5941_seq_swv_lptia_external_2m_start("Sequencer SWV-LPTIA-EXT2M-NB-150Hz",
                                                  150U,
                                                  AFE_SWV_LPTIA_EXT_150_HALF_US,
                                                  AFE_SWV_LPTIA_EXT_150_WAIT_CLKS,
                                                  AFE_SWV_LPTIA_EXT_150_FINALIZE_MS,
                                                  ENUM_AFE_LPTIACON0_DISCONTIA,
                                                  AFE_LPTIA_EXT_RTIA_OHMS,
                                                  "internal RTIA=open, external 2M + 220pF expected",
                                                  false,
                                                  false);
}

int App_SeqSWV_LPTIAExternal2M_Notch150Hz_Test_Start(void)
{
    printk("[AD5941] Mode G: LPTIA external 2M + 220pF, boost, 150Hz SWV with Notch enabled.\n");
    return ad5941_seq_swv_lptia_external_2m_start("Sequencer SWV-LPTIA-EXT2M-150Hz-Notch",
                                                  150U,
                                                  AFE_SWV_LPTIA_EXT_150_HALF_US,
                                                  AFE_SWV_LPTIA_EXT_150_WAIT_CLKS,
                                                  AFE_SWV_LPTIA_EXT_150_FINALIZE_MS,
                                                  ENUM_AFE_LPTIACON0_DISCONTIA,
                                                  AFE_LPTIA_EXT_RTIA_OHMS,
                                                  "internal RTIA=open, external 2M + 220pF expected",
                                                  true,
                                                  true);
}

static int ad5941_seq_swv_kdm_paper_scan_start(uint8_t stage)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    const char *capture_name;
    const char *stage_text;
    uint32_t frequency_hz;
    uint32_t half_us;
    uint32_t half_wait_clks;
    uint32_t finalize_ms;
    uint32_t sinc2_osr;
    const char *sinc2_osr_text;
    AD5940Err err;

    if (stage == 1U) {
        capture_name = "KDM-SignalOn-150Hz";
        stage_text = "signal-on";
        frequency_hz = 150U;
        half_us = AFE_SWV_KDM_PAPER_150_HALF_US;
        half_wait_clks = AFE_SWV_KDM_PAPER_150_WAIT_CLKS;
        finalize_ms = AFE_SWV_KDM_PAPER_150_FINALIZE_MS;
        sinc2_osr = AFE_SWV_KDM_FAST_SINC2_OSR;
        sinc2_osr_text = AFE_SWV_KDM_FAST_SINC2_OSR_TEXT;
    } else if (stage == 2U) {
        capture_name = "KDM-SignalOff-10Hz";
        stage_text = "signal-off";
        frequency_hz = 10U;
        half_us = AFE_SWV_KDM_PAPER_10_HALF_US;
        half_wait_clks = AFE_SWV_KDM_PAPER_10_WAIT_CLKS;
        finalize_ms = AFE_SWV_KDM_PAPER_10_FINALIZE_MS;
        sinc2_osr = AFE_SWV_SP_SINC2_OSR;
        sinc2_osr_text = AFE_SWV_SP_SINC2_OSR_TEXT;
    } else {
        return -EINVAL;
    }

    printk("[KDM] Starting %s SWV scan: f=%uHz, half=%uus, window=-450mV..0mV, step=0x%02X, pulse=+/-0x%02X.\n",
           stage_text,
           frequency_hz,
           half_us,
           AFE_SWV_KDM_PAPER_CODE_STEP,
           AFE_SWV_KDM_PAPER_PULSE_CODE);
    printk("[KDM] Expected scan time: %u ms plus %u ms finalize margin.\n",
           (AFE_SWV_KDM_PAPER_STEP_COUNT * half_us * 2U) / 1000U,
           AFE_SEQ_FINALIZE_MARGIN_MS);
    printk("[KDM] LPTIA external path: internal RTIA=open, SW9 external 2M + 220pF, BOOST2, Notch bypassed.\n");

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = capture_name;
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_SWV_KDM_PAPER_CODE_LOW;
    afe_swv_sp_code_high = AFE_SWV_KDM_PAPER_CODE_HIGH;
    afe_swv_sp_code_step = AFE_SWV_KDM_PAPER_CODE_STEP;
    afe_swv_sp_step_count = AFE_SWV_KDM_PAPER_STEP_COUNT;
    afe_swv_sp_descending_codes = false;
    swv_sp_clear_phase_expectation();
    afe_active_rtia_ohms = AFE_LPTIA_EXT_RTIA_OHMS;
    afe_kdm_paper_stage = stage;

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(sinc2_osr, sinc2_osr_text, false);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();
    ad5941_lptia_external_frontend_prepare(true);
    AD5940_WriteReg(REG_AFE_LPTIASW0, AFE_LPTIA_EXT_SW);
    ad5941_lptia_boost_gain_set(ENUM_AFE_LPTIACON0_DISCONTIA,
                                AFE_LPTIA_EXT_RTIA_OHMS,
                                "internal RTIA=open, external 2M + 220pF expected",
                                true);
    printk("[KDM] LPTIA external feedback switch enabled: LPTIASW0=0x%08X (NORM|SW9).\n",
           AD5940_ReadReg(REG_AFE_LPTIASW0));

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_KDM_PAPER_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_KDM_PAPER_CODE_LOW +
                             (step * AFE_SWV_KDM_PAPER_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_KDM_PAPER_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(half_wait_clks));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_KDM_PAPER_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(half_wait_clks));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[KDM] ERROR: %s sequence generation failed: err=%d len=%u\n",
               capture_name, err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[KDM] %s sequence loaded: SeqId=0, len=%u words, logical_points=%u.\n",
           capture_name, seq_len, swv_single_point_count());
    printk("[KDM] FIFO watermark IRQ enabled; workqueue drains FIFO during scan, KDM finish drains the tail.\n");
    printk("[KDM] Triggering %s scan now.\n", stage_text);

    AD5940_SEQMmrTrig(SEQID_0);
    int schedule_ret = k_work_schedule_for_queue(&afe_kdm_work_q,
                                                 &afe_kdm_finish_work,
                                                 K_MSEC(finalize_ms));
    printk("[KDM] Finish work scheduled in %u ms, ret=%d.\n", finalize_ms, schedule_ret);
    return 0;
}

static bool blank_peak_tia_is_hstia(enum ad5941_blank_peak_tia_path tia_path)
{
    return tia_path != AD5941_BLANK_PEAK_TIA_LPTIA;
}

static bool blank_peak_tia_is_hstia_external(enum ad5941_blank_peak_tia_path tia_path)
{
    return tia_path == AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K ||
           tia_path == AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K ||
           tia_path == AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K;
}

static float blank_peak_tia_rtia_ohms(enum ad5941_blank_peak_tia_path tia_path)
{
    switch (tia_path) {
    case AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K:
        return AFE_HSTIA_RTIA_OHMS;
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K:
        return AFE_HSTIA_EXT_160K_RTIA_OHMS;
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K:
        return AFE_HSTIA_EXT_330K_RTIA_OHMS;
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K:
        return AFE_HSTIA_EXT_680K_RTIA_OHMS;
    case AD5941_BLANK_PEAK_TIA_LPTIA:
    default:
        return AFE_LPTIA_160K_PARALLEL_2M_OHMS;
    }
}

static const char *blank_peak_tia_label(enum ad5941_blank_peak_tia_path tia_path)
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

static const char *blank_peak_tia_detail_label(enum ad5941_blank_peak_tia_path tia_path)
{
    switch (tia_path) {
    case AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K:
        return "HSTIA internal 160k";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K:
        return "HSTIA external AIN3 160k";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K:
        return "HSTIA external AIN2 330k";
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K:
        return "HSTIA external AIN1 680k";
    case AD5941_BLANK_PEAK_TIA_LPTIA:
    default:
        return "LPTIA 160k||2M";
    }
}

static uint32_t blank_peak_hstia_external_tswitch(enum ad5941_blank_peak_tia_path tia_path)
{
    switch (tia_path) {
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_160K:
        return SWT_AIN3;
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K:
        return SWT_AIN2;
    case AD5941_BLANK_PEAK_TIA_HSTIA_EXT_680K:
    default:
        return SWT_AIN1;
    }
}

static void ad5941_hstia_external_switch_matrix_config(enum ad5941_blank_peak_tia_path tia_path)
{
    SWMatrixCfg_Type sw_cfg;

    AD5940_StructInit(&sw_cfg, sizeof(sw_cfg));
    sw_cfg.Dswitch = SWD_OPEN;
    sw_cfg.Pswitch = SWP_OPEN;
    sw_cfg.Nswitch = SWN_OPEN;
    sw_cfg.Tswitch = SWT_TRTIA | blank_peak_hstia_external_tswitch(tia_path) | SWT_SE0LOAD;
    AD5940_SWMatrixCfgS(&sw_cfg);

    printk("[AD5941] External-HSTIA switch matrix configured for %s: D/P/N=open, T=TRTIA|external RTIA pin|SE0LOAD, TSW=0x%08X.\n",
           blank_peak_tia_detail_label(tia_path),
           AD5940_ReadReg(REG_AFE_TSWFULLCON));
}

static uint32_t blank_peak_segment_scan_ms(uint32_t segment_steps)
{
    return ((segment_steps * afe_blank_peak_segment_half_us * 2U) + 999U) / 1000U;
}

static int ad5941_blank_peak_start_next_segment(void)
{
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    uint32_t segment_start = afe_blank_peak_segment_next_step;
    uint32_t remaining;
    uint32_t segment_steps;
    uint32_t segment_end;
    uint32_t scan_ms;
    uint32_t finalize_ms;
    bool final_segment;
    AD5940Err err;

    if (segment_start >= afe_swv_sp_step_count) {
        return -EINVAL;
    }

    remaining = afe_swv_sp_step_count - segment_start;
    segment_steps = MIN(remaining, afe_blank_peak_segment_max_steps);
    segment_end = segment_start + segment_steps;
    final_segment = segment_end >= afe_swv_sp_step_count;
    scan_ms = blank_peak_segment_scan_ms(segment_steps);
    finalize_ms = scan_ms + AFE_BLANK_PEAK_SEGMENT_MARGIN_MS;

    AD5940_SEQCtrlS(bFALSE);
    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = segment_start; step < segment_end; step++) {
        uint32_t base_code = swv_sp_base_code_for_step(step);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_BLANK_PEAK_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(afe_blank_peak_segment_half_wait_clks));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_BLANK_PEAK_PULSE_CODE),
                            AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(afe_blank_peak_segment_half_wait_clks));
    }

    if (!final_segment && segment_end < afe_swv_sp_step_count) {
        AD5940_LPDAC0WriteS((uint16_t)swv_sp_base_code_for_step(segment_end),
                            AFE_LPDAC_6BIT);
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    if (final_segment && afe_blank_peak_insert_seq_highz_tail) {
        ad5941_seq_insert_safe_idle_tail();
    } else if (final_segment) {
        AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
    }
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[BLANK-PEAK] ERROR: segment sequence generation failed: start=%u steps=%u err=%d len=%u\n",
               segment_start + 1U,
               segment_steps,
               err,
               seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    afe_blank_peak_segment_next_step = segment_end;

    printk("[BLANK-PEAK] Segment %u..%u/%u loaded: len=%u words, scan=%u ms%s.\n",
           segment_start + 1U,
           segment_end,
           afe_swv_sp_step_count,
           seq_len,
           scan_ms,
           final_segment ?
           (afe_blank_peak_insert_seq_highz_tail ?
            ", final segment with safe-idle tail" :
            ", final segment with DAC-zero/legacy stop") :
           "");

    AD5940_SEQCtrlS(bTRUE);
    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(finalize_ms);
    return 0;
}

static int ad5941_hstia_external_frontend_config(enum ad5941_blank_peak_tia_path tia_path,
                                                 float rtia_ohms)
{
    LPDACCfg_Type lp_dac;
    HSTIACfg_Type hstia_cfg;
    uint32_t lpdac_con0;
    AD5940Err err;

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2HSTIA;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bTRUE;
    lp_dac.DacData12Bit = AFE_LPDAC_12BIT;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    lpdac_con0 = AD5940_ReadReg(REG_AFE_LPDACCON0);
    lpdac_con0 |= BITM_AFE_LPDACCON0_DACMDE;
    AD5940_WriteReg(REG_AFE_LPDACCON0, lpdac_con0);
    ad5941_hstia_lp_bias_path_prepare();

    AD5940_StructInit(&hstia_cfg, sizeof(hstia_cfg));
    hstia_cfg.HstiaBias = HSTIABIAS_VZERO0;
    hstia_cfg.HstiaRtiaSel = HSTIARTIA_OPEN;
    hstia_cfg.ExtRtia = (uint32_t)rtia_ohms;
    hstia_cfg.HstiaCtia = AFE_HSTIA_EXT_AIN1_CTIA_CODE;
    hstia_cfg.DiodeClose = bFALSE;
    hstia_cfg.HstiaDeRtia = HSTIADERTIA_TODE;
    hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hstia_cfg.HstiaDe1Rtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDe1Rload = HSTIADERLOAD_OPEN;
    err = AD5940_HSTIACfgS(&hstia_cfg);
    if (err != AD5940ERR_OK) {
        printk("[AD5941] ERROR: %s config failed: %d\n",
               blank_peak_tia_detail_label(tia_path),
               err);
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_HSTIAPWR | AFECTRL_DCBUFPWR, bTRUE);
    ad5941_hstia_external_switch_matrix_config(tia_path);
    AD5940_WriteReg(REG_AFE_LPTIASW0, ENUM_AFE_LPTIASW0_1);
    afe_active_rtia_ohms = rtia_ohms;

    printk("[AD5941] %s configured: RTIA=%lu ohm, CTIA=%upF, LPDACCON0=0x%08X, LPTIACON0=0x%08X, HSTIACON=0x%08X, HSRTIACON=0x%08X, DE0RESCON=0x%08X, TSW=0x%08X, LPTIASW0=0x%08X.\n",
           blank_peak_tia_detail_label(tia_path),
           (unsigned long)rtia_ohms,
           AFE_HSTIA_EXT_AIN1_CTIA_PF,
           AD5940_ReadReg(REG_AFE_LPDACCON0),
           AD5940_ReadReg(REG_AFE_LPTIACON0),
           AD5940_ReadReg(REG_AFE_HSTIACON),
           AD5940_ReadReg(REG_AFE_HSRTIACON),
           AD5940_ReadReg(REG_AFE_DE0RESCON),
           AD5940_ReadReg(REG_AFE_TSWFULLCON),
           AD5940_ReadReg(REG_AFE_LPTIASW0));
    return 0;
}

static int ad5941_seq_swv_blank_peak_start_common(uint32_t frequency_hz,
                                                  bool emit_smoothing,
                                                  enum ad5941_blank_peak_tia_path tia_path,
                                                  enum ad5941_swv_profile profile,
                                                  bool dummy_mode,
                                                  uint32_t dummy_ohms,
                                                  bool insert_seq_highz_tail,
                                                  bool finish_highz)
{
    ADCBaseCfg_Type adc_base;
    const struct swv_profile_cfg *profile_cfg = swv_profile_get(profile);
    uint32_t half_us;
    uint32_t half_wait_clks;
    uint32_t scan_ms;
    uint32_t step_count;
    uint32_t sinc2_osr;
    const char *sinc2_osr_text;
    bool use_hstia;
    bool use_hstia_external;
    float rtia_ohms;

    if (frequency_hz < 1U || frequency_hz > 250U) {
        printk("[BLANK-PEAK] ERROR: frequency must be 1..250 Hz, got %u Hz.\n",
               frequency_hz);
        return -EINVAL;
    }

    half_us = 500000U / frequency_hz;
    if (half_us == 0U) {
        half_us = 1U;
    }
    half_wait_clks = (uint32_t)(16UL * half_us);
    step_count = swv_profile_step_count(profile_cfg->code_step);
    if (step_count == 0U || step_count > AFE_SWV_MAX_STEP_COUNT) {
        printk("[BLANK-PEAK] ERROR: profile %s requires %u points; max buffer is %u.\n",
               profile_cfg->token,
               step_count,
               AFE_SWV_MAX_STEP_COUNT);
        return -EINVAL;
    }
    scan_ms = ((step_count * half_us * 2U) + 999U) / 1000U;
    if (frequency_hz >= 100U) {
        sinc2_osr = AFE_SWV_KDM_FAST_SINC2_OSR;
        sinc2_osr_text = AFE_SWV_KDM_FAST_SINC2_OSR_TEXT;
    } else {
        sinc2_osr = AFE_SWV_SP_SINC2_OSR;
        sinc2_osr_text = AFE_SWV_SP_SINC2_OSR_TEXT;
    }
    use_hstia = blank_peak_tia_is_hstia(tia_path);
    use_hstia_external = blank_peak_tia_is_hstia_external(tia_path);
    rtia_ohms = blank_peak_tia_rtia_ohms(tia_path);

    if (dummy_mode && dummy_ohms == 0U) {
        dummy_ohms = (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS;
    }

    afe_blank_peak_insert_seq_highz_tail = insert_seq_highz_tail;
    afe_finish_highz_enabled = finish_highz;

    printk("[BLANK-PEAK] %s SWV selected.\n",
           dummy_mode ? "Dummy-cell validation" : "Single-frequency PBS/MB blank peak");
    printk("[BLANK-PEAK] Profile=%s (%s), f=%uHz, half=%uus, expected_scan=%u ms, E_WE-RE window=-450mV..0mV, step=0x%02X, pulse=+/-0x%02X, logical_points=%u.\n",
           profile_cfg->token,
           profile_cfg->label,
           frequency_hz,
           half_us,
           scan_ms,
           profile_cfg->code_step,
           AFE_BLANK_PEAK_PULSE_CODE,
           step_count);
    printk("[BLANK-PEAK] Polarity note: WE(SE0)=VZERO, RE(RE0)=VBIAS, so E_WE-RE = VZERO - VBIAS.\n");
    if (use_hstia_external) {
        printk("[BLANK-PEAK] Path: %s, RTIA=%lu ohm, CTIA=%upF, LPPA/LPDAC waveform, Notch bypassed.\n",
               blank_peak_tia_detail_label(tia_path),
               (unsigned long)rtia_ohms,
               AFE_HSTIA_EXT_AIN1_CTIA_PF);
        printk("[BLANK-PEAK] Purpose: compare external thin-film RTIA options against internal HSTIA 160k.\n");
    } else if (tia_path == AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K) {
        printk("[BLANK-PEAK] Path: HSTIA internal 160k, CTIA=%upF, LPPA/LPDAC waveform, Notch bypassed.\n",
               AFE_HSTIA_CTIA_PF);
        printk("[BLANK-PEAK] Purpose: internal 160k reference for external RTIA comparison.\n");
    } else {
        printk("[BLANK-PEAK] Path: LPTIA internal 160k || external 2M + 220pF, SW9, BOOST2, Notch bypassed.\n");
        printk("[BLANK-PEAK] Purpose: lower LPTIA transimpedance anti-saturation check against HSTIA 160k.\n");
    }
    printk("[BLANK-PEAK] Quiet time: hold start potential for %u ms before FIFO/Sequencer scan.\n",
           AFE_BLANK_PEAK_QUIET_MS);
    if (emit_smoothing) {
        printk("[BLANK-PEAK] Extended smoothing output enabled: avg3, smooth5, smooth7, SG5.\n");
    }
    if (dummy_mode) {
        printk("[DUMMY-SWV] Dummy resistor=%u ohm, expected |Delta_I| from +/-pulse = ",
               dummy_ohms);
        print_current_na("", swv_dummy_expected_delta_pa(dummy_ohms));
        printk(".\n");
    }
    if (!insert_seq_highz_tail || !finish_highz) {
        printk("[LEGACY] High-Z guards reduced for comparison: seq_tail=%s, finish_highz=%s.\n",
               insert_seq_highz_tail ? "on" : "off",
               finish_highz ? "on" : "off");
        printk("[LEGACY] Dummy resistor only. Cell remains connected at DAC-zero after capture; press 'u' to force high-Z.\n");
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    (void)k_work_cancel_delayable(&afe_kdm_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = blank_peak_tia_label(tia_path);
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = true;
    afe_blank_peak_emit_smoothing = emit_smoothing;
    afe_blank_peak_segmented_active = true;
    afe_blank_peak_segment_next_step = 0U;
    afe_blank_peak_segment_half_us = half_us;
    afe_blank_peak_segment_half_wait_clks = half_wait_clks;
    afe_blank_peak_segment_max_steps = profile_cfg->segmented ?
                                       AFE_BLANK_PEAK_SEGMENT_MAX_STEPS :
                                       step_count;
    afe_capture_is_dummy_swv = dummy_mode;
    afe_current_swv_profile = profile_cfg->profile;
    afe_dummy_resistor_ohms = dummy_ohms;
    afe_dummy_scan_ms = scan_ms;
    /*
     * LPTIA and HSTIA raw TIA signs are both inverting-amplifier signs
     * (current into the TIA input drives ADC P-N negative). Normalize both
     * paths here so exported SWV data uses one DropSens-style polarity.
     */
    afe_blank_peak_invert_current_sign = true;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_BLANK_PEAK_CODE_LOW;
    afe_swv_sp_code_high = AFE_BLANK_PEAK_CODE_HIGH;
    afe_swv_sp_code_step = profile_cfg->code_step;
    afe_swv_sp_step_count = step_count;
    afe_swv_sp_descending_codes = true;
    if (dummy_mode) {
        swv_sp_set_dummy_phase_expectation(AFE_BLANK_PEAK_PULSE_CODE, (float)dummy_ohms);
    } else {
        swv_sp_clear_phase_expectation();
    }
    afe_active_rtia_ohms = rtia_ohms;
    afe_kdm_paper_active = false;
    afe_kdm_paper_stage = 0U;

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = use_hstia ? ADCMUXP_HSTIA_P : ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = use_hstia ? ADCMUXN_HSTIA_N : ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(sinc2_osr, sinc2_osr_text, false);

    ad5941_sram_fifo_seq_config();
    if (use_hstia_external) {
        if (ad5941_hstia_external_frontend_config(tia_path, rtia_ohms) != 0) {
            return -EIO;
        }
    } else if (tia_path == AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K) {
        if (ad5941_hstia_mixed_frontend_config() != 0) {
            return -EIO;
        }
    } else {
        ad5941_hs_switch_matrix_open();
        ad5941_lptia_external_frontend_prepare(true);
        AD5940_WriteReg(REG_AFE_LPTIASW0, AFE_LPTIA_EXT_SW);
        ad5941_lptia_boost_gain_set(ENUM_AFE_LPTIACON0_TIAGAIN160K,
                                    AFE_LPTIA_160K_PARALLEL_2M_OHMS,
                                    "internal 160k enabled || external 2M + 220pF expected",
                                    true);
        printk("[BLANK-PEAK] LPTIA external feedback switch enabled: LPTIASW0=0x%08X.\n",
               AD5940_ReadReg(REG_AFE_LPTIASW0));
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR |
                    (use_hstia ? (AFECTRL_HSTIAPWR | AFECTRL_DCBUFPWR) : 0),
                    bTRUE);
    AD5940_Delay10us(25);

    AD5940_LPDAC0WriteS((uint16_t)AFE_BLANK_PEAK_QUIET_CODE, AFE_LPDAC_6BIT);
    printk("[BLANK-PEAK] Holding start potential before scan: dac=0x%03X ",
           AFE_BLANK_PEAK_QUIET_CODE);
    print_voltage_mv("E_WE-RE=", lpdac_code_to_we_re_uv((uint16_t)AFE_BLANK_PEAK_QUIET_CODE));
    printk(", quiet=%u ms.\n", AFE_BLANK_PEAK_QUIET_MS);
    k_msleep(AFE_BLANK_PEAK_QUIET_MS);

    printk("[BLANK-PEAK] Sequencer loading: %u steps total, max %u steps/segment%s, logical_points=%u.\n",
           afe_swv_sp_step_count,
           afe_blank_peak_segment_max_steps,
           profile_cfg->segmented ? "" : " (single full segment)",
           swv_single_point_count());
    printk("[BLANK-PEAK] Triggering scan now; RTT CSV will print after all segments finalize.\n");

    {
        int ret = ad5941_blank_peak_start_next_segment();

        if (ret != 0) {
            afe_blank_peak_segmented_active = false;
            ad5941_enter_safe_idle("blank peak segment start error");
        }
        return ret;
    }
}

static int ad5941_seq_swv_blank_peak_start(uint32_t frequency_hz,
                                           bool emit_smoothing,
                                           enum ad5941_blank_peak_tia_path tia_path,
                                           enum ad5941_swv_profile profile,
                                           bool dummy_mode,
                                           uint32_t dummy_ohms)
{
    return ad5941_seq_swv_blank_peak_start_common(frequency_hz,
                                                  emit_smoothing,
                                                  tia_path,
                                                  profile,
                                                  dummy_mode,
                                                  dummy_ohms,
                                                  true,
                                                  true);
}

int App_SeqSWV_BlankPeak_Test_Start(uint32_t frequency_hz)
{
    return ad5941_seq_swv_blank_peak_start(frequency_hz,
                                           false,
                                           AD5941_BLANK_PEAK_TIA_LPTIA,
                                           AD5941_SWV_PROFILE_5MV_FULL,
                                           false,
                                           (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS);
}

int App_SeqSWV_BlankPeakSmoothing_Test_Start(uint32_t frequency_hz)
{
    return ad5941_seq_swv_blank_peak_start(frequency_hz,
                                           true,
                                           AD5941_BLANK_PEAK_TIA_LPTIA,
                                           AD5941_SWV_PROFILE_5MV_FULL,
                                           false,
                                           (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS);
}

int App_SeqSWV_BlankPeakHSTIA160K_Test_Start(uint32_t frequency_hz)
{
    return ad5941_seq_swv_blank_peak_start(frequency_hz,
                                           true,
                                           AD5941_BLANK_PEAK_TIA_HSTIA_INT_160K,
                                           AD5941_SWV_PROFILE_5MV_FULL,
                                           false,
                                           (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS);
}

int App_SeqSWV_BlankPeakSelectableTIA_Test_Start(
    uint32_t frequency_hz,
    enum ad5941_blank_peak_tia_path tia_path)
{
    return ad5941_seq_swv_blank_peak_start(frequency_hz,
                                           true,
                                           tia_path,
                                           AD5941_SWV_PROFILE_5MV_FULL,
                                           false,
                                           (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS);
}

int App_SeqSWV_DummyCell_Test_Start(enum ad5941_swv_profile profile,
                                    uint32_t frequency_hz,
                                    enum ad5941_blank_peak_tia_path tia_path,
                                    uint32_t dummy_ohms)
{
    return ad5941_seq_swv_blank_peak_start(frequency_hz,
                                           true,
                                           tia_path,
                                           profile,
                                           true,
                                           dummy_ohms);
}

int App_SeqSWV_DummyCellLegacyAIN2_Test_Start(uint32_t dummy_ohms)
{
    if (dummy_ohms == 0U) {
        dummy_ohms = (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS;
    }

    printk("[LEGACY] RTT legacy dummy path selected: AIN2 external 330k, 120Hz, 5.37mV step, +/-35mV pulse.\n");
    printk("[LEGACY] This mode skips the menu pre-test soft reset and disables in-sequence/post-finish high-Z.\n");

    return ad5941_seq_swv_blank_peak_start_common(120U,
                                                  true,
                                                  AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K,
                                                  AD5941_SWV_PROFILE_5MV_FULL,
                                                  true,
                                                  dummy_ohms,
                                                  false,
                                                  false);
}

int App_AutoRepeat_DummySWV_Test_Start(enum ad5941_swv_profile profile,
                                       uint32_t frequency_hz,
                                       enum ad5941_blank_peak_tia_path tia_path,
                                       uint32_t dummy_ohms,
                                       uint8_t repeat_count,
                                       uint32_t interval_ms)
{
    const struct swv_profile_cfg *profile_cfg = swv_profile_get(profile);
    int ret;

    if (repeat_count == 0U) {
        repeat_count = AFE_DUMMY_REPEAT_SWV_RUNS;
    }
    if (interval_ms == 0U) {
        interval_ms = AFE_DUMMY_REPEAT_SWV_INTERVAL_MS;
    }
    if (dummy_ohms == 0U) {
        dummy_ohms = (uint32_t)AFE_SWV_DUMMY_RESISTOR_OHMS;
    }

    afe_repeat_active = true;
    afe_repeat_is_dummy = true;
    afe_repeat_current_run = 1U;
    afe_repeat_total_runs = repeat_count;
    afe_repeat_interval_ms = interval_ms;
    afe_repeat_dummy_profile = profile_cfg->profile;
    afe_repeat_dummy_frequency_hz = frequency_hz;
    afe_repeat_dummy_tia_path = tia_path;
    afe_repeat_dummy_ohms = dummy_ohms;

    printk("[REPEAT] Dummy SWV repeat selected: profile=%s, f=%uHz, RTIA=%s, dummy=%u ohm, runs=%u, interval=%u ms.\n",
           profile_cfg->token,
           frequency_hz,
           blank_peak_tia_label(tia_path),
           dummy_ohms,
           repeat_count,
           interval_ms);
    if (ble_service_is_connected()) {
        char ble_line[96];

        snprintk(ble_line,
                 sizeof(ble_line),
                 "EVT,REPEAT,RUN,%u,%u\r\n",
                 afe_repeat_current_run,
                 afe_repeat_total_runs);
        (void)ble_service_send_text(ble_line);
    }

    ret = App_SeqSWV_DummyCell_Test_Start(profile_cfg->profile,
                                          frequency_hz,
                                          tia_path,
                                          dummy_ohms);
    if (ret != 0) {
        afe_repeat_active = false;
        afe_repeat_is_dummy = false;
    }

    return ret;
}

void ad5941_app_enter_high_z_now(void)
{
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    (void)k_work_cancel_delayable(&afe_kdm_finish_work);
    (void)k_work_cancel_delayable(&afe_highz_validation_done_work);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    atomic_clear(&afe_fifo_work_pending);
    ad5941_enter_safe_idle("manual high-Z request");
    if (afe_highz_validation_active) {
        afe_highz_validation_active = false;
        printk("[HIGHZ-TEST] Manual high-Z applied; held electrode potential released.\n");
        if (ble_service_is_connected()) {
            (void)ble_service_send_text("EVT,HZ,RELEASED\r\n");
        }
    }
    atomic_clear(&afe_measurement_busy);
}

void ad5941_app_reset_high_z_validation(void)
{
    printk("[HIGHZ-TEST] Reset requested; releasing held electrode state.\n");
    ad5941_app_soft_reset_for_next_test();
}

static uint16_t highz_validation_code_for_voltage(int32_t voltage_uv)
{
    uint32_t magnitude_uv = voltage_uv < 0 ? (uint32_t)(-voltage_uv) : 0U;
    uint32_t offset = (magnitude_uv * 10U + (AFE_LPDAC_LSB_UV_X10 / 2U)) /
                      AFE_LPDAC_LSB_UV_X10;

    return (uint16_t)(AFE_LPDAC_ZERO_CODE + offset);
}

int ad5941_app_start_high_z_validation(void)
{
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0U;
    uint16_t hold_code;
    AD5940Err err;

    if (ad5941_app_is_measurement_busy() || afe_highz_validation_active) {
        return -EBUSY;
    }

    ad5941_enter_safe_idle("high-Z validation start");
    if (ad5941_hstia_external_frontend_config(
            AD5941_BLANK_PEAK_TIA_HSTIA_EXT_330K,
            AFE_HSTIA_EXT_330K_RTIA_OHMS) != 0) {
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_SEQCtrlS(bFALSE);
    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    for (uint32_t point = 0U; point < AFE_HIGHZ_TEST_POINTS; point++) {
        int32_t voltage_uv = AFE_HIGHZ_TEST_START_UV +
                             ((int32_t)point * AFE_HIGHZ_TEST_STEP_UV);
        uint16_t code = highz_validation_code_for_voltage(voltage_uv);

        AD5940_LPDAC0WriteS(code, AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_HIGHZ_TEST_WAIT_CLKS));
    }

    hold_code = highz_validation_code_for_voltage(AFE_HIGHZ_TEST_HOLD_UV);
    AD5940_LPDAC0WriteS(hold_code, AFE_LPDAC_6BIT);
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        ad5941_enter_safe_idle("high-Z validation sequence error");
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0U;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    afe_highz_validation_active = true;
    atomic_set(&afe_measurement_busy, 1);
    AD5940_SEQCtrlS(bTRUE);
    AD5940_SEQMmrTrig(SEQID_0);
    (void)k_work_schedule_for_queue(&afe_kdm_work_q,
                                    &afe_highz_validation_done_work,
                                    K_MSEC(AFE_HIGHZ_TEST_FINALIZE_MS));

    printk("[HIGHZ-TEST] Started: E_WE-RE approximately -400 mV to -200 mV, %u points, hold=-200 mV, no automatic high-Z.\n",
           AFE_HIGHZ_TEST_POINTS);
    if (ble_service_is_connected()) {
        (void)ble_service_send_text("EVT,STARTING,HIGHZ_TEST,START_MV=-400,END_MV=-200,POINTS=41,HOLD_AFTER_SCAN=1\r\n");
    }
    return 0;
}

int App_LPTIAOffsetDiagnostic_Test_Start(void)
{
    struct offset_diag_stats off_stats;
    struct offset_diag_stats on_stats;
    uint32_t initial_offset;
    uint32_t off_offset;
    uint32_t on_offset;
    int ret;

    printk("[OFFSET] Mode O: LPTIA external 2M zero-offset diagnostic.\n");
    printk("[OFFSET] Path: LPTIA external 2M + 220pF, internal RTIA=open, SW9, BOOST2, 0mV polarization.\n");
    printk("[OFFSET] ADC filter: SINC2 output, Notch bypassed, OSR=%s; capture=%u ms per phase.\n",
           AFE_SWV_KDM_FAST_SINC2_OSR_TEXT, AFE_OFFSET_DIAG_CAPTURE_MS);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    (void)k_work_cancel_delayable(&afe_kdm_finish_work);
    atomic_clear(&afe_fifo_work_pending);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    afe_capture_name = "LPTIA-Offset-Diag";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_blank_peak_emit_smoothing = false;
    afe_capture_is_step_response = false;
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;

    initial_offset = AD5940_ReadReg(REG_AFE_ADCOFFSETLPTIA0);
    printk("[OFFSET] Initial ADCOFFSETLPTIA0=0x%08X.\n", initial_offset);

    printk("[OFFSET] Phase 1/2: offset calibration OFF. Clearing ADCOFFSETLPTIA0 and capturing zero-current baseline...\n");
    ad5941_lptia_offset_register_write(0);
    ad5941_lptia_external_zero_path_restore();
    k_msleep(AFE_OFFSET_DIAG_SETTLE_MS);
    off_offset = AD5940_ReadReg(REG_AFE_ADCOFFSETLPTIA0);
    ret = ad5941_offset_diag_capture("OFF", &off_stats);
    if (ret != 0) {
        printk("[OFFSET] ERROR: OFF capture failed: %d\n", ret);
        ad5941_lptia_offset_register_write(0);
        ad5941_enter_safe_idle("offset diagnostic error");
        return ret;
    }
    ad5941_offset_diag_print_stats("OFF", off_offset, &off_stats);

    printk("[OFFSET] Phase 2/2: running AD5940_LPTIAOffsetCal(), restoring external 2M path, and capturing again...\n");
    ret = ad5941_lptia_offset_calibrate();
    on_offset = AD5940_ReadReg(REG_AFE_ADCOFFSETLPTIA0);
    if (ret != 0) {
        printk("[OFFSET] ERROR: offset calibration failed: %d\n", ret);
        ad5941_lptia_offset_register_write(0);
        ad5941_enter_safe_idle("offset diagnostic error");
        return ret;
    }
    ad5941_lptia_external_zero_path_restore();
    k_msleep(AFE_OFFSET_DIAG_SETTLE_MS);
    ret = ad5941_offset_diag_capture("ON", &on_stats);
    if (ret != 0) {
        printk("[OFFSET] ERROR: ON capture failed: %d\n", ret);
        ad5941_lptia_offset_register_write(0);
        ad5941_enter_safe_idle("offset diagnostic error");
        return ret;
    }
    ad5941_offset_diag_print_stats("ON", on_offset, &on_stats);

    printk("[OFFSET] Difference ON-OFF: ");
    print_current_na("avg_delta=", on_stats.avg_pa - off_stats.avg_pa);
    print_current_na(", std_delta=", on_stats.std_pa - off_stats.std_pa);
    printk("\n");

    ad5941_lptia_offset_register_write(0);
    ad5941_enter_safe_idle("offset diagnostic");
    printk("[OFFSET] Diagnostic complete. ADCOFFSETLPTIA0 cleared back to 0; AFE left high-Z for normal tests.\n");
    return 0;
}

int App_SeqSWV_KDMPaper_Dummy_Test_Start(void)
{
    printk("[KDM] Mode H: dual-frequency paper-style SWV/KDM dummy-cell test selected.\n");
    printk("[KDM] Scan 1: 150Hz signal-on, Scan 2: 10Hz signal-off, LPTIA external 2M + 220pF.\n");
    afe_kdm_paper_active = true;
    afe_kdm_paper_stage = 0U;
    afe_kdm_signal_on_peak_pa = 0;
    afe_kdm_signal_off_peak_pa = 0;
    afe_kdm_signal_on_mean_pa = 0;
    afe_kdm_signal_off_mean_pa = 0;
    return ad5941_seq_swv_kdm_paper_scan_start(1U);
}

static void ad5941_hstia_mixed_switch_matrix_config(void)
{
    SWMatrixCfg_Type sw_cfg;

    AD5940_StructInit(&sw_cfg, sizeof(sw_cfg));
    sw_cfg.Dswitch = SWD_OPEN;
    sw_cfg.Pswitch = SWP_OPEN;
    sw_cfg.Nswitch = SWN_OPEN;
    sw_cfg.Tswitch = SWT_TRTIA | SWT_SE0LOAD;
    AD5940_SWMatrixCfgS(&sw_cfg);

    printk("[AD5941] Mixed-mode HS switch matrix configured: D/P/N=open, T=SWT_TRTIA|SWT_SE0LOAD.\n");
}

static int ad5941_hstia_mixed_frontend_config(void)
{
    LPDACCfg_Type lp_dac;
    HSTIACfg_Type hstia_cfg;
    uint32_t lpdac_con0;
    AD5940Err err;

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2HSTIA;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bTRUE;
    lp_dac.DacData12Bit = AFE_LPDAC_12BIT;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    lpdac_con0 = AD5940_ReadReg(REG_AFE_LPDACCON0);
    lpdac_con0 |= BITM_AFE_LPDACCON0_DACMDE;
    AD5940_WriteReg(REG_AFE_LPDACCON0, lpdac_con0);
    ad5941_hstia_lp_bias_path_prepare();

    AD5940_StructInit(&hstia_cfg, sizeof(hstia_cfg));
    hstia_cfg.HstiaBias = HSTIABIAS_VZERO0;
    hstia_cfg.HstiaRtiaSel = HSTIARTIA_160K;
    hstia_cfg.ExtRtia = 0U;
    hstia_cfg.HstiaCtia = AFE_HSTIA_CTIA_CODE_16PF;
    hstia_cfg.DiodeClose = bFALSE;
    hstia_cfg.HstiaDeRtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hstia_cfg.HstiaDe1Rtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDe1Rload = HSTIADERLOAD_OPEN;
    err = AD5940_HSTIACfgS(&hstia_cfg);
    if (err != AD5940ERR_OK) {
        printk("[AD5941] ERROR: HSTIA mixed-mode config failed: %d\n", err);
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_HSTIAPWR | AFECTRL_DCBUFPWR, bTRUE);
    ad5941_hstia_mixed_switch_matrix_config();
    AD5940_WriteReg(REG_AFE_LPTIASW0, ENUM_AFE_LPTIASW0_1);
    afe_active_rtia_ohms = AFE_HSTIA_RTIA_OHMS;

    printk("[AD5941] Mixed-mode HSTIA configured: RTIA=160k, CTIA=%upF, LPDACCON0=0x%08X, LPTIACON0=0x%08X, HSTIACON=0x%08X, LPTIASW0=0x%08X.\n",
           AFE_HSTIA_CTIA_PF,
           AD5940_ReadReg(REG_AFE_LPDACCON0),
           AD5940_ReadReg(REG_AFE_LPTIACON0),
           AD5940_ReadReg(REG_AFE_HSTIACON),
           AD5940_ReadReg(REG_AFE_LPTIASW0));
    return 0;
}

static void ad5941_hstia_external_ain1_switch_matrix_config(void)
{
    SWMatrixCfg_Type sw_cfg;

    AD5940_StructInit(&sw_cfg, sizeof(sw_cfg));
    sw_cfg.Dswitch = SWD_OPEN;
    sw_cfg.Pswitch = SWP_OPEN;
    sw_cfg.Nswitch = SWN_OPEN;
    sw_cfg.Tswitch = SWT_TRTIA | SWT_AIN1 | SWT_SE0LOAD;
    AD5940_SWMatrixCfgS(&sw_cfg);

    printk("[AD5941] External-HSTIA switch matrix configured: D/P/N=open, T=SWT_TRTIA|SWT_AIN1|SWT_SE0LOAD.\n");
}

static int ad5941_hstia_external_ain1_frontend_config(void)
{
    LPDACCfg_Type lp_dac;
    HSTIACfg_Type hstia_cfg;
    uint32_t lpdac_con0;
    AD5940Err err;

    AD5940_StructInit(&lp_dac, sizeof(lp_dac));
    lp_dac.LpdacSel = LPDAC0;
    lp_dac.LpDacSrc = LPDACSRC_MMR;
    lp_dac.LpDacVbiasMux = LPDACVBIAS_12BIT;
    lp_dac.LpDacVzeroMux = LPDACVZERO_6BIT;
    lp_dac.LpDacSW = LPDACSW_VBIAS2LPPA | LPDACSW_VZERO2HSTIA;
    lp_dac.LpDacRef = LPDACREF_2P5;
    lp_dac.DataRst = bFALSE;
    lp_dac.PowerEn = bTRUE;
    lp_dac.DacData12Bit = AFE_LPDAC_12BIT;
    lp_dac.DacData6Bit = AFE_LPDAC_6BIT;
    AD5940_LPDACCfgS(&lp_dac);

    lpdac_con0 = AD5940_ReadReg(REG_AFE_LPDACCON0);
    lpdac_con0 |= BITM_AFE_LPDACCON0_DACMDE;
    AD5940_WriteReg(REG_AFE_LPDACCON0, lpdac_con0);
    ad5941_hstia_lp_bias_path_prepare();

    AD5940_StructInit(&hstia_cfg, sizeof(hstia_cfg));
    hstia_cfg.HstiaBias = HSTIABIAS_VZERO0;
    hstia_cfg.HstiaRtiaSel = HSTIARTIA_OPEN;
    hstia_cfg.ExtRtia = (uint32_t)AFE_HSTIA_EXT_AIN1_RTIA_OHMS;
    hstia_cfg.HstiaCtia = AFE_HSTIA_EXT_AIN1_CTIA_CODE;
    hstia_cfg.DiodeClose = bFALSE;
    hstia_cfg.HstiaDeRtia = HSTIADERTIA_TODE;
    hstia_cfg.HstiaDeRload = HSTIADERLOAD_OPEN;
    hstia_cfg.HstiaDe1Rtia = HSTIADERTIA_OPEN;
    hstia_cfg.HstiaDe1Rload = HSTIADERLOAD_OPEN;
    err = AD5940_HSTIACfgS(&hstia_cfg);
    if (err != AD5940ERR_OK) {
        printk("[AD5941] ERROR: External AIN1 HSTIA config failed: %d\n", err);
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_HSTIAPWR | AFECTRL_DCBUFPWR, bTRUE);
    ad5941_hstia_external_ain1_switch_matrix_config();
    AD5940_WriteReg(REG_AFE_LPTIASW0, ENUM_AFE_LPTIASW0_1);
    afe_active_rtia_ohms = AFE_HSTIA_EXT_AIN1_RTIA_OHMS;

    printk("[AD5941] External AIN1 HSTIA configured: RTIA=2M, CTIA=%upF, LPDACCON0=0x%08X, LPTIACON0=0x%08X, HSTIACON=0x%08X, HSRTIACON=0x%08X, DE0RESCON=0x%08X, TSW=0x%08X, LPTIASW0=0x%08X.\n",
           AFE_HSTIA_EXT_AIN1_CTIA_PF,
           AD5940_ReadReg(REG_AFE_LPDACCON0),
           AD5940_ReadReg(REG_AFE_LPTIACON0),
           AD5940_ReadReg(REG_AFE_HSTIACON),
           AD5940_ReadReg(REG_AFE_HSRTIACON),
           AD5940_ReadReg(REG_AFE_DE0RESCON),
           AD5940_ReadReg(REG_AFE_TSWFULLCON),
           AD5940_ReadReg(REG_AFE_LPTIASW0));
    return 0;
}

int App_SeqSWV_MixedModeHSTIA_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Mode 10: mixed-mode SWV uses LPDAC sequencer waveform and HSTIA ADC path.\n");
    printk("[AD5941] Starting hardware Sequencer SWV-HSTIA test: steps=%u, pulse=+/-0x%02X, half=%ums, HSTIA_RTIA=160k.\n",
           AFE_SWV_SP_STEP_COUNT, AFE_SWV_PULSE_CODE, AFE_SWV_HSTIA_HALF_MS);
    printk("[AD5941] HSTIA mains diagnostic: one full SWV step is %ums; compare odd/even Delta_I for 50Hz locking.\n",
           AFE_SWV_HSTIA_HALF_MS * 2U);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "Sequencer SWV-HSTIA";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_SWV_CODE_LOW;
    afe_swv_sp_code_high = AFE_SWV_CODE_HIGH;
    afe_swv_sp_code_step = AFE_SWV_CODE_STEP;
    afe_swv_sp_step_count = AFE_SWV_SP_STEP_COUNT;
    afe_swv_sp_descending_codes = false;
    swv_sp_clear_phase_expectation();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_HSTIA_P;
    adc_base.ADCMuxN = ADCMUXN_HSTIA_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config(AFE_SWV_KDM_FAST_SINC2_OSR,
                                 AFE_SWV_KDM_FAST_SINC2_OSR_TEXT);

    ad5941_sram_fifo_seq_config();
    if (ad5941_hstia_mixed_frontend_config() != 0) {
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR | AFECTRL_HSTIAPWR |
                    AFECTRL_DCBUFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_CODE_LOW + (step * AFE_SWV_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_HSTIA_WAIT_CLKS));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_HSTIA_WAIT_CLKS));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: SWV-HSTIA sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] SWV-HSTIA sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer SWV-HSTIA now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_SWV_HSTIA_FINALIZE_MS);
    return 0;
}

static int ad5941_seq_swv_hstia_external_ain1_start(bool notch_enable)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Mode %c: external AIN1 HSTIA 2M, 150Hz SWV %s test.\n",
           notch_enable ? 'F' : 'A',
           notch_enable ? "with Notch enabled" : "comparison");
    printk("[AD5941] Starting hardware Sequencer SWV-HSTIA-EXT-AIN1-150Hz test: f=150Hz, half=%uus, steps=%u, base=0x%03X..0x%03X, step=%u, pulse=+/-0x%02X, HSTIA_EXT_RTIA=2M.\n",
           AFE_SWV_LPTIA_EXT_150_HALF_US,
           AFE_SWV_SP_STEP_COUNT,
           AFE_SWV_LPTIA_EXT_CODE_LOW,
           AFE_SWV_LPTIA_EXT_CODE_LOW + ((AFE_SWV_SP_STEP_COUNT - 1U) * AFE_SWV_LPTIA_EXT_CODE_STEP),
           AFE_SWV_LPTIA_EXT_CODE_STEP,
           AFE_SWV_LPTIA_EXT_PULSE_CODE);
    printk("[AD5941] Uses the same SWV timing, DAC codes, and endpoint extraction as Mode %c.\n",
           notch_enable ? 'G' : 'B');

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = notch_enable ? "Sequencer SWV-HSTIA-EXT-AIN1-150Hz-Notch" :
                                      "Sequencer SWV-HSTIA-EXT-AIN1-150Hz";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = true;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = false;
    afe_swv_endpoint_avg_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_AVG_PCT;
    afe_swv_endpoint_guard_pct = AFE_SWV_LPTIA_EXT_ENDPOINT_GUARD_PCT;
    afe_swv_sp_code_low = AFE_SWV_LPTIA_EXT_CODE_LOW;
    afe_swv_sp_code_high = AFE_SWV_LPTIA_EXT_CODE_HIGH;
    afe_swv_sp_code_step = AFE_SWV_LPTIA_EXT_CODE_STEP;
    afe_swv_sp_step_count = AFE_SWV_SP_STEP_COUNT;
    afe_swv_sp_descending_codes = false;
    swv_sp_set_dummy_phase_expectation(AFE_SWV_LPTIA_EXT_PULSE_CODE,
                                       AFE_SWV_DUMMY_RESISTOR_OHMS);

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_HSTIA_P;
    adc_base.ADCMuxN = ADCMUXN_HSTIA_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(AFE_SWV_KDM_FAST_SINC2_OSR,
                                    AFE_SWV_KDM_FAST_SINC2_OSR_TEXT,
                                    notch_enable);

    ad5941_sram_fifo_seq_config();
    if (ad5941_hstia_external_ain1_frontend_config() != 0) {
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR | AFECTRL_HSTIAPWR |
                    AFECTRL_DCBUFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);

    for (uint32_t step = 0; step < AFE_SWV_SP_STEP_COUNT; step++) {
        uint32_t base_code = AFE_SWV_LPTIA_EXT_CODE_LOW +
                             (step * AFE_SWV_LPTIA_EXT_CODE_STEP);

        AD5940_LPDAC0WriteS((uint16_t)(base_code + AFE_SWV_LPTIA_EXT_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_LPTIA_EXT_150_WAIT_CLKS));

        AD5940_LPDAC0WriteS((uint16_t)(base_code - AFE_SWV_LPTIA_EXT_PULSE_CODE), AFE_LPDAC_6BIT);
        AD5940_SEQGenInsert(SEQ_WAIT(AFE_SWV_LPTIA_EXT_150_WAIT_CLKS));
    }

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: SWV-HSTIA-EXT-AIN1 sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] SWV-HSTIA-EXT-AIN1-150Hz sequence loaded to SRAM: SeqId=0, addr=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering Sequencer SWV-HSTIA-EXT-AIN1-150Hz now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_SWV_LPTIA_EXT_150_FINALIZE_MS);
    return 0;
}

int App_SeqSWV_HSTIAExternalAIN1_Test_Start(void)
{
    return ad5941_seq_swv_hstia_external_ain1_start(false);
}

int App_SeqSWV_HSTIAExternalAIN1_Notch150Hz_Test_Start(void)
{
    return ad5941_seq_swv_hstia_external_ain1_start(true);
}

int App_LPTIAStepResponse_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Mode J: LPTIA external 2M + 220pF endpoint step response test.\n");
    printk("[AD5941] Step response: 0mV for %u ms, then +100mV for %u ms, SINC2 OSR=22.\n",
           AFE_STEP_PRE_MS, AFE_STEP_POST_MS);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "LPTIA Step Response";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = true;
    afe_step_response_setup_text = "LPTIA external RTIA=2M, CTIA=220pF, BOOST2, SINC2 OSR=22, Notch bypassed.";
    swv_sp_clear_phase_expectation();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_LPTIA0_P;
    adc_base.ADCMuxN = ADCMUXN_LPTIA0_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(AFE_SWV_KDM_FAST_SINC2_OSR,
                                    AFE_SWV_KDM_FAST_SINC2_OSR_TEXT,
                                    false);

    ad5941_hs_switch_matrix_open();
    ad5941_sram_fifo_seq_config();
    ad5941_lptia_external_frontend_prepare(true);
    AD5940_WriteReg(REG_AFE_LPTIASW0, AFE_LPTIA_EXT_SW);
    ad5941_lptia_boost_gain_set(ENUM_AFE_LPTIACON0_DISCONTIA,
                                AFE_LPTIA_EXT_RTIA_OHMS,
                                "internal RTIA=open, external 2M + 220pF expected",
                                true);
    printk("[AD5941] LPTIA external feedback switch enabled: LPTIASW0=0x%08X (NORM|SW9).\n",
           AD5940_ReadReg(REG_AFE_LPTIASW0));

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);
    AD5940_SEQGenInsert(SEQ_WAIT(AFE_STEP_PRE_CLKS));
    AD5940_LPDAC0WriteS(AFE_STEP_LPDAC_100MV, AFE_LPDAC_6BIT);
    AD5940_SEQGenInsert(SEQ_WAIT(AFE_STEP_POST_CLKS));
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: step response sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] Step response sequence loaded: SeqId=0, len=%u words.\n", seq_len);
    printk("[AD5941] Triggering LPTIA step response now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_STEP_FINALIZE_MS);
    return 0;
}

int App_HSTIAStepResponse_Test_Start(void)
{
    ADCBaseCfg_Type adc_base;
    SEQInfo_Type seq_info;
    const uint32_t *p_seq_cmd = NULL;
    uint32_t seq_len = 0;
    AD5940Err err;

    printk("[AD5941] Mode K: HSTIA external AIN1 2M + CTIA=%upF endpoint step response test.\n",
           AFE_HSTIA_EXT_AIN1_CTIA_PF);
    printk("[AD5941] Step response: 0mV for %u ms, then +100mV for %u ms, SINC2 OSR=22.\n",
           AFE_STEP_PRE_MS, AFE_STEP_POST_MS);

    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bFALSE);
    (void)k_work_cancel_delayable(&afe_seq_finish_work);
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    atomic_clear(&afe_fifo_work_pending);
    afe_baseline_count = 0;
    afe_latest_current_pa = 0;
    afe_capture_name = "HSTIA Step Response";
    afe_capture_is_cv = false;
    afe_capture_is_swv_kdm = false;
    afe_capture_is_swv_single_point = false;
    afe_capture_is_blank_peak = false;
    afe_capture_is_step_response = true;
    afe_step_response_setup_text = "HSTIA external AIN1 RTIA=2M, CTIA=4pF, SINC2 OSR=22, Notch bypassed.";
    swv_sp_clear_phase_expectation();

    AD5940_StructInit(&adc_base, sizeof(adc_base));
    adc_base.ADCMuxP = ADCMUXP_HSTIA_P;
    adc_base.ADCMuxN = ADCMUXN_HSTIA_N;
    adc_base.ADCPga = AFE_ADC_PGA;
    AD5940_ADCBaseCfgS(&adc_base);

    ad5941_swv_kdm_filter_config_ex(AFE_SWV_KDM_FAST_SINC2_OSR,
                                    AFE_SWV_KDM_FAST_SINC2_OSR_TEXT,
                                    false);

    ad5941_sram_fifo_seq_config();
    if (ad5941_hstia_external_ain1_frontend_config() != 0) {
        return -EIO;
    }

    AD5940_AFECtrlS(AFECTRL_ADCPWR | AFECTRL_HPREFPWR | AFECTRL_HSTIAPWR |
                    AFECTRL_DCBUFPWR, bTRUE);
    AD5940_Delay10us(25);

    AD5940_SEQGenInit(swv_seq_gen_buffer, AFE_SWV_SEQ_BUFFER_WORDS);
    AD5940_SEQGenCtrl(bTRUE);

    AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bTRUE);
    AD5940_SEQGenInsert(SEQ_WAIT(AFE_STEP_PRE_CLKS));
    AD5940_LPDAC0WriteS(AFE_STEP_LPDAC_100MV, AFE_LPDAC_6BIT);
    AD5940_SEQGenInsert(SEQ_WAIT(AFE_STEP_POST_CLKS));
    AD5940_AFECtrlS(AFECTRL_ADCCNV, bFALSE);
    AD5940_LPDAC0WriteS(AFE_LPDAC_ZERO_CODE, AFE_LPDAC_6BIT);
    ad5941_seq_insert_safe_idle_tail();
    AD5940_SEQGenInsert(SEQ_STOP());
    AD5940_SEQGenCtrl(bFALSE);

    err = AD5940_SEQGenFetchSeq(&p_seq_cmd, &seq_len);
    if (err != AD5940ERR_OK || p_seq_cmd == NULL || seq_len == 0U) {
        printk("[AD5941] ERROR: HSTIA step response sequence generation failed: err=%d len=%u\n",
               err, seq_len);
        return -EIO;
    }

    AD5940_StructInit(&seq_info, sizeof(seq_info));
    seq_info.SeqId = SEQID_0;
    seq_info.SeqRamAddr = 0;
    seq_info.SeqLen = seq_len;
    seq_info.WriteSRAM = bTRUE;
    seq_info.pSeqCmd = p_seq_cmd;
    AD5940_SEQInfoCfg(&seq_info);

    ad5941_fifo_reset();
    AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
    AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);

    printk("[AD5941] HSTIA step response sequence loaded: SeqId=0, len=%u words.\n",
           seq_len);
    printk("[AD5941] Triggering HSTIA step response now.\n");

    AD5940_SEQMmrTrig(SEQID_0);
    ad5941_schedule_seq_finish(AFE_STEP_FINALIZE_MS);
    return 0;
}

void ad5941_app_prepare_shutdown(void)
{
    (void)k_work_cancel_delayable(&afe_highz_validation_done_work);
    afe_highz_validation_active = false;
    ad5941_enter_safe_idle("shutdown");
    AD5940_MCUResourceDeInit();
}
