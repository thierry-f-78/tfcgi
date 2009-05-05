/* 
 * tfcgi.h: FastCGI API for starting threads
 *
 * Copyright 2008-2009 Thierry Fournier (http://cv.arpalert.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef __TFCGI_H__
#define __TFCGI_H__

#define NO_FCGI_DEFINES
#include <fcgi_stdio.h>

extern void (*tfcgi_init_threads)(int thread_id);
extern void (*tfcgi_exec)(FCGX_Request fcgi, int thread_id);
extern char *tfcgi_socket;
extern int tfcgi_backlog;
extern mode_t tfcgi_socket_mode;
extern int tfcgi_threads_nb;

static inline void tfcgi_set_init_threads(void (*arg)(int thread_id)) {
	tfcgi_init_threads = arg;
}

static inline void tfcgi_set_exec(void (*arg)(FCGX_Request fcgi, int thread_id)) {
	tfcgi_exec = arg;
}

static inline void tfcgi_set_socket(char *arg) {
	tfcgi_socket = arg;
}

static inline void tfcgi_set_backlog(int arg) {
	tfcgi_backlog = arg;
}

static inline void tfcgi_set_socket_mode(mode_t arg) {
	tfcgi_socket_mode = arg;
}

static inline void tfcgi_set_threads_nb(int arg) {
	tfcgi_threads_nb = arg;
}

void tfcgi_start(void);

#endif /* __TFCGI_H__ */
