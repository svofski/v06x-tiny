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

keyboard_state_t state;         // main state
keyboard_state_t io_state;      // gated io state (all off when osd is showing)

// matrix for osd-mode: 8 rows + mods
matrix_t rows;
matrix_t last_rows;

bool osd_enable;

static bool transaction_active;
static spi_device_handle_t spimatrix;

static spi_transaction_t transaction;

// spi bus must be initialised!
void init()
{
    osd_enable = false;

    state.blk = BLK_MASK;
    state.pc = PC_MODKEYS_MASK;
    state.rows = 0xff;
    state.ruslat = 0;
    io_state = state;

    spi_device_interface_config_t devcfg = {
        .mode = 0, // should be mode 3 because rp2040 spi is so broken, but somehow mode 0 seems to work better
        .cs_ena_pretrans = 6,
        .clock_speed_hz = 9000000,      // 8mhz seems to be the limit
        .spics_io_num = PIN_NUM_KEYBOARD_SS,
        .queue_size = 1,
    };

    esp_err_t ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spimatrix);
    ESP_ERROR_CHECK(ret);

    transaction_active = false;
    transaction = {};
    transaction.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
    transaction.length = 16; // 3 bytes: command, idle, response

    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i] = last_rows[i] = 0xff;
    }
}

void select_columns(uint8_t pa)
{
    if (transaction_active) {
        spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
    }
    transaction.tx_data[0] = 0xe5; 
    transaction.tx_data[1] = pa;

    esp_err_t ret = spi_device_polling_start(spimatrix, &transaction, portMAX_DELAY);
    transaction_active = ret == ESP_OK;
    ESP_ERROR_CHECK(ret);
}

void read_rows()
{
    if (transaction_active) {
        spi_device_polling_end(spimatrix, portMAX_DELAY);
        transaction_active = false;
    }
    transaction.tx_data[0] = 0xe6;
    transaction.tx_data[1] = 0;

    // while i can't figure a better way to flush tx fifo on slave, we perform double reads, second one's a charm
    // to improve performance of two back-to-back transactions, acquire bus
    spi_device_acquire_bus(spimatrix, portMAX_DELAY);
    spi_device_polling_transmit(spimatrix, &transaction);
    spi_device_polling_transmit(spimatrix, &transaction);
    spi_device_release_bus(spimatrix);
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
    
    //printf("m: %02x %02x\n", transaction.rx_data[0], transaction.rx_data[1]);
    state.pc = transaction.rx_data[0] & PC_MODKEYS_MASK;
    state.blk = transaction.rx_data[0] & BLK_MASK;
//
//    // update io modkeys automatically
//    if (!osd_enable) {
//        io_state.pc = state.pc;
//        io_state.blk = state.blk;
//    }
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


IRAM_ATTR
void io_select_columns(uint8_t pa)
{
    if (!osd_enable) select_columns(pa);
}

IRAM_ATTR
void io_read_rows()
{
    if (!osd_enable) {
        read_rows();
        io_state.rows = state.rows;
    }
}

void io_read_modkeys()
{
    if (!osd_enable) {
        read_modkeys();
        io_state.pc = state.pc;
        io_state.blk = state.blk;
        //printf("io_m: pc=%02x blk=%02x\n", io_state.pc, io_state.blk);
    }
}

void io_out_ruslat(uint8_t w8)
{
    if (!osd_enable) out_ruslat(w8);
}

void io_commit_ruslat()
{
    if (!osd_enable) commit_ruslat();
}

// when osd takes over keyboard control, block all io_*() calls
void osd_takeover(bool enable)
{
    osd_enable = enable;
    io_state.blk = BLK_MASK;
    io_state.pc = PC_MODKEYS_MASK;
    io_state.rows = 0xff;
}

void scan_matrix()
{
    int shifter = 1;
    for (int i = 0; i < 8; ++i, shifter <<= 1)
    {
        keyboard::select_columns(shifter ^ 0xff);
        keyboard::read_rows();
        rows[i] = keyboard::state.rows;
    }
    keyboard::read_modkeys();
    rows[8] = keyboard::state.pc | keyboard::state.blk;
}

// Keyboard encoding matrix:
//   │ 7   6   5   4   3   2   1   0
// ──┼───────────────────────────────
// 7 │SPC  ^   ]   \   [   Z   Y   X
// 6 │ W   V   U   T   S   R   Q   P
// 5 │ O   N   M   L   K   J   I   H
// 4 │ G   F   E   D   C   B   A   @
// 3 │ /   .   =   ,   ;   :   9   8
// 2 │ 7   6   5   4   3   2   1   0
// 1 │F5  F4  F3  F2  F1  AP2 CTP ^\ -
// 0 │DN  RT  UP  LT  ЗАБ ВК  ПС  TAB

/* scancode, column|bit, char  */
static const int keymap_tab[] = {
        SCANCODE_RUSLAT,        0x880,  0,      // rus/lat
        SCANCODE_US,            0x840,  0,      // ctrl
        SCANCODE_SS,            0x820,  0,      // shift
        0,                      0,      0,
        0,                      0,      0,
        0,                      0,      0,
        SCANCODE_SBROS,         0x802,  0,      // blk+sbr
        SCANCODE_VVOD,          0x801,  0,      // blk+vvod
        
        SCANCODE_SPACE,         0x780,  ' ',
        SCANCODE_GRAVE,         0x740,  '^',
        SCANCODE_RIGHTBRACKET,  0x720,  ']',
        SCANCODE_BACKSLASH,     0x710,  '\\',
        SCANCODE_LEFTBRACKET,   0x708,  '[',
        SCANCODE_Z,             0x704,  'Z',
        SCANCODE_Y,             0x702,  'Y',
        SCANCODE_X,             0x701,  'X',

        SCANCODE_W,             0x680,  'W',
        SCANCODE_V,             0x640,  'V',
        SCANCODE_U,             0x620,  'U',
        SCANCODE_T,             0x610,  'T',
        SCANCODE_S,             0x608,  'S',
        SCANCODE_R,             0x604,  'R',
        SCANCODE_Q,             0x602,  'Q',
        SCANCODE_P,             0x601,  'P',

        SCANCODE_O,             0x580,  'O',
        SCANCODE_N,             0x540,  'N',
        SCANCODE_M,             0x520,  'M',
        SCANCODE_L,             0x510,  'L',
        SCANCODE_K,             0x508,  'K',
        SCANCODE_J,             0x504,  'J',
        SCANCODE_I,             0x502,  'I',
        SCANCODE_H,             0x501,  'H',

        SCANCODE_G,             0x480,  'G',
        SCANCODE_F,             0x440,  'F',
        SCANCODE_E,             0x420,  'E',
        SCANCODE_D,             0x410,  'D',
        SCANCODE_C,             0x408,  'C',
        SCANCODE_B,             0x404,  'B',
        SCANCODE_A,             0x402,  'A',
        SCANCODE_AT,            0x401,  '@',

        SCANCODE_SLASH,         0x380,  '/',
        SCANCODE_PERIOD,        0x340,  '.',
        SCANCODE_MINUS,         0x320,  '-',
        SCANCODE_COMMA,         0x310,  ',',
        SCANCODE_SEMICOLON,     0x308,  ';',
        SCANCODE_APOSTROPHE,    0x304,  '\'',
        SCANCODE_9,             0x302,  '9',
        SCANCODE_8,             0x301,  '8',

        SCANCODE_7,             0x280,  '7',
        SCANCODE_6,             0x240,  '6',
        SCANCODE_5,             0x220,  '5',
        SCANCODE_4,             0x210,  '4',
        SCANCODE_3,             0x208,  '3',
        SCANCODE_2,             0x204,  '2',
        SCANCODE_1,             0x202,  '1',
        SCANCODE_0,             0x201,  '0',

        SCANCODE_F5,            0x180,  25, 
        SCANCODE_F4,            0x140,  24,
        SCANCODE_F3,            0x120,  23,
        SCANCODE_F2,            0x110,  22,
        SCANCODE_F1,            0x108,  21,
        SCANCODE_AR2,           0x104,  27,     // esc
        SCANCODE_STR,           0x102,  1,
        SCANCODE_HOME,          0x101,  12,     // form feed

        SCANCODE_DOWN,          0x080,  2,
        SCANCODE_RIGHT,         0x040,  3,
        SCANCODE_UP,            0x020,  4,
        SCANCODE_LEFT,          0x010,  5,
        SCANCODE_BACKSPACE,     0x008,  8,      // backspace
        SCANCODE_RETURN,        0x004,  13,     // carriage return
        SCANCODE_PS,            0x002,  10,     // line feed
        SCANCODE_TAB,           0x001,  9       // tab

};

void detect_changes()
{
    for (int col = 0; col < rows.size(); ++col) {
        uint8_t changed_bits = rows[col] ^ last_rows[col];
        if (changed_bits == 0)
            continue;
        // there is a change
        for (uint8_t b = 0, shitf = 0x80; b < 8; ++b, shitf >>= 1) {
            if (changed_bits & shitf) {
                bool make = last_rows[col] & shitf; // was 1 now 0 --> make
                int map_index = ((8 - col) * 8 + b) * 3;
                int scancode = keymap_tab[map_index];
                int charcode = keymap_tab[map_index + 2];

                printf("%2d %3d '%c' %s\n", scancode, charcode, charcode > 31 ? charcode : 0, make ? "make" : "break");
            }
        }
    }

    last_rows = rows;
}

}