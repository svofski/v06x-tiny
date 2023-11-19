#include "keyboard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i8080.h"

#define SIMULATE_KEY_PRESS 0
// keyboard protocol:
// 32-bit transaction (MSB, mode 0)
//  0:  TX port 03 (PA) value, column select        RX: 0xe5
//  1:  0x55                                        RX: 0xaa
//  2:  TX port 01 (PC) value, ruslat led           RX: bit 1: VVOD, bit 2: SBR, bits 5,6,7 modkeys SS,US,RUSLAT
//  3:                                              RX: rows

namespace keyboard
{

keyboard_state_t state;

static bool transaction_active;
static spi_device_handle_t spimatrix;

static spi_transaction_t transaction;

// spi bus must be initialised!
void init()
{
    state.blk = BLK_MASK;
    state.pc = PC_MODKEYS_MASK;
    state.rows = 0xff;
    state.ruslat = 0;

    spi_device_interface_config_t devcfg = {
        .mode = 0, // should be mode 3 because rp2040 spi is so broken, but somehow mode 0 seems to work better
        .cs_ena_pretrans = 5,
        .clock_speed_hz = 8000000,      // 8mhz seems to be the limit
        .spics_io_num = PIN_NUM_KEYBOARD_SS,
        .queue_size = 1,
    };

    esp_err_t ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spimatrix);
    ESP_ERROR_CHECK(ret);

    transaction_active = false;
    transaction = {};
    transaction.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
    transaction.length = 16; // 3 bytes: command, idle, response
}

void select_columns(uint8_t pa)
{
    if (transaction_active) {
        spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
        //assert(false);
        //return;
    }
    transaction.tx_data[0] = 0xe5; 
    transaction.tx_data[1] = pa;

    //for (int i = 0; i < 4; ++i) transaction.rx_data[i] = 0;
    esp_err_t ret = spi_device_polling_start(spimatrix, &transaction, portMAX_DELAY);
    transaction_active = ret == ESP_OK;
    ESP_ERROR_CHECK(ret);
}

#if SIMULATE_KEY_PRESS
static int dummyctr = 0;
#endif

void read_rows()
{
    esp_err_t ret;
    if (transaction_active) {
        ret = spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
    }
    transaction.tx_data[0] = 0xe6;
    transaction.tx_data[1] = 0;
    
    ret = spi_device_polling_transmit(spimatrix, &transaction);
    state.rows = transaction.rx_data[1];
    //printf("rx: %02x %02x\n", transaction.rx_data[0], transaction.rx_data[1]);
}

void read_modkeys()
{
    esp_err_t ret;
    if (transaction_active) {
        ret = spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
    }
    transaction.tx_data[0] = 0xe7;
    ret = spi_device_polling_transmit(spimatrix, &transaction);
    
    state.pc = transaction.rx_data[0] & PC_MODKEYS_MASK;
    state.blk = transaction.rx_data[0] & BLK_MASK;
}

void out_ruslat(uint8_t w8)
{
    state.ruslat = w8;
    //printf("keyboard::out_ruslat: %02x pc=%04x\n", w8, i8080cpu::i8080_pc());
}

void commit_ruslat()
{
    esp_err_t ret;
    if (transaction_active) {
        ret = spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
    }
    transaction.tx_data[0] = 0xe8;
    transaction.tx_data[1] = state.ruslat ^ PC_BIT_INDRUS;  // apply inverter D81.3
    ret = spi_device_polling_start(spimatrix, &transaction, portMAX_DELAY);
    transaction_active = true;
}

bool vvod_pressed()
{
    return (state.blk & BLK_BIT_VVOD) == 0;
}

bool sbros_pressed()
{
    return (state.blk & BLK_MASK) == (BLK_MASK & ~BLK_BIT_SBROS); // SBROS without VVOD
}

}