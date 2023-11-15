#include <string.h>
//#include "driver/spi_common_internal.h"
//#include "driver/spi_master.h"
#include "soc/spi_periph.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/xtensa_api.h"
#include "freertos/task.h"
#include "soc/soc_memory_layout.h"
//#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "stdatomic.h"
#include "sdkconfig.h"
#include "hal/spi_hal.h"

void onebyte_tx(uint8_t w8)
{
    spi_hal_usr_is_done(0);//&host->hal);
}

