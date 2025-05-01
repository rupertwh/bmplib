/* bmplib - logging.h
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

#ifndef LOGGING_H
#define LOGGING_H

typedef struct Log *LOG;


#if defined(__GNUC__)
	#define PRINTF(a,b) __attribute__((format(printf, a, b)))
#else
	#define PRINTF(a,b)
#endif


#ifdef DEBUG
	#define logerr(log, ...) logerr_(log, __FILE__, __LINE__, __func__, __VA_ARGS__)
	void PRINTF(5,6) logerr_(LOG log, const char *file, int line,
			 const char *function, const char *fmt, ...);

	#define logsyserr(log, ...) logsyserr_(log, __FILE__, __LINE__, __func__, __VA_ARGS__)
	void PRINTF(5,6) logsyserr_(LOG log, const char *file, int line,
			    const char *function, const char *fmt, ...);
#else
	void PRINTF(2,3) logerr(LOG log, const char *fmt, ...);
	void PRINTF(2,3) logsyserr(LOG log, const char *fmt, ...);
#endif


const char* logmsg(LOG log);

LOG logcreate(void);
void logfree(LOG log);
void logreset(LOG log);

void logreport(LOG log);

#endif /* LOGGING_H */
