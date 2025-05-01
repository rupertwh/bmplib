/* bmplib - logging.c
 *
 * Copyright (c) 2024, 2025, Rupert Weber.
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
#include <limits.h>
#include <stdbool.h>
#include <errno.h>

#include "config.h"
#include "logging.h"

#if defined(__GNUC__)
        #define MAY_BE_UNUSED __attribute__((unused))
#else
        #define MAY_BE_UNUSED
#endif


struct Log {
	int   size;
	char *buffer;
	bool  panic;
};


/* logerr(log, fmt, ...) and logsyserr(log, fmt, ...) are
 * printf-style logging functions.
 *
 * Use logsyserr() where perror() would be used, logerr()
 * otherwise.
 *
 * 'separator' and 'inter' can have any length
 * 'air' is just there so we don't need to realloc every
 * single time.
 */

static const char separator[]="\n"; /* separator between log entries */
static const char inter[]=": ";     /* between own message and sys err text */
static const int  air = 80;         /* how much more than required we allocate */

static bool s_allocate(LOG log, size_t add_chars);
static void s_log(LOG log, const char *file, int line, const char *function,
		  const char *etxt, const char *fmt, va_list args);
static void panic(LOG log);
#ifdef DEBUG
static int s_add_file_etc(LOG log, const char *file, int line, const char *function);
#endif



/*********************************************************
 *      logcreate / logfree / etc.
 *********************************************************/

LOG logcreate(void)
{
	LOG log;

	if (!(log = malloc(sizeof *log)))
		return NULL;
	memset(log, 0, sizeof *log);
	return log;
}

void logfree(LOG log)
{
	if (log) {
		if (log->size > 0 && log->buffer)
			free(log->buffer);
		free(log);
	}
}

void logreset(LOG log)
{
	if (log && log->buffer)
		*log->buffer = 0;
}



/*********************************************************
 *      logmsg()
 *********************************************************/

const char* logmsg(LOG log)
{
	if (log) {
		if (log->panic) {
			return "PANIC! bmplib encountered an error while "
		               "trying to set an error message";
		}
		else if (log->buffer) {
			return log->buffer;
		}
	}
	return "";
}



/*********************************************************
 *      logerr()
 *********************************************************/

#ifdef DEBUG
void logerr_(LOG log, const char *file, int line, const char *function, const char *fmt, ...)
#else
void logerr(LOG log, const char *fmt, ...)
#endif
{
	va_list args;

	va_start(args, fmt);

#ifdef DEBUG
	s_log(log, file, line, function, NULL, fmt, args);
#else
	s_log(log, NULL, 0, NULL, NULL, fmt, args);
#endif

	va_end(args);
}



/*********************************************************
 *      logsyserr()
 *********************************************************/

#ifdef DEBUG
void logsyserr_(LOG log, const char *file, int line, const char *function, const char *fmt, ...)
#else
void logsyserr(LOG log, const char *fmt, ...)
#endif
{
	va_list     args;
	const char *etxt;

	va_start(args, fmt);
	etxt = strerror(errno);

#ifdef DEBUG
	s_log(log, file, line, function, etxt, fmt, args);
#else
	s_log(log, NULL, 0, NULL, etxt, fmt, args);
#endif

	va_end(args);
}



/*********************************************************
 *      s_log()
 *********************************************************/

static void s_log(LOG log, const char *file MAY_BE_UNUSED, int line MAY_BE_UNUSED,
                  const char *function MAY_BE_UNUSED,
                  const char *etxt, const char *fmt, va_list args)
{
	va_list argsdup;
	int     len = 0,addl_len, required_len;

	if (log->panic)
		return;

#ifdef DEBUG
	if (!s_add_file_etc(log, file, line, function))
		return;
#endif

	if (log->buffer)
		len = strlen(log->buffer);

	va_copy(argsdup, args);
	addl_len = vsnprintf(NULL, 0, fmt, argsdup);
	va_end(argsdup);

	required_len = len + strlen(separator) +
	               addl_len + (etxt ? strlen(inter) + strlen (etxt) : 0) + 1;
	if (required_len > log->size) {
		if (!s_allocate(log, required_len - log->size)) {
			panic(log);
			return;
		}
	}

#ifndef DEBUG   /*  <-- if NOT defined */
	if (*log->buffer) {
		strcat(log->buffer, separator);
		len += strlen(separator);
	}
#endif

	if (log->size - len <= vsnprintf(log->buffer + len, log->size - len,
					 fmt, args)) {
		panic(log);
		return;
	}

	if (etxt) {
		strcat(log->buffer, inter);
		strcat(log->buffer, etxt);
	}
}



/*********************************************************
 *      s_allocate()
 *********************************************************/

static bool s_allocate(LOG log, size_t add_chars)
{
	char   *tmp;
	size_t  newsize;

	if (log->panic)
		return false;

	add_chars += air;

	newsize = (size_t) log->size + add_chars;
	if (newsize > INT_MAX) {
		panic(log);
		return false;
	}

	tmp = realloc(log->buffer, newsize);
	if (tmp) {
		log->buffer = tmp;
		if (log->size == 0)
			log->buffer[0] = 0;
		log->size = newsize;
	} else {
		panic(log);
		return false;
	}

	return true;
}



/*********************************************************
 *      panic()
 *********************************************************/

static void panic(LOG log)
{
	log->panic = true;
}



/*********************************************************
 *      s_add_file_etc()
 *********************************************************/

#ifdef DEBUG
static int s_add_file_etc(LOG log, const char *file, int line, const char *function)
{
	int len = 0, addl_len, required_len;

	if (log->buffer)
		len = strlen(log->buffer);

	addl_len = snprintf(NULL, 0, "[%s, line %d, %s()] ", file, line, function);

	required_len = len + strlen(separator) + addl_len + 1;
	if (required_len > log->size) {
		if (!s_allocate(log, required_len - log->size)) {
			panic(log);
			return 0;
		}
	}

	if (*log->buffer) {
		strcat(log->buffer, separator);
		addl_len += strlen(separator);
		len += strlen(separator);
	}

	if (log->size - len <= snprintf(log->buffer + len, log->size - len,
	                                "[%s, line %d, %s()] ", file, line, function)) {
		panic(log);
		return 0;
	}
	return addl_len;
}
#endif
