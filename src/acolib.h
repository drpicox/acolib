/*
	Acotes Runtime Library
	Copyright (C) 2007 - David Rodenas Pico <david.rodenas@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
    
    $Id: acolib.h 2073 2008-02-14 16:29:50Z drodenas $
*/
#ifndef ACOLIB_H_
#define ACOLIB_H_

#include <stdint.h>

typedef struct task* task_t;
int task_init(task_t* task, void (*start)(task_t), int team_count);
void task_start(task_t task);
void task_wait(task_t task);
void task_close();
int task_allopen();
void task_copyin(task_t task, int state, void * src, int size);
void task_copyout(task_t task, int state, void * dst, int size);
void* copyin_acquire(int state);
void* copyout_acquire(int state);
void task_oport(task_t task, int port, int esize, int gcount, int bsize);
void task_iport(task_t task, int port, int esize, int gcount, int bsize, void *ivalues, int isize);
void port_connect(task_t ot, int oport, task_t it, int iport);
void oport_acquire(int port, int ecount);
int oport_tryacquire(int port, int ecount);
void* oport_peek(int port, int enumber);
void oport_push(int port, int ecount);
void iport_acquire(int port, int ecount);
int iport_tryacquire(int port, int ecount);
void* iport_peek(int port, int enumber);
void iport_pop(int port, int ecount);
void ioport_bypass(int iport, int oport, int ecount);
void task_shared(task_t task, int state, void* ivalue, int size);
void shared_async(task_t utask, int ustate, task_t atask, int astate);
void shared_sync(task_t utask, int ustate, task_t atask, int astate);
void* shared_acquire(int state);
void shared_update(int state);
void shared_check(int state);
int task_leader();
int task_team_me();
void iport_replicate(task_t task, int port);


// Tracing facilities
void trace_app_begin(char* name);
void trace_app_end();
void trace_instance_begin();
void trace_instance_end();
void trace_iteration_begin();
void trace_iteration_end();
void trace_teamreplicate_begin();
void trace_teamreplicate_end();

#endif /*ACOLIB_H_*/
