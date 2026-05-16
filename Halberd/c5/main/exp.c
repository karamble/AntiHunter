#include "exp.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hardware.h"
#include "link.h"
#include "link_protocol.h"

static const char *TAG = "exp";

#define EXP_I2C_FREQ_HZ      100000   // 100 kHz, standard-mode Qwiic
#define EXP_I2C_PORT         I2C_NUM_0
#define EXP_I2C_TIMEOUT_MS   100      // per-transaction timeout

static i2c_master_bus_handle_t s_i2c_bus;
static bool s_i2c_ready;

// 128-bit bitmap of I²C addresses that ACKed during the most recent
// scan. addr / 8 indexes the byte; addr % 8 indexes the bit. exp_init()
// fills this from the boot-time scan; exp_i2c_rescan() refreshes it.
static uint8_t s_i2c_present[16];

// EXP_GPIO0..4 → ESP32-C5 GPIO numbers, per hardware.h.
static const gpio_num_t s_exp_pins[LINK_EXP_GPIO_COUNT] = {
    HALBERD_C5_EXP_GPIO0,
    HALBERD_C5_EXP_GPIO1,
    HALBERD_C5_EXP_GPIO2,
    HALBERD_C5_EXP_GPIO3,
    HALBERD_C5_EXP_GPIO4,
};

static uint8_t map_status(esp_err_t err) {
    switch (err) {
    case ESP_OK:                  return LINK_EXP_STATUS_OK;
    case ESP_ERR_INVALID_ARG:     return LINK_EXP_STATUS_BAD_PARAM;
    case ESP_ERR_TIMEOUT:         return LINK_EXP_STATUS_TIMEOUT;
    case ESP_ERR_NOT_FOUND:
    case ESP_FAIL:                return LINK_EXP_STATUS_NACK;
    case ESP_ERR_INVALID_STATE:   return LINK_EXP_STATUS_NOT_READY;
    default:                      return LINK_EXP_STATUS_NACK;
    }
}

// ── I²C handlers ───────────────────────────────────────────────────────────
static void on_i2c_read(const struct link_i2c_read_req *req) {
    struct link_i2c_read_resp resp = {0};
    resp.request_id = req->request_id;

    if (!s_i2c_ready) {
        resp.status = LINK_EXP_STATUS_NOT_READY;
        link_send_i2c_read_resp(&resp);
        return;
    }
    if (req->read_len == 0 || req->read_len > LINK_I2C_DATA_MAX) {
        resp.status = LINK_EXP_STATUS_BAD_PARAM;
        link_send_i2c_read_resp(&resp);
        return;
    }

    i2c_device_config_t devcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = req->device_addr,
        .scl_speed_hz    = EXP_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &devcfg, &dev);
    if (err != ESP_OK) {
        resp.status = map_status(err);
        link_send_i2c_read_resp(&resp);
        return;
    }

    uint8_t buf[LINK_I2C_DATA_MAX] = {0};
    if (req->reg_present) {
        uint8_t reg = req->reg;
        err = i2c_master_transmit_receive(dev, &reg, 1, buf, req->read_len,
                                          EXP_I2C_TIMEOUT_MS);
    } else {
        err = i2c_master_receive(dev, buf, req->read_len, EXP_I2C_TIMEOUT_MS);
    }
    i2c_master_bus_rm_device(dev);

    resp.status = map_status(err);
    if (err == ESP_OK) {
        resp.read_len = req->read_len;
        memcpy(resp.data, buf, req->read_len);
    }
    link_send_i2c_read_resp(&resp);
}

static void on_i2c_write(const struct link_i2c_write_req *req) {
    struct link_i2c_write_resp resp = {0};
    resp.request_id = req->request_id;

    if (!s_i2c_ready) {
        resp.status = LINK_EXP_STATUS_NOT_READY;
        link_send_i2c_write_resp(&resp);
        return;
    }
    size_t total = (req->reg_present ? 1u : 0u) + req->data_len;
    if (total == 0 || total > LINK_I2C_DATA_MAX + 1) {
        resp.status = LINK_EXP_STATUS_BAD_PARAM;
        link_send_i2c_write_resp(&resp);
        return;
    }

    i2c_device_config_t devcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = req->device_addr,
        .scl_speed_hz    = EXP_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &devcfg, &dev);
    if (err != ESP_OK) {
        resp.status = map_status(err);
        link_send_i2c_write_resp(&resp);
        return;
    }

    uint8_t buf[LINK_I2C_DATA_MAX + 1];
    size_t off = 0;
    if (req->reg_present) {
        buf[off++] = req->reg;
    }
    if (req->data_len > 0) {
        memcpy(buf + off, req->data, req->data_len);
        off += req->data_len;
    }
    err = i2c_master_transmit(dev, buf, off, EXP_I2C_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);

    resp.status = map_status(err);
    link_send_i2c_write_resp(&resp);
}

// ── GPIO handler ───────────────────────────────────────────────────────────
static esp_err_t apply_mode(gpio_num_t pin, uint8_t mode) {
    gpio_config_t cfg = {0};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.intr_type    = GPIO_INTR_DISABLE;

    switch (mode) {
    case LINK_GPIO_MODE_INPUT:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case LINK_GPIO_MODE_OUTPUT:
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case LINK_GPIO_MODE_INPUT_PULLUP:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case LINK_GPIO_MODE_INPUT_PULLDOWN:
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    case LINK_GPIO_MODE_OPEN_DRAIN:
        cfg.mode = GPIO_MODE_OUTPUT_OD;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_config(&cfg);
}

static void on_gpio_req(const struct link_gpio_req *req) {
    struct link_gpio_resp resp = {0};
    resp.request_id = req->request_id;

    if (req->pin_index >= LINK_EXP_GPIO_COUNT) {
        resp.status = LINK_EXP_STATUS_BAD_PARAM;
        link_send_gpio_resp(&resp);
        return;
    }
    gpio_num_t pin = s_exp_pins[req->pin_index];

    esp_err_t err = ESP_OK;
    switch (req->op) {
    case LINK_GPIO_OP_CONFIG:
        err = apply_mode(pin, req->mode);
        break;
    case LINK_GPIO_OP_WRITE:
        err = gpio_set_level(pin, req->value ? 1 : 0);
        break;
    case LINK_GPIO_OP_READ: {
        int lvl = gpio_get_level(pin);
        resp.value = (lvl != 0) ? 1 : 0;
        break;
    }
    default:
        err = ESP_ERR_INVALID_ARG;
        break;
    }

    resp.status = map_status(err);
    link_send_gpio_resp(&resp);
}

void exp_init(void) {
    // v5 carrier has NO I²C pull-ups (see feedback_c5_i2c_pullups);
    // we rely on the plugged-in Qwiic / STEMMA QT module's onboard
    // pulls. Internal pull-ups stay off for the same reason
    // (typical module pulls are 4.7-10 kΩ; doubling with our internal
    // ~45 kΩ wouldn't help but isn't necessary either).
    i2c_master_bus_config_t cfg = {0};
    cfg.i2c_port = EXP_I2C_PORT;
    cfg.sda_io_num = HALBERD_C5_EXP_SDA_GPIO;
    cfg.scl_io_num = HALBERD_C5_EXP_SCL_GPIO;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = false;

    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init: %s", esp_err_to_name(err));
        s_i2c_ready = false;
    } else {
        s_i2c_ready = true;
    }

    link_register_i2c_read_req(on_i2c_read);
    link_register_i2c_write_req(on_i2c_write);
    link_register_gpio_req(on_gpio_req);

    ESP_LOGI(TAG, "init i2c(sda=%d,scl=%d,%dHz) gpio[%d,%d,%d,%d,%d]",
             HALBERD_C5_EXP_SDA_GPIO, HALBERD_C5_EXP_SCL_GPIO, EXP_I2C_FREQ_HZ,
             (int)s_exp_pins[0], (int)s_exp_pins[1], (int)s_exp_pins[2],
             (int)s_exp_pins[3], (int)s_exp_pins[4]);

    // Boot-time I²C bus scan. Probes every 7-bit address and reports
    // responders. Logs to the C5's USB Serial/JTAG console so an
    // operator can see what's plugged into J_EXP / J_QWIIC without
    // any S3 involvement. Results land in s_i2c_present so the sensor
    // framework's manifest loader can skip probe attempts at empty
    // addresses without re-scanning.
    if (s_i2c_ready) {
        exp_i2c_rescan();
    }
}

bool exp_i2c_addr_present(uint8_t addr) {
    if (addr > 0x7F) return false;
    return (s_i2c_present[addr / 8] & (1u << (addr % 8))) != 0;
}

void exp_i2c_rescan(void) {
    if (!s_i2c_ready) return;
    memset(s_i2c_present, 0, sizeof(s_i2c_present));
    int found = 0;
    ESP_LOGI(TAG, "i2c scan: probing 0x08..0x77 ...");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 50) == ESP_OK) {
            s_i2c_present[addr / 8] |= (1u << (addr % 8));
            const char *hint = "";
            if (addr == 0x62) hint = " (Grove Vision AI V2 / WE-2)";
            else if (addr == 0x28) hint = " (Grove Vision AI camera sensor?)";
            ESP_LOGI(TAG, "i2c scan: found 0x%02X%s", addr, hint);
            found++;
        }
    }
    ESP_LOGI(TAG, "i2c scan: done, %d device(s)", found);
}

esp_err_t exp_i2c_xfer(uint8_t addr,
                       const uint8_t *write_buf, size_t write_len,
                       uint8_t *read_buf, size_t read_len) {
    if (!s_i2c_ready) return ESP_ERR_INVALID_STATE;
    if (write_len == 0 && read_len == 0) return ESP_ERR_INVALID_ARG;
    if ((write_len > 0 && !write_buf) || (read_len > 0 && !read_buf)) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_device_config_t devcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = EXP_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &devcfg, &dev);
    if (err != ESP_OK) return err;

    if (write_len > 0 && read_len > 0) {
        err = i2c_master_transmit_receive(dev, write_buf, write_len,
                                          read_buf, read_len,
                                          EXP_I2C_TIMEOUT_MS);
    } else if (write_len > 0) {
        err = i2c_master_transmit(dev, write_buf, write_len, EXP_I2C_TIMEOUT_MS);
    } else {
        err = i2c_master_receive(dev, read_buf, read_len, EXP_I2C_TIMEOUT_MS);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}
