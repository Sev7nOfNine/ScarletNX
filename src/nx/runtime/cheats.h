/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Atmosphere-format cheats from the data folder's cheats/<build id>.txt (cheats.c) */
#ifndef SNX_CHEATS_H
#define SNX_CHEATS_H
/* runs every cheat once; the first call loads them. Called for each frame the display shows */
void cheats_frame(void);
#endif
