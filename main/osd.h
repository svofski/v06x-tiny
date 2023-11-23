#pragma once

#include <cstdint>
#include <functional>
#include <array>

#include "compat.h"
#include "Graphics/Font.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsBGR233.h"
#include "keyboard.h"


#include "fonts.h"

//template<typename G>
typedef GraphicsBGR233 G;

template<int ncolumns>
class ListView
{
public:
    std::function<void(G&,const Rect&,int,int)> on_draw_cell;
public:
    typedef std::array<int,ncolumns> column_widths_t;
    Rect bounds;
    int scroll_start;
    int rows_count;
    int row_height;
    bool invalid;
    column_widths_t column_widths;

    ListView() : scroll_start(0)
    {
        //static_assert(std::is_base_of(Graphics,G)::value);
    }
    ~ListView() {}

    // column width in pixels
    void set_column_widths(column_widths_t widths)
    {
        column_widths = widths;
    }

    void paint(G gfx)
    {
        invalid = false;

        Rect saved = gfx.clip_rect;
        gfx.clip_rect = bounds;

        int row = scroll_start / row_height;
        int screen_y = -(scroll_start % row_height);
        for (; screen_y < bounds.h;) {
            int col_x = bounds.x;
            for (int col = 0; col < ncolumns; ++col) {
                Rect r {col_x, bounds.y + screen_y, column_widths[col], row_height };
                on_draw_cell(gfx, r, row, col);
                col_x += column_widths[col];
            }
            ++row;
            screen_y += row_height;
        }

        gfx.clip_rect = saved;
    }

    void invalidate() {
        invalid = true;
    }

};

struct Colormap {
    static constexpr G::Color dir_bg = 0222;
    static constexpr G::Color dir_text = 0000;
};

class OSD
{
private:
    GraphicsBGR233 gfx;

public:
    typedef GraphicsBGR233::Color Color;

    int x, y;
    int width, height;

    typedef ListView<3> dirview_t;
    dirview_t filebox;

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

        int cw = gfx.font->charWidth;
        filebox.set_column_widths({cw * 22, cw * 6, cw * 2});
        filebox.row_height = gfx.font->charHeight;
        filebox.bounds = {
            .x = 4,
            .y = 16,
            .w= gfx.font->charWidth * 30,
            .h =gfx.font->charHeight * 25
        };
        filebox.on_draw_cell = std::bind(&OSD::draw_cell, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

        filebox.rows_count = 1000;
    }

    void draw_cell(G& gfx, const Rect& r, int row, int col)
    {
        gfx.fillRect(r.x, r.y, r.w, r.h, Colormap::dir_bg);
        gfx.setCursor(r.x, r.y);
        if (col < 2) {
            gfx.print(row);
            gfx.print(":");
            gfx.print(col);
        }
        else {
            char c = 'A' + row % 26;
            gfx.print(c);
        }
    }

    Color ** framebuffer() const { return gfx.frameBuffers[gfx.currentFrameBuffer]; }

    void test()
    {
        Color bg = gfx.RGB(100, 100, 20);
        Color fg = gfx.RGB(255, 100, 255);
        Color text = gfx.RGB(255, 255, 255);
        Color black = gfx.RGB(0, 0, 0);

        gfx.fillRect(0, 0, gfx.xres, gfx.yres, bg);
        gfx.fillCircle(0, 0, gfx.xres/2, fg);
        gfx.setTextColor(text, black);

        filebox.invalidate();
        
        gfx.show();
    }

    void frame()
    {
        keyboard::scan_matrix();
        gfx.setCursor(0, 29 * gfx.font->charHeight);
        for (int i = 0; i < 8; ++i) {
            //printf("%02x ", rows[i]);
            gfx.print(keyboard::rows[i], 16, 2);
            gfx.print(" ");
        }
        //keyboard::read_modkeys();
        gfx.print("M:");
        gfx.print(keyboard::state.pc, 16, 2);
        
        filebox.scroll_start++;
        filebox.invalidate();
        
        if (filebox.invalid) {
            filebox.paint(gfx);
        }

        gfx.show();

        keyboard::detect_changes();
    }
};

