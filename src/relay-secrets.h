/*
DualStream Relay for OBS
Copyright (C) 2026 Dual Stream Studio Inc <hello@dualstream.gg>

SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The C face of the secret store in relay-secrets.cpp, for the routing code
 * that has no Qt in it. Same protection, same rules: values are sealed with
 * the operating system's own facility, tied to the signed-in user account.
 *
 * Both return NULL when the platform has no store or the value cannot be
 * processed, which the caller must treat as "no value". Returned strings are
 * allocated with bstrdup(); callers bfree() them. */

char *dsr_secret_protect_cstr(const char *plain);
char *dsr_secret_unprotect_cstr(const char *sealed);

#ifdef __cplusplus
}
#endif
