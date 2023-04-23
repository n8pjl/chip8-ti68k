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
#include <graph.h>
#include <gray.h>
#include <kbd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
//
// Stack operations, keyboard functions, and other helper routines
//
////////////////////////////////////////////////////////////////////////////////

// Creates a new, empty stack.
struct ch8_stack ch8_stack_new(void)
{
	return (struct ch8_stack){ .sp = 0 };
}

/*
 * Pushes a new value onto the stack. Returns E_STACK_OVERFLOW if the stack
 * is full, otherwise E_OK.
 */
static enum ch8_error ch8_stack_push(struct ch8_stack *stack, uint16_t x)
{
	if (stack->sp == C8_STACK_CAPACITY)
		return E_STACK_OVERFLOW;
	else
		stack->stack[stack->sp++] = x;
	return E_OK;
}

/*
 * Pops a value off the stack. Returns E_STACK_UNDERFLOW if the stack is
 * empty, otherwise E_OK. The popped value is returned in x.
*/
static enum ch8_error ch8_stack_pop(struct ch8_stack *stack, uint16_t *x)
{
	if (stack->sp == 0)
		return E_STACK_UNDERFLOW;
	else
		*x = stack->stack[--stack->sp];
	return E_OK;
}

/*
 * read_keyboard() scans out the entire keyboard, mapped to chip-8 key codes.
 * This primitive can be used to build more complex keyboard functions.
 *
 * The CHIP-8 keyboard maps to the calculator keyboards like so:
 *
 *  |1|2|3|C|
 *  |4|5|6|D|
 *  |7|8|9|E|
 *  |A|0|B|F|
 *      ||
 *      ||
 *      \/
 *  |7|8|9|x|
 *  |4|5|6|-|
 *  |1|2|3|+|
 *  |0|.|-|e|
 *
 * In addition, esc can be used to exit the program and F1 can be used to open
 * the savestate dialog. Also note that the up, down, left, and right arrow
 * keys are bound to the 5, 8, 7, and 9 CHIP8 keys, respectively. 
 * 2nd (and HAND) can similarly be used for the CHIP8 6 key.
 */
static void read_keyboard(char out[18])
{
	if (TI89 == TRUE) {
		BEGIN_KEYTEST
		out[0xC] = _keytest_optimized(RR_MULTIPLY);
		out[0xD] = _keytest_optimized(RR_MINUS);
		out[0xE] = _keytest_optimized(RR_PLUS);
		out[0xF] = _keytest_optimized(RR_ENTER);
		END_KEYTEST
		BEGIN_KEYTEST
		out[3] = _keytest_optimized(RR_9);
		out[6] = _keytest_optimized(RR_6);
		out[9] = _keytest_optimized(RR_3);
		out[0xB] = _keytest_optimized(RR_NEGATE);
		END_KEYTEST
		BEGIN_KEYTEST
		out[2] = _keytest_optimized(RR_8);
		out[5] = _keytest_optimized(RR_5);
		out[8] = _keytest_optimized(RR_2);
		out[0] = _keytest_optimized(RR_DOT);
		END_KEYTEST
		BEGIN_KEYTEST
		out[1] = _keytest_optimized(RR_7);
		out[4] = _keytest_optimized(RR_4);
		out[7] = _keytest_optimized(RR_1);
		out[0xA] = _keytest_optimized(RR_0);
		END_KEYTEST
		BEGIN_KEYTEST
		out[5] |= _keytest_optimized(RR_UP);
		out[6] |= _keytest_optimized(RR_2ND);
		out[7] |= _keytest_optimized(RR_LEFT);
		out[8] |= _keytest_optimized(RR_DOWN);
		out[9] |= _keytest_optimized(RR_RIGHT);
		END_KEYTEST
		out[0x10] = _keytest(RR_ESC);
		out[0x11] = _keytest(RR_F1);
	} else {
		// TI-92P or V200
		BEGIN_KEYTEST
		out[0x1] = _keytest_optimized(RR_7);
		out[0x2] = _keytest_optimized(RR_8);
		out[0x3] = _keytest_optimized(RR_9);
		END_KEYTEST
		BEGIN_KEYTEST
		out[0x4] = _keytest_optimized(RR_4);
		out[0x5] = _keytest_optimized(RR_5);
		out[0x6] = _keytest_optimized(RR_6);
		END_KEYTEST
		BEGIN_KEYTEST
		out[0x7] = _keytest_optimized(RR_1);
		out[0x8] = _keytest_optimized(RR_2);
		out[0x9] = _keytest_optimized(RR_3);
		END_KEYTEST
		BEGIN_KEYTEST
		out[0x0] = _keytest_optimized(RR_DOT);
		out[0xA] = _keytest_optimized(RR_0);
		out[0xB] = _keytest_optimized(RR_NEGATE);
		out[0xD] = _keytest_optimized(RR_MINUS);
		out[0xF] = _keytest_optimized(RR_ENTER1);
		END_KEYTEST
		BEGIN_KEYTEST
		out[5] |= _keytest_optimized(RR_UP);
		out[6] |= _keytest_optimized(RR_HAND);
		out[6] |= _keytest_optimized(RR_2ND);
		out[7] |= _keytest_optimized(RR_LEFT);
		out[8] |= _keytest_optimized(RR_DOWN);
		out[9] |= _keytest_optimized(RR_RIGHT);
		END_KEYTEST
		out[0xC] = _keytest(RR_MULTIPLY);
		out[0xE] = _keytest(RR_PLUS);
		out[0x10] = _keytest(RR_ESC);
		out[0x11] = _keytest(RR_F1);
	}
}

// These don't need explaining.

static inline uint8_t first(uint16_t x)
{
	return (x & 0xF000) >> 12;
}

static inline uint8_t second(uint16_t x)
{
	return (x & 0x0F00) >> 8;
}

static inline uint8_t third(uint16_t x)
{
	return (x & 0x00F0) >> 4;
}

static inline uint8_t last(uint16_t x)
{
	return x & 0xF;
}

//////////////////////////////////////////////////////////////////////////////
//
// CHIP-8 opcode implementations
//
//////////////////////////////////////////////////////////////////////////////

#define OPCODE_HANDLER(x) \
	static enum ch8_error x(struct ch8_state *state, uint16_t op)

// 00E0 - Clear screen
static enum ch8_error ch8_clear(enum ch8_plane planes)
{
	if (planes & C8_PLANE_LIGHT)
		memset(GrayGetPlane(LIGHT_PLANE), 0, LCD_SIZE);
	if (planes & C8_PLANE_DARK)
		memset(GrayGetPlane(DARK_PLANE), 0, LCD_SIZE);

	return E_OK;
}

// 00EE - Return from subroutine
static enum ch8_error ch8_ret(struct ch8_state *state)
{
	enum ch8_error ret;
	ret = ch8_stack_pop(&state->stack, &state->pc);
	return ret;
}

// 00FD - Exit Interpreter
static enum ch8_error ch8_quit(void)
{
	return E_SILENT_EXIT;
}

// 00FE - Disable hi-res mode
static enum ch8_error ch8_exit_hires(struct ch8_state *state)
{
	state->is_hires_on = FALSE;
	return E_OK;
}

// 00FF - Enable hi-res mode
static enum ch8_error ch8_enter_hires(struct ch8_state *state)
{
	state->is_hires_on = TRUE;
	return E_OK;
}

// 1nnn - Jump to location nnn
OPCODE_HANDLER(ch8_jump)
{
	state->pc = op & 0xFFF;
	return E_OK;
}

// 2nnn - Call subroutine at nnn
OPCODE_HANDLER(ch8_call)
{
	enum ch8_error err = ch8_stack_push(&state->stack, state->pc);
	state->pc = op & 0xFFF;
	return err;
}

// 3xnn - Skip the next instruction if Vx = nn
OPCODE_HANDLER(ch8_skip_eq)
{
	if (state->registers[second(op)] == (op & 0xFF))
		state->pc += 2;
	return E_OK;
}

// 4xnn - Skip the next instruction if Vx != nn
OPCODE_HANDLER(ch8_skip_neq)
{
	if (state->registers[second(op)] != (op & 0xFF))
		state->pc += 2;
	return E_OK;
}

// 5xy0 - Skip the next instruction if Vx = Vy
OPCODE_HANDLER(ch8_skip_reg_eq)
{
	if (last(op) != 0)
		return E_INVALID_OPCODE;

	if (state->registers[second(op)] == state->registers[third(op)])
		state->pc += 2;
	return E_OK;
}

// 5xy2 - Store Vx to Vy at I to I+(y-x). Do not update I (xo-chip)
OPCODE_HANDLER(ch8_store_xo)
{
	for (short i = second(op); i <= third(op); i++)
		state->memory[(state->I + i) & 0xFFF] = state->registers[i];
	return E_OK;
}

// 5xy3 - Load Vx to Vy from I to I+(y-x). Do not update I (xo-chip)
OPCODE_HANDLER(ch8_load_xo)
{
	for (short i = second(op); i <= third(op); i++)
		state->registers[i] = state->memory[(state->I + i) & 0xFFF];
	return E_OK;
}

// 6xnn - Set Vx = nn
OPCODE_HANDLER(ch8_set_imm)
{
	state->registers[second(op)] = op & 0xFF;
	return E_OK;
}

// 7xnn - Set Vx = Vx + nn
OPCODE_HANDLER(ch8_add_imm)
{
	state->registers[second(op)] += op & 0xFF;
	return E_OK;
}

// 8xy0 - Set Vx = Vy
OPCODE_HANDLER(ch8_mov)
{
	state->registers[second(op)] = state->registers[third(op)];
	return E_OK;
}

// 8xy1 - Set Vx |= Vy
OPCODE_HANDLER(ch8_or)
{
	state->registers[second(op)] |= state->registers[third(op)];
	return E_OK;
}

// 8xy2 - Set Vx &= Vy
OPCODE_HANDLER(ch8_and)
{
	state->registers[second(op)] &= state->registers[third(op)];
	return E_OK;
}

// 8xy3 - Set Vx ^= Vy
OPCODE_HANDLER(ch8_xor)
{
	state->registers[second(op)] ^= state->registers[third(op)];
	return E_OK;
}

// 8xy4 - Set Vx += Vy, VF to !carry
OPCODE_HANDLER(ch8_add)
{
	uint16_t x = state->registers[second(op)];
	uint16_t y = state->registers[third(op)];

	state->registers[second(op)] += y;
	state->registers[0xF] = (x + y) & ~UINT8_MAX ? 1 : 0;

	return E_OK;
}

// 8xy5 - Set Vx -= Vy, VF to !borrow
OPCODE_HANDLER(ch8_sub_5)
{
	uint16_t x = state->registers[second(op)];
	uint16_t y = state->registers[third(op)];

	state->registers[second(op)] -= y;
	state->registers[0xF] = y > x ? 0 : 1;

	return E_OK;
}

// 8xy6 - Set Vx = Vy >> 1, VF to carry
OPCODE_HANDLER(ch8_lsr)
{
	uint8_t y = state->registers[third(op)];

	state->registers[second(op)] = y >> 1;
	state->registers[0xF] = y & 1;

	return E_OK;
}

// 8xy7 - Set Vx = Vy - Vx, VF to !borrow
OPCODE_HANDLER(ch8_sub_7)
{
	uint16_t x = state->registers[second(op)];
	uint16_t y = state->registers[third(op)];

	state->registers[second(op)] = y - x;
	state->registers[0xF] = x > y ? 0 : 1;

	return E_OK;
}

// 8xyE - Set Vx = Vy << 1, VF to carry
OPCODE_HANDLER(ch8_lsl)
{
	uint8_t y = state->registers[third(op)];

	state->registers[second(op)] = y << 1;
	state->registers[0xF] = (y & 0x80) >> 7;

	return E_OK;
}

// 9xy0 - Skip the next instruction if Vx != Vy
OPCODE_HANDLER(ch8_skip_reg_neq)
{
	if (last(op) != 0)
		return E_INVALID_OPCODE;

	if (state->registers[second(op)] != state->registers[third(op)])
		state->pc += 2;
	return E_OK;
}

// annn - Set I = nnn
OPCODE_HANDLER(ch8_load_ptr)
{
	state->I = op & 0xFFF;
	return E_OK;
}

// bnnn - Jump to nnn + V0
OPCODE_HANDLER(ch8_jump_reg)
{
	state->pc = ((op & 0xFFF) + state->registers[0]) & 0xFFF;
	return E_OK;
}

// cxnn - Set Vx = random number AND nn
OPCODE_HANDLER(ch8_rand)
{
	state->registers[second(op)] = rand() & op & 0xFF;
	return E_OK;
}

// dxyn - Draw sprite
OPCODE_HANDLER(ch8_draw)
{
	_Bool result;
	uint8_t x = state->registers[second(op)];
	uint8_t y = state->registers[third(op)];

	if (state->is_hires_on) {
		if (!last(op))
			result = draw_sprite_16_hi(
				state->planes, (void *)state->memory + state->I,
				x, y, 16);
		else
			result = draw_sprite_8_hi(state->planes,
						  state->memory + state->I, x,
						  y, last(op));
	} else {
		if (!last(op))
			result = draw_sprite_16_lo(
				state->planes, (void *)state->memory + state->I,
				x, y, 16);
		else
			result = draw_sprite_8_lo(state->planes,
						  state->memory + state->I, x,
						  y, last(op));
	}

	state->registers[0xF] = result;
	return E_OK;
}

/*
 * ex9e - Skip the next instruction if key Vx is currently pressed
 * Currently does not skip if Vx > 16
 * Will need to test against roms to make sure.
 */
OPCODE_HANDLER(ch8_key_set)
{
	char board[18];
	uint8_t key = state->registers[second(op)];

	if (key >= 16)
		goto exit;

	read_keyboard(board);

	if (board[key])
		state->pc += 2;
exit:
	return E_OK;
}

/*
 * exa1 - Skip the next instruction if key Vx is not currently pressed
 * See Ex9E (ch8_key_set()) for more.
 */
OPCODE_HANDLER(ch8_key_unset)
{
	char board[18];
	uint8_t key = state->registers[second(op)];

	read_keyboard(board);

	if ((key < 16 && !board[key]) || key >= 16)
		state->pc += 2;
	return E_OK;
}

// fx07 - Set Vx = delay timer
OPCODE_HANDLER(ch8_read_timer)
{
	state->registers[second(op)] = state->delay_timer;
	return E_OK;
}

// fx0a - Set Vx = next pressed key (blocking)
OPCODE_HANDLER(ch8_key_wait)
{
	char old_row[18];
	char new_row[18];

	read_keyboard(old_row);

	while (1) {
		read_keyboard(new_row);

		// TODO handle other keys.
		if (new_row[16])
			return E_SILENT_EXIT;
		if (new_row[17])
			return E_EXIT_SAVE;

		for (uint8_t i = 0; i < 16; i++) {
			// Only evaluates to true on falling edge.
			if (old_row[i] && !new_row[i]) {
				state->registers[second(op)] = i;
				return E_OK;
			}
		}

		memcpy(old_row, new_row, sizeof(new_row));
	}
}

// fx15 - Set delay timer = Vx
OPCODE_HANDLER(ch8_set_timer)
{
	state->delay_timer = state->registers[second(op)];
	return E_OK;
}

// fx18 - Set sound timer = Vx
OPCODE_HANDLER(ch8_set_sound)
{
	state->sound_timer = state->registers[second(op)];
	return E_OK;
}

// fx1e - Set I += Vx
OPCODE_HANDLER(ch8_add_ptr)
{
	state->I = (state->I + state->registers[second(op)]);

	state->registers[0xF] = state->I & ~0xFFF ? 1 : 0;
	state->I &= 0xFFF;

	return E_OK;
}

// fx29 - Set I = address of hex digit stored in Vx
OPCODE_HANDLER(ch8_font)
{
	if (state->registers[second(op)] > 0xF)
		return E_INVALID_OPCODE; // Maybe a different error code?
	state->I = state->registers[second(op)] * 5;
	return E_OK;
}

// fx30 - Set I = address of hex digit stored in Vx (S-CHIP/Octo)
OPCODE_HANDLER(ch8_font_big)
{
	// Note that hex digits A-F are an Octo-specific extension
	if (state->registers[second(op)] > 0xF)
		return E_INVALID_OPCODE; // See ch8_font()
	state->I = state->registers[second(op)] * 10 + 80;
	return E_OK;
}

// fx33 - Set (I,I+1,I+2) = (100s, 10s, 1s) digits. (BCD routine)
OPCODE_HANDLER(ch8_bcd)
{
	uint8_t num = state->registers[second(op)];

	for (short j = 2; j >= 0; j--) {
		state->memory[(state->I + j) & 0xFFF] = num % 10;
		num /= 10;
	}
	return E_OK;
}

// fx55 - Store V0 to Vx at I to I+x. Set I += x + 1
OPCODE_HANDLER(ch8_store)
{
	for (short j = 0; j <= second(op); j++)
		state->memory[(state->I + j) & 0xFFF] = state->registers[j];

	state->I = (state->I + second(op) + 1) & 0xFFF;
	return E_OK;
}

// fx65 - Load V0 to Vx from I to I+x. Set I += x + 1
OPCODE_HANDLER(ch8_load)
{
	for (short j = 0; j <= second(op); j++)
		state->registers[j] = state->memory[(state->I + j) & 0xFFF];

	state->I = (state->I + second(op) + 1) & 0xFFF;
	return E_OK;
}

// fx75 - Store V0 to Vx in rpl persistent storage
// Note: rpl storage is currently faked in this version.
// rpl storage is just a second set of registers in the state.
OPCODE_HANDLER(ch8_rpl_store)
{
	for (short i = 0; i <= second(op); i++)
		state->rpl_fake[i] = state->registers[i];

	return E_OK;
}

// fx85 - Load V0 to Vx from rpl persistent storage
// Note: see ch8_rpl_store()
OPCODE_HANDLER(ch8_rpl_load)
{
	for (short i = 0; i <= second(op); i++)
		state->registers[i] = state->rpl_fake[i];

	return E_OK;
}

#undef OPCODE_HANDLER

//////////////////////////////////////////////////////////////////////////////
//
// CHIP-8 level 2 dispatch
//
//////////////////////////////////////////////////////////////////////////////

static enum ch8_error ch8_dispatch_0(struct ch8_state *state, uint16_t op)
{
	if (second(op) != 0)
		return E_INVALID_OPCODE;

	switch (third(op)) {
	case 0xC:
		ch8_scroll_down(state->planes, op);
		return E_OK;
	case 0xE:

		switch (last(op)) {
		case 0x0:
			return ch8_clear(state->planes);
		case 0xE:
			return ch8_ret(state);
		}
		break;
	case 0xF:

		switch (last(op)) {
		case 0xB:
			ch8_scroll_right(state->planes);
			return E_OK;
		case 0xC:
			ch8_scroll_left(state->planes);
			return E_OK;
		case 0xD:
			return ch8_quit();
		case 0xE:
			return ch8_exit_hires(state);
		case 0xF:
			return ch8_enter_hires(state);
		}
		break;
	}
	return E_INVALID_OPCODE;
}

static enum ch8_error ch8_dispatch_5(struct ch8_state *state, uint16_t op)
{
	switch (last(op)) {
	case 0x0:
		return ch8_skip_reg_eq(state, op);
	case 0x2:
		return ch8_store_xo(state, op);
	case 0x3:
		return ch8_load_xo(state, op);
	default:
		return E_INVALID_OPCODE;
	}
}

static enum ch8_error ch8_dispatch_8(struct ch8_state *state, uint16_t op)
{
	switch (last(op)) {
	case 0x0:
		return ch8_mov(state, op);
	case 0x1:
		return ch8_or(state, op);
	case 0x2:
		return ch8_and(state, op);
	case 0x3:
		return ch8_xor(state, op);
	case 0x4:
		return ch8_add(state, op);
	case 0x5:
		return ch8_sub_5(state, op);
	case 0x6:
		return ch8_lsr(state, op);
	case 0x7:
		return ch8_sub_7(state, op);
	case 0xE:
		return ch8_lsl(state, op);
	default:
		return E_INVALID_OPCODE;
	}
}

static enum ch8_error ch8_dispatch_e(struct ch8_state *state, uint16_t op)
{
	if ((op & 0xFF) == 0x9E)
		return ch8_key_set(state, op);
	else if ((op & 0xFF) == 0xA1)
		return ch8_key_unset(state, op);
	else
		return E_INVALID_OPCODE;
}

static enum ch8_error ch8_dispatch_f(struct ch8_state *state, uint16_t op)
{
	switch (third(op)) {
	case 0x0:
		switch (last(op)) {
		case 0x7:
			return ch8_read_timer(state, op);
		case 0xA:
			return ch8_key_wait(state, op);
		default:
			return E_INVALID_OPCODE;
		}
	case 0x1:
		switch (last(op)) {
		case 0x5:
			return ch8_set_timer(state, op);
		case 0x8:
			return ch8_set_sound(state, op);
		case 0xE:
			return ch8_add_ptr(state, op);
		default:
			return E_INVALID_OPCODE;
		}
	case 0x2:
		if (last(op) == 0x9)
			return ch8_font(state, op);
		else
			return E_INVALID_OPCODE;
	case 0x3:
		switch (last(op)) {
		case 0x0:
			return ch8_font_big(state, op);
		case 0x3:
			return ch8_bcd(state, op);
		default:
			return E_INVALID_OPCODE;
		}
	case 0x5:
		if (last(op) == 0x5)
			return ch8_store(state, op);
		else
			return E_INVALID_OPCODE;
	case 0x6:
		if (last(op) == 0x5)
			return ch8_load(state, op);
		else
			return E_INVALID_OPCODE;
	case 0x7:
		if (last(op) == 0x5)
			return ch8_rpl_store(state, op);
		else
			return E_INVALID_OPCODE;
	case 0x8:
		if (last(op) == 0x5)
			return ch8_rpl_load(state, op);
		else
			return E_INVALID_OPCODE;
	default:
		return E_INVALID_OPCODE;
	}
}

//////////////////////////////////////////////////////////////////////////////
//
//  Main execution loop and instruction dispatch
//
//////////////////////////////////////////////////////////////////////////////

/*
 * Performs dispatching of opcodes to their corresponding handlers. Function
 * pointers are *not* used because they block inlining by the compiler.
 */
static enum ch8_error ch8_dispatch(struct ch8_state *state, uint16_t opcode)
{
	switch (first(opcode)) {
	case 0x0:
		return ch8_dispatch_0(state, opcode);
	case 0x1:
		return ch8_jump(state, opcode);
	case 0x2:
		return ch8_call(state, opcode);
	case 0x3:
		return ch8_skip_eq(state, opcode);
	case 0x4:
		return ch8_skip_neq(state, opcode);
	case 0x5:
		return ch8_dispatch_5(state, opcode);
	case 0x6:
		return ch8_set_imm(state, opcode);
	case 0x7:
		return ch8_add_imm(state, opcode);
	case 0x8:
		return ch8_dispatch_8(state, opcode);
	case 0x9:
		return ch8_skip_reg_neq(state, opcode);
	case 0xA:
		return ch8_load_ptr(state, opcode);
	case 0xB:
		return ch8_jump_reg(state, opcode);
	case 0xC:
		return ch8_rand(state, opcode);
	case 0xD:
		return ch8_draw(state, opcode);
	case 0xE:
		return ch8_dispatch_e(state, opcode);
	case 0xF:
		return ch8_dispatch_f(state, opcode);
	default:
		return E_INVALID_OPCODE;
	}
}

/*
 * Executes the next instruction from memory, incrementing the program counter
 * *before* handling the instruction.
 */
static enum ch8_error ch8_step(struct ch8_state *state)
{
	uint16_t opcode;

	if (state->pc > 0x0FFE)
		return E_INVALID_ADDRESS;

	// Loading one byte at a time fixes crashes due to misalignment.
	opcode = ((*(state->memory + state->pc)) << 8) |
		 *(state->memory + state->pc + 1);

	state->pc += 2;

	return ch8_dispatch(state, opcode);
}

/*
 * Executes the CHIP-8 program from the given state until an error occurs or a
 * "boss key" is pressed. In the future, this function will also handle creating
 * a pause menu for better user control.
 */
enum ch8_error ch8_run(struct ch8_state *state)
{
	enum ch8_error err = E_OK;

	while (err == E_OK) {
		err = ch8_step(state);

		if (_keytest(RR_ESC))
			return E_SILENT_EXIT;

		if (_keytest(RR_F1))
			return E_EXIT_SAVE;

		// TODO: Make a pause menu.
	}

	return err;
}
