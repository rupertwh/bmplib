/* bmplib - test-fileio-pipes.h
 *
 * Copyright (c) 2025, Rupert Weber.
 *
 * This file is part of bmplib.
 * bmplib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <https://www.gnu.org/licenses/>
 */

struct WritePipe {
	FILE *file_read;
};


FILE* provide_as_file(const unsigned char *data, size_t size);

FILE* open_write_pipe(struct WritePipe **hwp);
int data_from_write_pipe(struct WritePipe *wp, unsigned char *buffer, int size);
