/* bmplib - logging.c
 *
 * Copyright (c) 2024, Rupert Weber.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>

#include "config.h"

#include "logging.h"


struct Log {
	size_t	size;
	char   *buffer;
};


static void panic(LOG log);

/*********************************************************
 *      logcreate()
 *********************************************************/

LOG logcreate(void)
{
	LOG	log;

	if (!(log = malloc(sizeof *log)))
		return NULL;

	memset(log, 0, sizeof *log);

	return log;
}


void logfree(LOG log)
{
	if (log) {
		if (log->size != (size_t)-1 && log->buffer)
			free(log->buffer);
		free(log);
	}
}

int s_allocate(LOG log, size_t add_chars)
{
	char   *tmp;
	size_t  newsize;

	if (log->size == (size_t)-1)
		return 0; /* log is set to a string literal (panic) */

	newsize = log->size + add_chars;

	tmp = realloc(log->buffer, newsize);
	if (tmp) {
		log->buffer = tmp;
		if (log->size == 0)
			log->buffer[0] = 0;
		log->size = newsize;
	}
	else
		return 0;

	return 1;
}



void logreset(LOG log)
{
	if (log && log->buffer)
		*log->buffer = 0;
}

const char* logmsg(LOG log)
{
	if (log && log->buffer)
		return log->buffer;
	else
		return "";
}


/*********************************************************
 *      logerr_()
 *********************************************************/

void logerr_(LOG log, const char *file, int line, const char *function, ...)
{
        va_list     args, backup;
        const char *fmt;
        int         written, len;

        va_start(args, function);

	if (log->size == (size_t)-1)
		goto done; /* log is set to a string literal (panic) */

        if (!log->size || log->size - strlen(log->buffer) < 5) {
        	if (!s_allocate(log, 100)) {
        		panic(log);
        		goto done;
        	}
        }

        len = strlen(log->buffer);
        if (len) {
        	strcat(log->buffer, "\n");
        	len++;
        }

#ifdef DEBUG
        do {
	       	written = snprintf(log->buffer + len, log->size - len,
	       		              "[%s, line %d, %s()] ", file, line, function);
		if (written < 0) {
			panic(log);
			goto done;
	       	}
	       	if (written >= log->size - len) {
	       		if (!s_allocate(log, written - (log->size - len) + 5)) {
	       			panic(log);
	       			goto done;
	       		}
	       	}
	       	else
	       		break;

	} while(1);

#endif

        len = strlen(log->buffer);

        va_copy(backup, args);
        fmt = va_arg(args, const char*);
        do {

		written = vsnprintf(log->buffer + len, log->size - len, fmt, args);
		if (written < 0) {
			panic(log);
			goto done;
		}
		if (written >= log->size - len) {
			if (!s_allocate(log, written - (log->size - len) + 5)) {
				panic(log);
				goto done;
			}
			va_copy(args, backup);
			fmt = va_arg(args, const char*);
		}
		else
			break;
	} while (1);

done:
        va_end(args);
}



/*********************************************************
 *      logsyserr_()
 *********************************************************/


void logsyserr_(LOG log, const char *file, int line, const char *function, int eno, ...)
{
        va_list     args, backup;
        const char *fmt;
        const char *etxt;
        int         written, len;

        va_start(args, eno);

	if (log->size == (size_t)-1)
		return; /* log is set to a string literal (panic) */

        etxt = strerror(eno);

        if (!log->size || log->size - strlen(log->buffer) < 5) {
        	if (!s_allocate(log, 100 + strlen(etxt))) {
        		panic(log);
        		return;
        	}
        }

        len = strlen(log->buffer);
        if (len) {
        	strcat(log->buffer, "\n");
        	len++;
        }

#ifdef DEBUG
        do {
	       	written = snprintf(log->buffer + len, log->size - len,
	       		              "[%s, line %d, %s()] ", file, line, function);
		if (written < 0) {
			panic(log);
			goto done;
	       	}
	       	if (written >= log->size - len) {
	       		if (!s_allocate(log, written - (log->size - len) + 5)) {
	       			panic(log);
	       			goto done;
	       		}
	       	}
	       	else
	       		break;

	} while(1);
#endif

	va_copy(backup, args);
        fmt = va_arg(args, const char*);

        len = strlen(log->buffer);

        do {
	       	written = vsnprintf(log->buffer + len, log->size - len, fmt, args);
		if (written < 0) {
			panic(log);
			goto done;
		}
		if (written >= log->size - len) {
			if (!s_allocate(log, written - (log->size - len) + 5)) {
				panic(log);
				goto done;
			}
			va_copy(args, backup);
			fmt = va_arg(args, const char*);
		}
		else
			break;
	} while (1);

        len = strlen(log->buffer);

        do {
	       	written = snprintf(log->buffer + len, log->size - len, ": %s", etxt);
		if (written < 0) {
			panic(log);
			goto done;
		}
		if (written >= log->size - len) {
			if (!s_allocate(log, written - (log->size - len) + 5)) {
				panic(log);
				goto done;
			}
		}
		else
			break;
	} while (1);

done:
        va_end(args);

}


static void panic(LOG log)
{
	log->size = (size_t) -1;
	log->buffer = "PANIC! bmplib encountered an error while trying to set "
	              "an error message";
}
