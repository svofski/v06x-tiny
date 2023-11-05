#pragma once

#include <inttypes.h>
#include <string.h>
#include <functional>
#include <map>

class Keyboard
{
private:
    uint8_t matrix[8];
    std::map<int, uint32_t> keymap;

public:
    bool ss, us, rus;
    bool terminate;
    std::function<void(bool)> onreset;

    Keyboard() : ss(false), us(false), rus(false), terminate(false)
    {
        memset(matrix, 0, sizeof(matrix));
        init_map();
    }

    int read(int rowbit)
    {
        int result = 0;
        for (int i = 0; i < 8; ++i) {
            if ((rowbit & 1) != 0) {
                result |= this->matrix[i];
            }
            rowbit >>= 1;
        }
        return (~result) & 0377;
    }

private:
    void init_map()
    {
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
    }
};
