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
#include <alloc.h>
#include <args.h>
#include <dialogs.h>
#include <intr.h>
#include <statline.h>
#include <stdlib.h>
#include <string.h>
#include <vat.h>

/*
 * S-CHIP sprites start at index 80 (0x3C)
 */
static const uint8_t CHIP8_SPRITES[240] = {
	0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10,
	0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0, 0x90, 0x90, 0xF0, 0x10,
	0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0,
	0x10, 0x20, 0x40, 0x40, 0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0,
	0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80,
	0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80, 0xFF, 0xFF, 0xC3, 0xC3,
	0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0x18, 0x78, 0x78, 0x18, 0x18, 0x18,
	0x18, 0x18, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0xC0, 0xC0,
	0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF,
	0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF,
	0xC0, 0xC0, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0xC0,
	0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x03, 0x06, 0x0C,
	0x18, 0x18, 0x18, 0x18, 0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3,
	0xFF, 0xFF, 0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF,
	0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3, 0xC3, 0xFC, 0xFC,
	0xC3, 0xC3, 0xFC, 0xFC, 0xC3, 0xC3, 0xFC, 0xFC, 0x3C, 0xFF, 0xC3, 0xC0,
	0xC0, 0xC0, 0xC0, 0xC3, 0xFF, 0x3C, 0xFC, 0xFE, 0xC3, 0xC3, 0xC3, 0xC3,
	0xC3, 0xC3, 0xFE, 0xFC, 0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0xC0, 0xC0,
	0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0
};

static const char C8SV_TAG[] = { 0, 'c', '8', 's', 'v', 0, OTH_TAG };
static const char CH8_TAG[] = { 0, 'c', 'h', '8', 0, OTH_TAG };

/*
 * This pointer is used to share the state pointer between the main execution
 * loop and interrupt handlers. It is unsafe to use to access anything except
 * the timer registers.
 */
static volatile struct ch8_state *global_state;

/*
 * This interrupt handler is called at just under 60hz. It is used to update the
 * timers at a constant rate and to display sound timer output. Options in
 * interrupt context are limited; avoid adding to this handler if you can.
 */
DEFINE_INT_HANDLER(timer_update_interrupt)
{
	static volatile _Bool is_sound_on = FALSE;

	if (global_state->delay_timer > 0)
		global_state->delay_timer--;
	if (global_state->sound_timer > 0)
		global_state->sound_timer--;

	if (global_state->sound_timer && !is_sound_on) {
		uint8_t saved[1024];

		save_chip8_screen(saved);
		memset(LCD_MEM, -1, LCD_SIZE);
		restore_chip8_screen(saved);

		is_sound_on = TRUE;
	} else if (!global_state->sound_timer && is_sound_on) {
		uint8_t saved[1024];

		save_chip8_screen(saved);
		memset(LCD_MEM, 0, LCD_SIZE);
		restore_chip8_screen(saved);

		is_sound_on = FALSE;
	}
}

/*
 * Get error message from error type enum. Note that identical return values are
 * constant folded.
 */
static const char *get_error_message(enum ch8_error err)
{
	switch (err) {
	case E_OK:
		return "Done";
	case E_EXIT_SAVE:
		return "Done";
	case E_SILENT_EXIT:
		return "";
	case E_INVALID_ARGUMENT:
		return "Error: invalid program parameter";
	case E_ROM_LOAD:
		return "Error: failed loading ROM";
	case E_VERSION:
		return "Error: invalid format";
	case E_STACK_OVERFLOW:
		return "Error: stack overflow";
	case E_STACK_UNDERFLOW:
		return "Error: stack underflow";
	case E_OOM:
		return "Error: out of memory";
	case E_INVALID_OPCODE:
		return "Error: invalid instruction";
	case E_INVALID_ADDRESS:
		return "Error: address out of range";
	case E_UNKNOWN_ERR:
	default:
		return "Error: unknown error";
	}
}

/*
 * Do not forget to update this message when the release version changes.
 *
 * Safety: can trigger heap compression.
 */
static void display_about(void)
{
	DlgMessage(
		"About",
		"chip8-ti68k v1.0\n"
		"A (S)CHIP-8 emulator for ti68k graphing calculators.\n"
		"\n"
		"Copyright 2022 Peter Lafreniere\n"
		"This is free software. See COPYING for more details.",
		BT_NONE, BT_OK);
}

/* 
 * Provides a very simple LZSS decompressor for roms and savestates.
 *
 * Warning: will do funky things when the structure isn't as expected.
 * Only use for trusted inputs.
 */
static uint16_t decompress(uint8_t *restrict dest, const uint8_t *restrict src,
			   uint16_t srclen)
{
	uint16_t count = 0;
	uint16_t offset;
	uint16_t i = 0;

	while (srclen > i) {
		if (src[i] != 0xFF) {
			dest[count++] = src[i++];
		} else if (src[i + 1] & 63) {
			for (uint8_t j = 0; j < (src[i + 1] & 63); j++) {
				offset = (src[i + 1] & 0xC0) << 2 | src[i + 2];
				dest[count + j] = dest[count + j - offset - 1];
			}
			count += src[i + 1] & 63;
			i += 3;
		} else {
			dest[count++] = 0xFF;
			i += 2;
		}
	}
	return count;
}

/*
 * Initializes the saved screen, interrupts and the PRG, calling the main loop,
 * then restores previous state.
 * 
 * PRNG settings were found by trial and error with a handheld stopwatch.
 * (1, 241) = 62.5Hz, (0, 0) = 62.5Hz, and (1, 240) = 58.8Hz.
 * 
 * Safety: can trigger heap compression; messes with interrupts and the
 * programable rate generator. See ch8_run() for more.
 */
static enum ch8_error ch8_start(struct ch8_state *state)
{
	unsigned char old_prg_start;
	enum ch8_error result;
	INT_HANDLER old_int_1;
	INT_HANDLER old_int_5;
	short old_prg_rate;
	HANDLE old_fb;

	old_fb = HeapAlloc(LCD_SIZE);
	if (old_fb == H_NULL)
		return E_OOM;

	LCD_save(HeapDeref(old_fb));

	ClrScr();
	if (state->from_state)
		restore_chip8_screen(state->display);

	if (!IsPRGEnabled())
		EnablePRG();

	old_prg_rate = PRG_getRate();
	old_prg_start = PRG_getStart();

	// ~60Hz
	PRG_setRate(1);
	PRG_setStart(240);

	old_int_1 = GetIntVec(AUTO_INT_1);
	SetIntVec(AUTO_INT_1, DUMMY_HANDLER);
	old_int_5 = GetIntVec(AUTO_INT_5);
	SetIntVec(AUTO_INT_5, timer_update_interrupt);

	result = ch8_run(state);

	SetIntVec(AUTO_INT_1, old_int_1);
	SetIntVec(AUTO_INT_5, old_int_5);

	PRG_setRate(old_prg_rate);
	PRG_setStart(old_prg_start);

	if (result == E_EXIT_SAVE)
		save_chip8_screen(state->display);

	LCD_restore(HeapDeref(old_fb));

	HeapFree(old_fb);
	return result;
}

/*
 * Returns a new state from the given rom. randstate and display are left
 * uninitialized.
 */
static enum ch8_error load_rom(const MULTI_EXPR *rom, struct ch8_state *state)
{
	const struct ch8_rom *pack;

	*state = (struct ch8_state){
		.version = { MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION },
		.stack = ch8_stack_new(),
		.registers = { 0 },
		.pc = 0x200,
		.I = 0,
		.delay_timer = 0,
		.sound_timer = 0,
		.from_state = FALSE,
		.is_hires_on = FALSE,
		.memory = { 0 },
		.rpl_fake = { 0 },
	};
	memcpy(state->memory, CHIP8_SPRITES, sizeof(CHIP8_SPRITES));
	randomize();

	if (rom->Size - sizeof(pack->version) > 0x1000 - 0x200)
		return E_ROM_LOAD;

	pack = (struct ch8_rom *)rom->Expr;

	if (pack->version.major != MAJOR_VERSION ||
	    pack->version.minor > MINOR_VERSION)
		return E_VERSION;

	if (!decompress(state->memory + 0x200, pack->rom,
			rom->Size - sizeof(pack->version)))
		return E_ROM_LOAD;

	return E_OK;
}

/*
 * Loads a snapshot of chip8 state from a pointer to a buffer,
 * validates said buffer, and returns an error if the buffer is invalid.
 */
static enum ch8_error load_state(const MULTI_EXPR *input,
				 struct ch8_state *state)
{
	const struct ch8_state *rodata;

	// The structs need to be the same for states.
	if (input->Size - sizeof(C8SV_TAG) != sizeof(*rodata))
		return E_VERSION;

	rodata = (struct ch8_state *)input->Expr;
	if (rodata->version.major != MAJOR_VERSION ||
	    rodata->version.minor > MINOR_VERSION)
		return E_VERSION;

	srand(rodata->randstate);

	*state = *rodata;
	state->from_state = TRUE;
	return E_OK;
}

/*
 * Validates and dispatches a file to the appropriate loader.
 */
static enum ch8_error load_dispatch(struct ch8_state *state, HSym handle)
{
	MULTI_EXPR *data;

	data = HeapDeref(DerefSym(handle)->handle);

	if (!memcmp(data->Expr + data->Size - sizeof(C8SV_TAG), C8SV_TAG,
		    sizeof(C8SV_TAG)))
		return load_state(data, state);
	else if (!memcmp(data->Expr + data->Size - sizeof(CH8_TAG), CH8_TAG,
			 sizeof(CH8_TAG)))
		return load_rom(data, state);
	else
		return E_ROM_LOAD;
}

/*
 * Attempts to load a file from user supplied arguments. Fails if more than one
 * argument is passed, or if the argument is not a valid file path.
 *
 * Safety: can trigger heap compression.
 */
static enum ch8_error load_path(struct ch8_state *state)
{
	ESI arg = top_estack;
	const char *str;
	HSym handle;

	if (GetArgType(arg) != STR_TAG)
		return E_INVALID_ARGUMENT;

	str = GetStrnArg(arg);

	if (!SymCmp(str, "about")) {
		display_about();
		return E_SILENT_EXIT;
	}

	handle = SymFind(SYMSTR(str));

	if (handle.folder == 0)
		return E_INVALID_ARGUMENT;

	return load_dispatch(state, handle);
}

/*
 * Allows the user to select a rom or savestate to play.
 *
 * Safety: can trigger heap compression.
 */
static enum ch8_error load_usermenu(struct ch8_state *state)
{
	HSym handle;

	// Zero terminated list of possible file types.
	const ESQ ftype_opts[] = { OTH_TAG, OTH_TAG, 0x00 };
	const char *extensions[] = { "ch8", "c8sv" };

	handle = VarOpen(ftype_opts, extensions);

	if (handle.folder == 0)
		return E_SILENT_EXIT;

	return load_dispatch(state, handle);
}

/*
 * Handles user dialogue and saving snapshots of emulator state.
 * 
 * Safety: can trigger heap compression.
 */
static enum ch8_error save_state(const struct ch8_state *state)
{
	struct ch8_state *saved_state;
	SYM_ENTRY *symbol;
	MULTI_EXPR *file;
	HANDLE handle;
	HSym hsym;

	const ESQ ftype_opts[] = { OTH_TAG, 0x00 };
	const char *extensions[] = { "c8sv" };

	hsym = VarNew(ftype_opts, extensions);

	if (hsym.folder == 0)
		return E_SILENT_EXIT;

	if (!(handle = HeapAlloc(2 + sizeof(struct ch8_state) +
				 sizeof(C8SV_TAG))))
		return E_OOM;

	symbol = DerefSym(hsym);
	symbol->handle = handle;

	file = HeapDeref(handle);
	file->Size = sizeof(struct ch8_state) + sizeof(C8SV_TAG);
	saved_state = (struct ch8_state *)file->Expr;

	*saved_state = *state;

	saved_state->randstate = __randseed;

	// It's ugly but I need to manually place the type bytes at the end.
	memcpy((void *)saved_state + sizeof(struct ch8_state), C8SV_TAG,
	       sizeof(C8SV_TAG));

	// TODO: Compress save states. Previous attempts were too slow to use.

	return E_OK;
}

/*
 * The main function serves as an error handler and the location of the main
 * state struct. Also the entry point for the program.
 */
void _main(void)
{
	// static variables persist changes across program runs.
	// This does not work for archived programs.
	static _Bool has_been_run = FALSE;

	struct ch8_state *state;
	enum ch8_error result;

	if (!has_been_run) {
		has_been_run = TRUE;
		display_about();
	}

	if (!(state = HLock(HeapAlloc(sizeof(struct ch8_state))))) {
		ST_helpMsg(get_error_message(E_OOM));
		return;
	}

	switch (ArgCount()) {
	case 0:
		result = load_usermenu(state);
		break;
	case 1:
		result = load_path(state);
		break;
	default:
		result = E_INVALID_ARGUMENT;
	}

	if (result != E_OK) {
		if (result != E_SILENT_EXIT)
			ST_helpMsg(get_error_message(result));
		goto exit;
	}

	global_state = state;

	result = ch8_start(state);
	if (result != E_SILENT_EXIT)
		ST_helpMsg(get_error_message(result));

	if (result == E_EXIT_SAVE)
		save_state(state);

exit:
	HeapFree(HeapPtrToHandle(state));
	return;
}
