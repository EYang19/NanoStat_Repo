/*
 * oled_display.c
 * Minimal raw SSD1315/SSD1306 I2C text helper for NanoStat.
 *
 * This intentionally bypasses Zephyr's display/CFB stack. The gm009605 v4.3
 * OLED module accepts SSD1306-compatible commands, but its CFB path did not
 * map text reliably. Direct page writes are stable on this hardware.
 */

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "oled_display.h"

#define OLED_NODE       DT_NODELABEL(ssd1306)
#define OLED_WIDTH      128U
#define OLED_PAGES      8U
#define OLED_CHAR_W     6U

BUILD_ASSERT(DT_NODE_EXISTS(OLED_NODE), "Missing devicetree node label: ssd1306");

static const struct i2c_dt_spec oled_i2c = I2C_DT_SPEC_GET(OLED_NODE);
static bool oled_ready;

static int oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};

    return i2c_write_dt(&oled_i2c, buf, sizeof(buf));
}

static int oled_write_cmds(const uint8_t *cmds, size_t len)
{
    uint8_t buf[32];

    if (len > (sizeof(buf) - 1U)) {
        return -EINVAL;
    }

    buf[0] = 0x00;
    memcpy(&buf[1], cmds, len);

    return i2c_write_dt(&oled_i2c, buf, len + 1U);
}

static int oled_write_data(const uint8_t *data, size_t len)
{
    uint8_t buf[OLED_WIDTH + 1U];

    if (len > OLED_WIDTH) {
        return -EINVAL;
    }

    buf[0] = 0x40;
    memcpy(&buf[1], data, len);

    return i2c_write_dt(&oled_i2c, buf, len + 1U);
}

static int oled_set_page_col(uint8_t page, uint8_t col)
{
    int ret;

    ret = oled_write_cmd(0xB0U | (page & 0x07U));
    if (ret != 0) {
        return ret;
    }

    ret = oled_write_cmd(0x00U | (col & 0x0FU));
    if (ret != 0) {
        return ret;
    }

    return oled_write_cmd(0x10U | ((col >> 4) & 0x0FU));
}

static int oled_clear_all(void)
{
    uint8_t zeros[OLED_WIDTH];

    memset(zeros, 0, sizeof(zeros));

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        int ret = oled_set_page_col(page, 0);

        if (ret != 0) {
            return ret;
        }

        ret = oled_write_data(zeros, sizeof(zeros));
        if (ret != 0) {
            return ret;
        }
    }

    return 0;
}

static int oled_init_controller(void)
{
    static const uint8_t init_cmds[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide */
        0xA8, 0x3F, /* multiplex 1/64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* display start line */
        0x8D, 0x14, /* charge pump on */
        0x20, 0x02, /* page addressing mode */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction remap */
        0xDA, 0x12, /* COM pins: 128x64 common layout */
        0x81, 0x8F, /* contrast */
        0xD9, 0xF1, /* precharge */
        0xDB, 0x40, /* VCOMH deselect */
        0xA4,       /* output follows RAM */
        0xA6,       /* normal display */
        0x2E,       /* deactivate scroll */
    };

    int ret = oled_write_cmds(init_cmds, sizeof(init_cmds));

    if (ret != 0) {
        return ret;
    }

    ret = oled_clear_all();
    if (ret != 0) {
        return ret;
    }

    return oled_write_cmd(0xAF);
}

static const uint8_t *glyph_for_char(char c)
{
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};

    static const uint8_t glyph_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t glyph_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t glyph_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t glyph_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t glyph_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t glyph_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t glyph_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t glyph_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t glyph_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t glyph_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};

    static const uint8_t glyph_a[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t glyph_b[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t glyph_c[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t glyph_d[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t glyph_e[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t glyph_f[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t glyph_g[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t glyph_h[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t glyph_i[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t glyph_j[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    static const uint8_t glyph_k[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t glyph_l[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t glyph_m[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t glyph_n[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t glyph_o[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t glyph_p[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t glyph_q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
    static const uint8_t glyph_r[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t glyph_s[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t glyph_t[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t glyph_u[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t glyph_v[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t glyph_w[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
    static const uint8_t glyph_x[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static const uint8_t glyph_y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
    static const uint8_t glyph_z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

    static const uint8_t glyph_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t glyph_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t glyph_minus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};

    switch ((char)toupper((unsigned char)c)) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_a;
    case 'B': return glyph_b;
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'E': return glyph_e;
    case 'F': return glyph_f;
    case 'G': return glyph_g;
    case 'H': return glyph_h;
    case 'I': return glyph_i;
    case 'J': return glyph_j;
    case 'K': return glyph_k;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'N': return glyph_n;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'Q': return glyph_q;
    case 'R': return glyph_r;
    case 'S': return glyph_s;
    case 'T': return glyph_t;
    case 'U': return glyph_u;
    case 'V': return glyph_v;
    case 'W': return glyph_w;
    case 'X': return glyph_x;
    case 'Y': return glyph_y;
    case 'Z': return glyph_z;
    case ':': return glyph_colon;
    case '.': return glyph_dot;
    case '-': return glyph_minus;
    case ' ': return blank;
    default: return question;
    }
}

static int oled_print_page(uint8_t page, uint8_t col, const char *text)
{
    while (*text != '\0' && col <= (OLED_WIDTH - OLED_CHAR_W)) {
        uint8_t bytes[OLED_CHAR_W];
        const uint8_t *glyph = glyph_for_char(*text++);
        int ret;

        memcpy(bytes, glyph, 5U);
        bytes[5] = 0x00;

        ret = oled_set_page_col(page, col);
        if (ret != 0) {
            return ret;
        }

        ret = oled_write_data(bytes, sizeof(bytes));
        if (ret != 0) {
            return ret;
        }

        col += OLED_CHAR_W;
    }

    return 0;
}

static void format_current_na(char *buf, size_t len, int32_t current_pa)
{
    int32_t abs_pa = current_pa < 0 ? -current_pa : current_pa;
    const char *sign = current_pa < 0 ? "-" : "";

    (void)snprintk(buf, len, "%s%d.%03d nA", sign, abs_pa / 1000, abs_pa % 1000);
}

int oled_display_init(void)
{
    int ret;

    if (!device_is_ready(oled_i2c.bus)) {
        printk("[OLED] I2C bus is not ready.\n");
        return -ENODEV;
    }

    k_msleep(250);

    ret = oled_init_controller();
    if (ret != 0) {
        printk("[OLED] SSD1315 raw init failed: %d\n", ret);
        return ret;
    }

    oled_ready = true;
    oled_display_show_boot("Booting");

    printk("[OLED] SSD1315 raw display initialized.\n");
    return 0;
}

bool oled_display_is_ready(void)
{
    return oled_ready;
}

void oled_display_show_boot(const char *status)
{
    if (!oled_ready) {
        return;
    }

    (void)oled_clear_all();
    (void)oled_print_page(0, 0, "NanoStat Ready");
    (void)oled_print_page(2, 0, status != NULL ? status : "Starting");
}

void oled_display_show_afe_sample(uint32_t sample_count, int32_t current_pa)
{
    char line[24];
    char current_text[18];

    if (!oled_ready) {
        return;
    }

    format_current_na(current_text, sizeof(current_text), current_pa);

    (void)oled_clear_all();
    (void)oled_print_page(0, 0, "NanoStat Ready");

    (void)snprintk(line, sizeof(line), "AFE samples:%lu", (unsigned long)sample_count);
    (void)oled_print_page(2, 0, line);

    (void)snprintk(line, sizeof(line), "I:%s", current_text);
    (void)oled_print_page(4, 0, line);

    (void)oled_print_page(6, 0, "RTT stream active");
}
