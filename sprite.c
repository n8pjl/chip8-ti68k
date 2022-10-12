/* Copyright (C) 2022 Peter Lafreniere
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "chip8.h"
#include <compat.h>
#include <string.h>

/*
 * The main sprite drawing code. This function works perfectly, at a fast enough
 * speed for this project.
 *
 * The tigcclib sprite code does not report when pixels are reset, otherwise
 * that would be a better option.
 *
 * Safety: can be called from interrupt context. Directly modifies lcd memory.
 * Do not use when the screen is redirected.
 */
_Bool draw_sprite_16_hi(const uint16_t *sprite16, uint8_t x, uint8_t y,
			uint8_t n)
{
	const uint8_t *sprite = (uint8_t *)sprite16;
	uint16_t mask = UINT16_MAX;
	uint_fast8_t shft;
	uint32_t ret = 0;
	uint32_t line;
	uint32_t data;
	void *ptr;

	x %= 128;
	y %= 64;

	x += X_BASE;

	shft = 16 - (x % 16);

	if (x + 16 >= 128 + X_BASE) {
		mask = UINT32_MAX << (uint32_t)(x % 16);

		// Recurse to draw the overshoot. Can only happen once per call.
		uint16_t left_sprite[n];
		for (short i = 0; i < n; i++) {
			left_sprite[i] = (sprite[2 * i] << 8) |
					 sprite[2 * i + 1];
			left_sprite[i] = (left_sprite[i] & ~mask) << shft;
		}

		ret = draw_sprite_16_hi(left_sprite, 0, y, n);
	}

	for (short i = 0; i < n; i++) {
		ptr = LCD_MEM;
		ptr += ((y + i) % 64 + Y_BASE) * 30;
		ptr += x / 8 & ~1;
		line = *(uint32_t *)ptr;
		data = ((sprite[2 * i] << 8) | sprite[2 * i + 1]);
		data &= mask;
		data <<= shft;
		ret |= data & line;
		*(uint32_t *)ptr = line ^ data;
	}

	return ret;
}

/*
 * The same as draw_sprite_16_hi(), except it only loads sprites as 8 pixels per
 * line. See draw_sprite_16_hi(). 
 *
 * Safety: can be called from interrupt context. Directly modifies lcd memory.
 * Do not use when the screen is redirected.
 */
_Bool draw_sprite_8_hi(const uint8_t *sprite8, uint8_t x, uint8_t y, uint8_t n)
{
	uint16_t mask = UINT16_MAX;
	uint_fast8_t shft;
	uint32_t ret = 0;
	uint32_t line;
	uint32_t data;
	void *ptr;

	x %= 128;
	y %= 64;

	x += X_BASE;

	shft = 24 - (x % 16);

	if (x >= 128 + X_BASE) {
		mask = UINT32_MAX << (uint32_t)(x % 16);

		// Recurse to draw the overshoot. Can only happen once per call.
		uint8_t left_sprite[n];
		for (short i = 0; i < n; i++)
			left_sprite[i] = (sprite8[i] & ~mask) << shft;

		ret = draw_sprite_8_hi(left_sprite, 0, y, n);
	}

	for (short i = 0; i < n; i++) {
		ptr = LCD_MEM;
		ptr += ((y + i) % 64 + Y_BASE) * 30;
		ptr += x / 8 & ~1;
		line = *(uint32_t *)ptr;
		data = sprite8[i];
		data &= mask;
		data <<= shft;
		ret |= data & line;
		*(uint32_t *)ptr = line ^ data;
	}

	return ret;
}

/*
 * Draws a properly clipped chip-8 sprite, expanding low-res sprites to 
 * their high-res equivalents.
 * 
 * Safety: See draw_sprite_16()
 */
_Bool draw_sprite_8_lo(const uint8_t *sprite8, uint8_t x, uint8_t y, uint8_t n)
{
	uint16_t sprite16[n * 2];
	// Fastest option to zero a vla in c99
	for (short i = 0; i < n * 2; i++)
		sprite16[i] = 0;

	x *= 2;
	y *= 2;

	for (short i = 0; i < n; i++) {
		uint8_t row = sprite8[i];
		for (short j = 7; j >= 0; j--) {
			sprite16[i * 2] |= (row & 128) >> 7 << (j * 2);
			sprite16[i * 2] |= (row & 128) >> 7 << (j * 2 + 1);
			sprite16[i * 2 + 1] |= (row & 128) >> 7 << (j * 2);
			sprite16[i * 2 + 1] |= (row & 128) >> 7 << (j * 2 + 1);
			row <<= 1;
		}
	}

	return draw_sprite_16_hi(sprite16, x, y, n * 2);
}

/*
 * Basically it's a 16x16 sprite, but in lo-res, so actually 32x32
 * 
 * Safety: See draw_sprite_16()
 */
_Bool draw_sprite_16_lo(const uint16_t *sprite16, uint8_t x, uint8_t y,
			uint8_t n)
{
	uint8_t sprite8_left[n];
	uint8_t sprite8_right[n];

	for (short i = 0; i < n; i++) {
		sprite8_left[i] = ((uint8_t *)sprite16)[i * 2];
		sprite8_right[i] = ((uint8_t *)sprite16)[i * 2 + 1];
	}

	return draw_sprite_8_lo(sprite8_left, x, y, n) ||
	       draw_sprite_8_lo(sprite8_right, x + 8, y, n);
}

/*
 * There's no reason to save the entire screen when you can just save the 128x64
 * section that gets drawn to. This just wraps the actual copying.
 */
void save_chip8_screen(uint8_t dest[1024])
{
	const uint8_t row_bytes = 128 / 8;

	for (short i = 0; i < 64; i++)
		memcpy(dest + i * row_bytes,
		       LCD_MEM + (i + Y_BASE) * 30 + X_BASE / 8, row_bytes);
}

/*
 * Reverses the above save_chip8_screen()
 */
void restore_chip8_screen(const uint8_t src[1024])
{
	const uint8_t row_bytes = 128 / 8;

	for (short i = 0; i < 64; i++)
		memcpy(LCD_MEM + (i + Y_BASE) * 30 + X_BASE / 8,
		       src + i * row_bytes, row_bytes);
}

void ch8_scroll_right(void)
{
	uint8_t carry;
	uint16_t *ptr;
	uint16_t tmp;

	for (short i = Y_BASE; i < Y_BASE + 64; i++) {
		carry = 0;
		for (short j = X_BASE / 8; j < (X_BASE + 128) / 8; j += 2) {
			ptr = LCD_MEM + i * 30 + j;
			tmp = *ptr >> 4 | carry << 12;
			carry = *ptr & 0xF;
			*ptr = tmp;
		}
	}
}

void ch8_scroll_left(void)
{
	uint8_t carry;
	uint16_t *ptr;
	uint16_t tmp;

	for (short i = Y_BASE; i < Y_BASE + 64; i++) {
		carry = 0;
		for (short j = (X_BASE + 128) / 8; j > X_BASE / 8; j -= 2) {
			ptr = LCD_MEM + i * 30 + j;
			tmp = *ptr << 4 | carry;
			carry = (*ptr & 0xF000) >> 12;
			*ptr = tmp;
		}
	}
}

void ch8_scroll_down(uint16_t op)
{
	memmove(LCD_MEM + (Y_BASE + (op & 0xF)) * 30, LCD_MEM + Y_BASE * 30,
		30 * (63 - (op & 0xF)));
	memset(LCD_MEM + Y_BASE * 30, 0, 30 * (op & 0xF));
}
