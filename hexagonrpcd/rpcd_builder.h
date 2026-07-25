/*
 * FastRPC virtual filesystem builder - header file
 *
 * Copyright (C) 2023-2025 The HexagonRPC Contributors
 *
 * This file is part of HexagonRPC.
 *
 * HexagonRPC is free software: you can redistribute it and/or modify
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

#ifndef RPCD_BUILDER_H
#define RPCD_BUILDER_H

#include "hexagonfs.h"

struct hexagonfs_dirent *construct_root_dir(const char *prefix, const char *dsp);
struct hexagonfs_dirent *construct_root_dir_with_prefix(const char *prefix, const char *dsp);

#endif
