/**
 * @file
 * @brief Implementation of Events.
 *
 * @Copyright (C) 2018  Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This file is part of events.
 *
 * events is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * events is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with events.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sys.h"
#include "debug.h"
#include "Events.h"

#ifdef CWDEBUG
NAMESPACE_DEBUG_CHANNELS_START
channel_ct events("EVENTS");
NAMESPACE_DEBUG_CHANNELS_END
#endif
