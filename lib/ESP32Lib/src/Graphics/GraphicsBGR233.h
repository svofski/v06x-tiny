/* bitluni's Graphics driver for 8-bit BGR233 format 
   2023 svofski
  */
#pragma once
#include <cstdint>
#include "Graphics.h"

class GraphicsBGR233: public Graphics<uint8_t>
{
	public:
	typedef uint8_t Color;
	GraphicsBGR233()
	{
		frontColor = 0xff;
	}

    //                                    this is what octals are made for
    static constexpr unsigned int RMASK = 0007;
	static constexpr unsigned int GMASK = 0070;
	static constexpr unsigned int BMASK = 0300;

	virtual int R(Color c) const
	{
        return ((c & RMASK) * 255) / 7;
	}
	virtual int G(Color c) const
	{
		return (((c & GMASK) >> 3) * 255) / 7;
	}
	virtual int B(Color c) const
	{
		return (((c & BMASK) >> 6) * 255) / 3;
	}
	virtual int A(Color c) const
	{
		return 255;
	}

	virtual Color RGBA(int r, int g, int b, int a = 255) const
	{
        return (b & BMASK) | ((g >> 2) & GMASK) | ((r >> 5) & RMASK);
	}

	virtual void dotFast(int x, int y, Color color)
	{
		backBuffer[y][x] = color;
	}

	virtual void dot(int x, int y, Color color)
	{
		if ((unsigned int)x < xres && (unsigned int)y < yres)
			backBuffer[y][x] = color;
	}

	virtual void dotAdd(int x, int y, Color color)
	{
		if ((unsigned int)x < xres && (unsigned int)y < yres)
		{
			int c0 = backBuffer[y][x];
			int c1 = color;

			int r = (c0 & RMASK) + (c1 & RMASK);
			if (r > RMASK) r = RMASK;

			int g = (c0 & GMASK) + (c1 & GMASK);
			if (g > GMASK) g = GMASK;

			int b = (c0 & BMASK) + (c1 & BMASK);
			if (b > BMASK) g = BMASK;

			backBuffer[y][x] = r | g | b;
		}
	}
	
	virtual void dotMix(int x, int y, Color color)
	{
		// no alpha ergo replace
		dot(x, y, color);
	}
	
	virtual Color get(int x, int y)
	{
		if ((unsigned int)x < xres && (unsigned int)y < yres)
			return backBuffer[y][x];
		return 0;
	}

	virtual void clear(Color color = 0)
	{
		for (int y = 0; y < this->yres; y++)
			for (int x = 0; x < this->xres; x++)
				backBuffer[y][x] = color;
	}

	virtual Color** allocateFrameBuffer()
	{
		return Graphics<Color>::allocateFrameBuffer(xres, yres, (Color)0);
	}
};