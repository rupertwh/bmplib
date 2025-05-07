/* bmplib - test-fileio-pipes.c
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

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <test-fileio-pipes.h>


FILE* provide_as_file(const unsigned char *data, size_t size)
{
	int   fd[2] = { -1, -1 };
	FILE *file_read = NULL, *file_write = NULL;


	if (pipe(fd)) {
		perror("pipe");
		goto abort;
	}

	if (!(file_read = fdopen(fd[0], "rb"))) {
		perror("fdopen read pipe");
		goto abort;
	}

	if (!(file_write = fdopen(fd[1], "w"))) {
		perror("fdopen write pipe");
		goto abort;
	}

	if (size != fwrite(data, 1, size, file_write)) {
		perror("fwrite to pipe");
		goto abort;
	}

	fclose(file_write);
	return file_read;

abort:
	if (file_write)
		fclose(file_write);
	else if (fd[1] != -1)
		close(fd[1]);
	if (file_read)
		fclose(file_read);
	else if (fd[0] != -1)
		close(fd[0]);

	return NULL;
}



FILE* open_write_pipe(struct WritePipe **hwp)
{
	int   fd[2] = { -1, -1 };
	FILE *file_read = NULL, *file_write = NULL;
	struct WritePipe *wp = NULL;

	if (!hwp)
		return NULL;
	*hwp = NULL;

	if (!(wp = malloc(sizeof *wp))) {
		perror(__func__);
		goto abort;
	}
	memset(wp, 0, sizeof *wp);

	if (pipe(fd)) {
		perror("pipe");
		goto abort;
	}

	if (!(file_read = fdopen(fd[0], "rb"))) {
		perror("fdopen read pipe");
		goto abort;
	}

	if (!(file_write = fdopen(fd[1], "w"))) {
		perror("fdopen write pipe");
		goto abort;
	}

	wp->file_read = file_read;
	*hwp = wp;

	return file_write;

abort:
	if (file_write)
		fclose(file_write);
	else if (fd[1] != -1)
		close(fd[1]);

	if (file_read)
		fclose(file_read);
	else if (fd[0] != -1)
		close(fd[0]);

	if (wp)
		free(wp);

	return NULL;
}

int data_from_write_pipe(struct WritePipe *wp, unsigned char *buffer, int size)
{
	int nread;

	if (!(wp && buffer)) {
		fprintf(stderr, "%s(): invalid NULL argument(s)\n", __func__);
		exit(3);
	}
	if (!wp->file_read) {
		fprintf(stderr, "%s(): FILE* is NULL\n", __func__);
		exit(3);
	}
	nread = fread(buffer, 1, size, wp->file_read);
	fclose(wp->file_read);
	free(wp);

	return nread;
}
