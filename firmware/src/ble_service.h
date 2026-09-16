/*
 * NanoStat BLE service layer.
 *
 * First BLE revision uses Nordic UART Service (NUS) so it can be tested with
 * Nordic phone tools and simple Web Bluetooth clients before a custom GATT
 * protocol is finalized.
 */
#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

int ble_service_init(void);
bool ble_service_is_connected(void);
int ble_service_send_text(const char *text);
int ble_service_send_sample(uint32_t sample_count, int32_t current_pa);

#endif /* BLE_SERVICE_H */
