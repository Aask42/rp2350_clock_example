#ifndef __DS3231_H
#define __DS3231_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define rtc_address 0x68

/*the first verison use i2c1(GP6,GP7)*/
/*the new vesion use i2c0(GP20,GP21)*/
#define I2C_PORT_RTC	i2c0
#define I2C_SCL_RTC		20	
#define I2C_SDA_RTC		21

int ds3231SetTime();
void ds3231ReadTime();

#endif

