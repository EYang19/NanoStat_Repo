#ifndef POWER_CONFIG_H_
#define POWER_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the system rails and power-button listener.
 *
 * Enables the AD5941 digital and analogue rails in order (LS1, then LDO2)
 * and installs the P1.05 SLP-button interrupt for a three-second shutdown.
 *
 * @return 0 on success, or a negative error if a device binding fails.
 */
int board_power_system_init(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_CONFIG_H_ */
