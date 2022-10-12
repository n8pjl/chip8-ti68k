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

#ifndef CHIP8_H
#define CHIP8_H

#if !defined(USE_TI92P) && !defined(USE_TI89) && !defined(USE_V200)
#define USE_TI89
#define USE_TI92P
#define USE_V200
#endif

#include <graph.h>
#include <stdint.h>

// The major version number is used for rom and save file format compatibility.
#define MAJOR_VERSION 0
// The minor version is used for feature changes that are backwards
// (but not forward) compatible.
#define MINOR_VERSION 9
// The patch version is used for bug fixes that do not change compatiblity.
#define PATCH_VERSION 0

enum ch8_error {
	E_OK,
	E_EXIT_SAVE,
	E_SILENT_EXIT,
	E_INVALID_ARGUMENT,
	E_ROM_LOAD,
	E_VERSION,
	E_STACK_OVERFLOW,
	E_STACK_UNDERFLOW,
	E_OOM,
	E_INVALID_OPCODE,
	E_INVALID_ADDRESS,
	E_UNKNOWN_ERR,
};

/*
 * The original CHIP-8 interpreter had a 12-entry stack, but all modern
 * implementations that I know of use at least a 16-entry stack.
 */
#define C8_STACK_CAPACITY 16

struct ch8_stack {
	uint16_t stack[C8_STACK_CAPACITY];
	uint8_t sp;
};

/*
 * Saved state of the game. To maintain save game compatibility, do not
 * modify this struct. While it is a bad idea to make saves ABI dependant,
 * there is only one ABI that the compiler can target. The version number is
 * used to detect incompatible saves.
 * 
 * TODO: Optimize this struct for space. Will need to reread the abi.
 */
struct ch8_state {
	struct {
		uint8_t major;
		uint8_t minor;
		uint8_t patch;
	} version;
	struct ch8_stack stack;
	long randstate; // A copy of the __randseed global variable used by tigcclib.
	uint16_t pc;
	uint16_t I;
	_Bool from_state;
	_Bool is_hires_on;
	uint8_t registers[16];
	volatile uint8_t delay_timer;
	volatile uint8_t sound_timer;
	uint8_t memory[4096];
	uint8_t display[1024];
	uint8_t rpl_fake[16];
};

struct ch8_rom {
	struct {
		uint8_t major;
		uint8_t minor;
		uint8_t patch;
	} header;
	uint8_t rom[];
} __attribute__((packed));

#define X_BASE ((LCD_WIDTH / 2 - 128 / 2) & 0xF0)
#define Y_BASE ((LCD_HEIGHT / 2 - 64 / 2) & 0xF0)

// opcodes.c
struct ch8_stack ch8_stack_new(void);
enum ch8_error ch8_run(struct ch8_state *state);

// sprite.c
_Bool draw_sprite_16_hi(const uint16_t *sprite16, uint8_t x, uint8_t y,
			uint8_t n);
_Bool draw_sprite_16_lo(const uint16_t *sprite16, uint8_t x, uint8_t y,
			uint8_t n);
_Bool draw_sprite_8_hi(const uint8_t *sprite8, uint8_t x, uint8_t y, uint8_t n);
_Bool draw_sprite_8_lo(const uint8_t *sprite8, uint8_t x, uint8_t y, uint8_t n);
void save_chip8_screen(uint8_t dest[1024]);
void restore_chip8_screen(const uint8_t src[1024]);
void ch8_scroll_right(void);
void ch8_scroll_left(void);
void ch8_scroll_down(uint16_t op);

#endif /* CHIP8_H */
