#pragma once

#include <cstdint>
#include "compat.h"
#include "Graphics/Font.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsBGR233.h"
#include "keyboard.h"


#include "fonts.h"

class OSD
{
private:
    GraphicsBGR233 gfx;

public:
    typedef GraphicsBGR233::Color Color;

    int x, y;
    int width, height;

    OSD(): x(0), y(0), width(0), height(0)
    {
        gfx.setFrameBufferCount(1);
    }

    void init(int xres, int yres)
    {
        width = xres;
        height = yres;
        gfx.setResolution(xres, yres);
        gfx.setFont(CodePage437_8x8);
    }

    Color ** framebuffer() const { return gfx.frameBuffers[gfx.currentFrameBuffer]; }

    void test()
    {
        Color bg = gfx.RGB(100, 100, 20);
        Color fg = gfx.RGB(255, 100, 255);
        Color text = gfx.RGB(255, 255, 255);
        Color black = gfx.RGB(0, 0, 0);

        printf("BGR233: R=%02x G=%02x B=%02x bg=%02x fg=%02x text=%02x\n", gfx.RGB(255, 0, 0), gfx.RGB(0, 255, 0), gfx.RGB(0, 0, 255),
            bg, fg, text);
        gfx.fillRect(0, 0, gfx.xres, gfx.yres, bg);
        gfx.fillCircle(0, 0, gfx.xres/2, fg);
        gfx.setTextColor(text, black);
        gfx.print("Hello.JPG");
        for (int i = 0; i < 10; ++i) {
            gfx.setCursor(0, (i + 2) * gfx.font->charHeight);
            gfx.print("bob mike");
        }
        gfx.show();
    }

    void frame()
    {
        uint8_t rows[8];
        int shifter = 1;
        for (int i = 0; i < 8; ++i, shifter <<= 1) {
            keyboard::select_columns(shifter ^ 0xff);
            keyboard::read_rows(); // purge slave's tx fifo
            keyboard::read_rows(); 
            rows[i] = keyboard::state.rows;
        }
        keyboard::read_modkeys();
        gfx.setCursor(0, 29 * gfx.font->charHeight);
        for (int i = 0; i < 8; ++i) {
            //printf("%02x ", rows[i]);
            gfx.print(rows[i], 16, 2);
            gfx.print(" ");
        }
        keyboard::read_modkeys();
        gfx.print("M:");
        gfx.print(keyboard::state.pc, 16, 2);
        gfx.show();
    }
};

