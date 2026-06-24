/*
* This file is part of the hoverboard-firmware-hack project.
*
* Copyright (C) 2018 Simon Hailes <btsimonh@googlemail.com>
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
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// this file defines the structure to store variables in the flash
// e.g. PID constants
// such that they can be configured through protocol.c
// and saved to flash

// a copy is kept in ram.
// a pointer to each variable alnog with methods for modification is kept in a structure in protocol

// to write to it, use protocol to write to magic, this will commit content to flash

// decimal to make it easier to type!
#pragma once
#include "protocol.h"
#include "board_table.h"   // gpio_pin_t, board_select_mode_t

/* ---------------------------------------------------------------------------
 * Board-override persistence (M4).
 *
 * One persisted pin override: which ACTIVE field (field_id, see board_override.c
 * for the canonical enum) is remapped to which gpio_pin_t. Stored so a
 * PENDING_REBOOT override (phase-PWM/ADC/hall/dead-time) can be re-applied on
 * the next boot by the resolver, and so LIVE GPIO overrides survive a reset.
 *
 * #pragma pack(2): gpio_pin_t is two uint8_t (already 2-byte sized), field_id
 * is one byte padded to two — keeps the whole record 2-byte aligned so the
 * append below does not disturb the offsets of any pre-existing FLASH_CONTENT
 * field (PID / CAN_BoardID / adc).
 * ------------------------------------------------------------------------- */
#define BOARD_OVERRIDE_MAX 16   /* persisted override slots */

#pragma pack(push, 2)
typedef struct tag_BOARD_OVERRIDE_ENTRY {
    uint8_t    field_id;   /* board_field_id_t (board_override.c); 0xFF = empty slot */
    uint8_t    flags;      /* bit0: pending-reboot (not yet live); reserved otherwise */
    gpio_pin_t pin;        /* {0xFF,0xFF} = unset */
} board_override_entry_t;

typedef struct tag_BOARD_OVERRIDE {
    uint16_t               count;                          /* number of valid entries [0..BOARD_OVERRIDE_MAX] */
    board_override_entry_t entry[BOARD_OVERRIDE_MAX];
} board_override_t;
#pragma pack(pop)

#pragma pack(push, 2) // all variables of type unsigned short (2 bytes)
typedef struct tag_FLASH_CONTENT{
    unsigned short magic;  // write this with CURRENT_MAGIC to commit to flash

    unsigned short PositionKpx100; // pid params for Position
    unsigned short PositionKix100;
    unsigned short PositionKdx100;
    unsigned short PositionPWMLimit; // e.g. 200

    unsigned short SpeedKpx100; // pid params for Speed
    unsigned short SpeedKix100;
    unsigned short SpeedKdx100;
    unsigned short SpeedPWMIncrementLimit; // e.g. 20

    unsigned short MaxCurrLim;

    unsigned short HoverboardEnable; // non zero to enable
    unsigned short calibration_0;
    unsigned short calibration_1;
    unsigned short HoverboardPWMLimit;
    unsigned short CAN_BoardID;      // CAN Board ID (0-15) - runtime configurable
    unsigned short CAN_BaseID_CMD;   // CAN Command Base ID (default 0x100)
    unsigned short CAN_BaseID_STATUS; // CAN Status Base ID (default 0x200)
    PROTOCOL_ADC_SETTINGS adc;

    /* --- board_override fields (M4) — APPENDED so the offsets of every field
     * above (PID / CAN_BoardID / adc) are byte-for-byte unchanged. ---------- */
    unsigned short   board_override_valid;   // non-zero => board_selected_index/board_override are meaningful
    unsigned short   board_selected_index;   // resolver's stored_index (g_board_table row); 0xFFFF = none
    unsigned short   board_select_mode;      // board_select_mode_t (cast)
    board_override_t board_override;         // persisted pin overrides (pending + live)
} FLASH_CONTENT;
#pragma pack(pop)

extern FLASH_CONTENT FlashContent;
extern const FLASH_CONTENT FlashDefaults;