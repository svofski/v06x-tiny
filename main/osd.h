#pragma once

#include <cstdint>
#include <functional>
#include <array>
#include <algorithm>

#include "compat.h"
#include "Graphics/Font.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsBGR233.h"
#include "keyboard.h"

#include "fonts.h"

#include "sdcard.h"

typedef GraphicsBGR233 G;

struct Colormap {
    static constexpr G::Color logo_text = 0000;
    static constexpr G::Color logo_bg = 00057;

    static constexpr G::Color osd_text = 0377;// cold grey
    static constexpr G::Color osd_bg = 0122;// cold grey
    static constexpr G::Color dir_bg = 0111;//0212;
    static constexpr G::Color dir_text = 0377;
    static constexpr G::Color dir_text_selected = 0;
    static constexpr G::Color dir_bg_selected = 0350;

    static constexpr G::Color index_bg = 0027;
    static constexpr G::Color index_fg = 0377;

    static constexpr G::Color asset_fg = 0377;
    static constexpr G::Color asset_bg = osd_bg;
    static constexpr G::Color asset_fg_selected = 0067;
    static constexpr G::Color asset_bg_selected = dir_bg;
};

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
        int height_in_rows = bounds.h / row_height;

        if (rows_count < height_in_rows) {
            scroll_start = 0;
            return;
        }

        if (index >= first_visible_row(true) && index <= last_visible_row(true)) return;

        // want to be in the middle of a page
        scroll_start = std::clamp(index * row_height - (height_in_rows / 2) * row_height, 0, row_height * rows_count - bounds.h);
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

// file select view: ROM/FDD/WAV/EDD/BAS tabs
// file list view
class OSD
{
private:
    GraphicsBGR233 gfx;

public:
    typedef GraphicsBGR233::Color Color;

    G::Color bgcolor;
    SDCard& sdcard;

    int x, y;
    int width, height;
    bool visible;

    std::function<void(void)> onshown;
    std::function<void(void)> onhidden;
    std::function<void(AssetKind,int)> onload;

    typedef ListView<3> dirview_t;
    dirview_t filebox;

    typedef ListView<AK_LAST+1> assview_t;
    assview_t assbox;

    int key_direction;
    int key_hold;
    int key_acceleration;

    AssetKind asset_kind;
    std::array<int, AK_LAST+1> asset_selected;  // current selection for each asset kind

    OSD(SDCard& sdcard): sdcard(sdcard),x(0), y(0), width(0), height(0)
    {
        asset_kind = AK_ROM;
        bgcolor = Colormap::osd_bg;
        visible = false;

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
        filebox.set_column_widths({cw * 24, cw * 6 + 2, cw * 2});

        // calculate filebox width
        int width = 0;
        for (int i = 0; i < filebox.column_widths.size(); ++i) 
            width += filebox.column_widths[i];

        int anchor_x = 4;
        int anchor_y = gfx.font->charHeight + 8;
        
        assbox.row_height = gfx.font->charHeight + 2;
        assbox.bounds = {
            .x = anchor_x,
            .y = anchor_y,
            .w = width,
            .h = assbox.row_height
        };

        assbox.on_draw_cell = std::bind(&OSD::draw_asset_cell, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        assbox.set_column_widths({cw * 4, cw * 4, cw * 4, cw * 4, cw * 4});

        filebox.row_height = gfx.font->charHeight + 2;
        filebox.bounds = {
            .x = anchor_x,
            .y = assbox.bounds.y + assbox.bounds.h,
            .w = width,
            .h = filebox.row_height * 20,
        };
        filebox.on_draw_cell = std::bind(&OSD::draw_file_cell, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

        asset_kind = AK_ROM;
        filebox.rows_count = sdcard.get_file_count(asset_kind);
        asset_selected = {};

        keyboard::onkeyevent = std::bind(&OSD::keyevent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

        key_direction = 0;
        key_acceleration = 0;
        key_hold = 0;
    }

    void draw_file_cell(G& gfx, const Rect& r, int row, int col)
    {
        const int char_width = gfx.font->charWidth;
        const int char_height = gfx.font->charHeight;

        gfx.setTextColor(Colormap::dir_text, Colormap::dir_bg);
        if (row == filebox.selected) {
            gfx.setTextColor(Colormap::dir_text_selected, Colormap::dir_bg_selected);
        }

        int vgap = (r.h - char_height) / 2;
        gfx.setCursor(r.x, r.y + vgap);

        const FileInfo * fi = sdcard.get_file_info(asset_kind, row);
        if (fi == nullptr) {
            gfx.fillRect(r.x, r.y, r.w, r.h, col < 2 ? Colormap::dir_bg : bgcolor);

            if (row == filebox.first_visible_row(true) && col == 0) {
                gfx.setTextColor(Colormap::dir_text, Colormap::dir_bg);
                gfx.print("No files");
            }
            return;
        }

        switch (col) {
        case 0:
            {
                int lpadding = 2;
                int textw = std::clamp((int)fi->name.length() * char_width, 0, r.w);
                gfx.setCursor(r.x + lpadding, r.y + vgap);
                gfx.print(fi->name);

                // padding
                gfx.fillRect(r.x, r.y, r.w, vgap, gfx.backColor);
                gfx.fillRect(r.x, r.y + vgap + char_height, r.w, vgap, gfx.backColor);
                gfx.fillRect(r.x, r.y, lpadding, r.h, gfx.backColor);
                if (textw + lpadding < r.w) {
                    gfx.fillRect(r.x + textw + lpadding, r.y, r.w - textw - lpadding, r.h, gfx.backColor);
                }
            }
            break;
        case 1:
            {
                int width_px = filebox.column_widths[col];
                int w = width_px / 8;
                const std::string sstr = fi->size_string();
                for (int i = 0; i < w - sstr.length(); ++i)
                    gfx.print(' ');
                gfx.print(fi->size_string());

                // vert gap
                gfx.fillRect(r.x, r.y, r.w, vgap, gfx.backColor);
                gfx.fillRect(r.x, r.y + vgap + char_height, r.w, vgap, gfx.backColor);
                gfx.fillRect(gfx.cursorX, r.y, r.w - (gfx.cursorX - r.x), r.h, gfx.backColor);
            }
            break;
        case 2:
            {
                bool paint = false;
                if (fi->name.size() > 0) {
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
                        // left bg
                        gfx.fillRect(r.x, r.y, r.w - 4, r.h, Colormap::index_bg);
                        // character
                        gfx.setTextColor(Colormap::index_fg, Colormap::index_bg);
                        gfx.setCursor(r.x + 4, r.y + vgap);
                        gfx.print(initial);
                        // right wedge
                        int x = r.x + r.w - 4;
                        gfx.fillRect(x, r.y + r.h - 4, 4, 4, bgcolor);
                        for (int i = 0, h = r.h; i < 4; ++i, ++x, --h) {
                            gfx.fillRect(x, r.y, 1, h, Colormap::index_bg);
                        }
                    }
                }

                if (!paint) {
                    gfx.fillRect(r.x, r.y, r.w, r.h, bgcolor);
                }
            }
            break;
        }
    }

    void draw_asset_cell(G& gfx, const Rect& r, int row, int col)
    {
        gfx.setTextColor(Colormap::asset_fg, Colormap::asset_bg);
        if ((int)asset_kind == col) {
            // selected
            gfx.setTextColor(Colormap::asset_fg_selected, Colormap::asset_bg_selected);
        }

        int vpadding = (r.h - gfx.font->charHeight) / 2;
        int text_w = 3 * gfx.font->charWidth;
        int hpadding = (r.w - text_w) / 2;

        gfx.setCursor(r.x + hpadding, r.y + vpadding);

        gfx.print(AssetStorage::asset_cstr((AssetKind)col));
        gfx.fillRect(r.x, r.y, r.w, vpadding, gfx.backColor);
        gfx.fillRect(r.x, r.y + gfx.font->charHeight + vpadding, r.w, vpadding, gfx.backColor);
        gfx.fillRect(r.x, r.y, hpadding, r.h, gfx.backColor);
        gfx.fillRect(r.x + hpadding + text_w, r.y, hpadding, r.h, gfx.backColor);
    }

    void set_asset_kind(AssetKind value, bool always = false)
    {
        if (always || value != asset_kind) {
            asset_selected[asset_kind] = filebox.selected;
            asset_kind = value;
            filebox.set_rows_count(sdcard.get_file_count(asset_kind));
            filebox.set_selected(std::clamp(asset_selected[asset_kind], 0, filebox.rows_count - 1));
            filebox.ensure_visible(filebox.selected);
            filebox.invalidate();
            assbox.invalidate();
        }
    }

    void keyevent(int scancode, int charcode, bool keydown)
    {
        //printf("keyevent: scan=%d char=%d down=%d\n", scancode, charcode, keydown);
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
                    set_asset_kind(AssetStorage::prev(asset_kind));
                    break;
                case SCANCODE_TAB:
                case SCANCODE_RIGHT:
                    set_asset_kind(AssetStorage::next(asset_kind));
                    break;
                case SCANCODE_AR2:
                    hide();
                    break;
                case SCANCODE_RETURN:
                    if (onload) {
                        onload(asset_kind, filebox.selected);
                    }
                    hide();
                    break;

                //case SCANCODE_TAB:
                //    bgcolor = (unsigned)((bgcolor - 1)) & 255;
                //    test();
                //    break;
                //case SCANCODE_PS:
                //    bgcolor = (unsigned)((bgcolor + 1)) & 255;
                //    test();
                //    break;
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

    void paint_background()
    {
        for (int i = 0; i < gfx.frameBufferCount; ++i) {
            gfx.fillRect(0, 0, gfx.xres, gfx.yres, bgcolor);
            gfx.fillRect(0, 2, gfx.xres, gfx.font->charHeight + 4, Colormap::logo_bg); // hide the ugly fact that two bottom lines are mixed up with the top
            gfx.setTextColor(Colormap::logo_text, Colormap::logo_bg);
            gfx.setCursor(4, 4);
            gfx.print("v06x-tiny-esp32 2024 svofski " VERSION_STRING);
            filebox.invalidate();
            assbox.invalidate();
            gfx.show();
        }
    }

    void show()
    {
        set_asset_kind(asset_kind, true);
        visible = true;
        paint_background();
        if (onshown) onshown();
    }

    void hide()
    {
        visible = false;
        asset_selected[asset_kind] = filebox.selected;
        if (onhidden) onhidden();
    }

    void frame(int frameno)
    {
        keyboard::scan_matrix();
        gfx.setCursor(0, 29 * gfx.font->charHeight - 2);
        gfx.setTextColor(Colormap::osd_text, Colormap::osd_bg);
        for (int i = 0; i < 8; ++i) {
            gfx.print(keyboard::rows[i], 16, 2);
            gfx.print(" ");
        }
        gfx.print("M:");
        gfx.print(keyboard::state.pc, 16, 2);

        #if 0
        gfx.setCursor(0, 28 * gfx.font->charHeight);
        gfx.print("bgcolor: ");
        gfx.print((unsigned char)bgcolor, 8, 3);
        #endif

        if (filebox.invalid) {
            filebox.paint(gfx);
        }
        if (assbox.invalid) {
            assbox.paint(gfx);
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

