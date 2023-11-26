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

#include "sdcard.h"

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
    int selected;

    ListView() : scroll_start(0)
    {
        //static_assert(std::is_base_of(Graphics,G)::value);
        selected = 0;
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

    int first_visible_row(bool fully) const
    {
        int row = scroll_start / row_height;
        if (fully) {
            if (scroll_start % row_height != 0) {
                ++row;
            }
        }
        return row;
    }

    int last_visible_row(bool fully) const
    {
        int row = (scroll_start + bounds.h) / row_height - 1;
        if (!fully) {
            if ((scroll_start + bounds.h) % row_height != 0) {
                ++row;
            }
        }
        return row;
    }

    void ensure_visible(int index)
    {
        if (index >= first_visible_row(true) && index <= last_visible_row(true)) return;

        // want to be in the middle of a page
        int height_in_rows = bounds.h / row_height;
        scroll_start = std::clamp(index * row_height - (height_in_rows / 2) * row_height, 0, row_height * rows_count - bounds.h);
        printf("ensure_visible(%d) scroll_start=%d\n", index, scroll_start);
        invalidate();
    }

    void set_selected(int value)
    {
        if (value != selected) {
            selected = value;
            invalidate();
        }
    }

    void set_rows_count(int value)
    {
        if (value != rows_count) {
            rows_count = value;
            invalidate();
        }
    }
};

struct Colormap {
    static constexpr G::Color dir_bg = 0212;
    static constexpr G::Color dir_text = 0377;
    static constexpr G::Color dir_text_selected = 0;
    static constexpr G::Color dir_bg_selected = 0372;
};

// file select view: ROM/FDD/WAV/EDD/BAS tabs
// file list view
class OSD
{
private:
    GraphicsBGR233 gfx;

public:
    typedef GraphicsBGR233::Color Color;

    SDCard& sdcard;

    int x, y;
    int width, height;

    typedef ListView<3> dirview_t;
    dirview_t filebox;

    int key_direction;
    int key_hold;
    int key_acceleration;

    AssetKind asset_kind;

    OSD(SDCard& sdcard): sdcard(sdcard),x(0), y(0), width(0), height(0)
    {
        asset_kind = AK_ROM;

        gfx.setFrameBufferCount(1);
    }

    void init(int xres, int yres)
    {
        width = xres;
        height = yres;
        gfx.setResolution(xres, yres);
        gfx.setFont(CodePage437_8x8);
        gfx.autoScroll = false;

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

        asset_kind = AK_ROM;
        filebox.rows_count = sdcard.get_file_count(asset_kind);

        keyboard::onkeyevent = std::bind(&OSD::keyevent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

        key_direction = 0;
        key_acceleration = 0;
        key_hold = 0;
    }

    void draw_cell(G& gfx, const Rect& r, int row, int col)
    {
        gfx.setTextColor(Colormap::dir_text, Colormap::dir_bg);
        if (row == filebox.selected) {
            gfx.setTextColor(Colormap::dir_text_selected, Colormap::dir_bg_selected);
        }
        gfx.fillRect(r.x, r.y, r.w, r.h, gfx.backColor);

        const FileInfo * fi = sdcard.get_file_info(asset_kind, row);
        if (fi == nullptr) {
            return;
        }

        gfx.setCursor(r.x, r.y);
        switch (col) {
        case 0:
            gfx.print(fi->name);
            break;
        case 1:
            {
                int w = filebox.column_widths[col] / 8;
                const std::string sstr = fi->size_string();
                //printf("w=%d sstr=%s len=%d spaces=%d\n", w, sstr.c_str(), sstr.length(), w - sstr.length());
                for (int i = 0; i < w - sstr.length(); ++i)
                    gfx.print(' ');
                gfx.print(fi->size_string());
            }
            break;
        case 2:
            {
                if (fi->name.size() > 0) {
                    bool paint = false;
                    char initial = fi->initial();
                    if (row == filebox.first_visible_row(true))
                        paint = true;
                    if (!paint) {
                        const FileInfo * prev = sdcard.get_file_info(asset_kind, row - 1);
                        if (prev == nullptr || prev->initial() != initial) {
                            paint = true;
                        }
                    }
                    if (paint) {
                        gfx.setCursor(r.x + 4, r.y);
                        gfx.print(initial);
                    }
                }
            }
            break;
        }
    }

    void keyevent(int scancode, int charcode, bool keydown)
    {
        printf("keyevent: scan=%d char=%d down=%d\n", scancode, charcode, keydown);
        if (keydown) {
            switch (scancode) {
                case SCANCODE_DOWN:
                    key_direction = 1;
                    key_acceleration = 0;
                    break;
                case SCANCODE_UP:
                    key_direction = -1;
                    key_acceleration = 0;
                    break;
                case SCANCODE_LEFT:
                    asset_kind = AssetStorage::prev(asset_kind);
                    filebox.invalidate();
                    break;
                case SCANCODE_RIGHT:
                    asset_kind = AssetStorage::next(asset_kind);
                    filebox.invalidate();
                    break;
            }
        }
        else {
            switch (scancode) {
                case SCANCODE_DOWN:
                case SCANCODE_UP:
                    key_direction = 0;
                    key_hold = 0;
                    break;
            }
        }
    }

    Color ** framebuffer() const { return gfx.frameBuffers[gfx.currentFrameBuffer]; }

    void test()
    {
        Color text = 0377;
        Color black = 0;

        gfx.fillRect(0, 0, gfx.xres, gfx.yres, 0100);
        //gfx.fillCircle(0, 0, gfx.xres/2, fg);
        gfx.setTextColor(text, black);

        filebox.invalidate();
        
        gfx.show();
    }

    void showing()
    {
        filebox.rows_count = sdcard.get_file_count(asset_kind);
    }

    void frame(int frameno)
    {
        keyboard::scan_matrix();
        gfx.setCursor(0, 29 * gfx.font->charHeight);
        for (int i = 0; i < 8; ++i) {
            gfx.print(keyboard::rows[i], 16, 2);
            gfx.print(" ");
        }
        gfx.print("M:");
        gfx.print(keyboard::state.pc, 16, 2);

        //filebox.scroll_start++;
        //filebox.invalidate();

        int dummy;
        if (uxQueueMessagesWaiting(sdcard.osd_notify_queue) > 0) {
            //xQueueReset(sdcard.osd_notify_queue); 
            int dummy;
            while (xQueueReceive(sdcard.osd_notify_queue, &dummy, 0)) {
                //nrequested--;
            }
            filebox.invalidate();
        }

        if (filebox.invalid) {
            filebox.paint(gfx);
        }

        gfx.show();

        keyboard::detect_changes();
        if (key_direction) {
            key_hold += 1 + key_acceleration;
            if (key_hold > 0) {
                key_hold -= 10;
                filebox.set_selected(filebox.selected + key_direction);
                if (filebox.selected >= filebox.rows_count) {
                    filebox.set_selected(0);
                }
                else if (filebox.selected < 0) {
                    filebox.set_selected(filebox.rows_count - 1);
                }
                filebox.ensure_visible(filebox.selected);
            }

            key_acceleration += 1 + key_acceleration/3;

        }
    }
};

