#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include "driver/i2c_master.h"

void i2c_scan();
i2c_master_bus_handle_t get_i2c_bus();   // per condividere il bus con altri dispositivi

#endif