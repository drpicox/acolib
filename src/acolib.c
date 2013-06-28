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
    
    $Id: acolib.c 2100 2008-02-20 18:09:24Z drodenas $
*/
#include "acolib.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <mintaka.h>

/* ********************************************************************
 * * Type definition
 * ********************************************************************/

// typedef struct task* task_t;
typedef struct task_instance* instance_t;
typedef struct state* state_t;
typedef struct ostream* ostream_t;
typedef struct istream* istream_t;
typedef struct port* port_t;
typedef struct costream* costream_t;
typedef struct cistream* cistream_t;

struct task {
    void (*start)(task_t);
    int team_size;
    int close_count;
    instance_t instances;
    int number;
};

struct task_instance {
    int thread_id;
    int number;
    int allopen;
    int state_count;
    int state_capacity;
    state_t states;
    int port_count;
    int port_capacity;
    port_t ports;
    pthread_t pthread;
    task_t task;
    int is_replicate;
    port_t replicate_port;
};

struct state {
    int number;
    char* buffer;
    int size;
    int is_shared_state;
    char* shared_buffer;
    int is_copy_in_state;
    int is_copy_out_state;
    char* copy_out_buffer;
};

#define DSTREAM(T,X) printf(T " %s(%p) first:%d next:%d size:%d capacity:%d\n", \
    #X, (X), (X)->first, (X)->next, (X)->size, (X)->capacity)

struct ostream {
    volatile int first;
    int next;
    int size;
    int capacity;
    istream_t istream;
};

struct istream {
    volatile int is_open;
    volatile int next;
    char* buffer;
    int first;
    int size;
    int capacity;
    int trace;
    int eos;
    ostream_t ostream;
};

struct port {
    /* Invariant if esize != 0
     * wposition <= aposition
     * 0 <= aposition - wposition <= bsize
     * 1 <= gcount <= bsize
     */
    int number;
    int aposition;
    int wposition;
    int esize;
    int gcount;
    int bsize;    

    /* For output ports, we have the buffer to write the value,
     * and one output stream connection for each input port where
     * we are connected.
     */
    int is_outport;
    char *buffer;
    int costream_count;
    int costream_capacity;
    costream_t costreams;

    /* Input ports have the initial values of the stream,
     * a flat to say if it is replicated,
     * and a connection istream to one input port.
     */
    int is_inport;
    char* ivalues;
    int isize;
    int replicate;
    cistream_t cistream;
};

struct costream {
    int replicate; // if it must be replicated on the counterpart
    int gcount; // counterpart group count
    int tsize;  // counterpart team size
    ostream_t ostreams;
};

struct cistream {
    int gcount; // counterpart group count
    int tsize;  // counterpart team size
    istream_t istreams; 
};


#define MIN_BUFFER_SIZE 16 /* elements */

/* ********************************************************************
 * * Thread definition
 * ********************************************************************/

#define MAX_INSTANCE_STACK 256
static instance_t __thread current_instance= NULL;
static int __thread current_instance_next_stack= 1;
static instance_t __thread current_instance_stack[MAX_INSTANCE_STACK]= { NULL };

static int next_thread_id= 1;

/* ********************************************************************
 * * Istream and Ostream operations
 * ********************************************************************/

static void ostream_close(ostream_t ostream)
{
    istream_t istream= ostream->istream;
    istream->is_open= 0;
}



/* ********************************************************************
 * * Task
 * ********************************************************************/

int task_init(task_t* task, void (*start)(task_t), int team_size)
{
    assert(task);
    assert(team_size >= 0);
    int i;
    
    *task= (task_t)malloc(sizeof(struct task));
    
    if (team_size == 0) {
        team_size= 1;
    }
    
    (*task)->start= start;
    (*task)->team_size= team_size;
    (*task)->close_count= 0;
    (*task)->instances= (instance_t)malloc(sizeof(struct task_instance) * team_size);
    (*task)->number= next_thread_id;
    
    for (i= 0; i < team_size; i++) {
        (*task)->instances[i].thread_id= next_thread_id; next_thread_id++;
        (*task)->instances[i].number= i;
        (*task)->instances[i].allopen= 1;
        (*task)->instances[i].state_count= 0;
        (*task)->instances[i].state_capacity= 8;
        (*task)->instances[i].states= (state_t)malloc(sizeof(struct state) * 8);
        (*task)->instances[i].port_count= 0;
        (*task)->instances[i].port_capacity= 8;
        (*task)->instances[i].ports= (port_t)malloc(sizeof(struct port) * 8);
        (*task)->instances[i].task= *task;
    }

    return 0;
}

static void* instance_start(void* arg)
{
    instance_t instance= (instance_t) arg;
    
    assert(current_instance_next_stack < MAX_INSTANCE_STACK);
    current_instance_stack[current_instance_next_stack]= instance;
    current_instance_next_stack++;
    current_instance= (instance_t) instance;
    
    
    instance->task->start(NULL);

    assert(current_instance_next_stack > 1);
    current_instance_next_stack--;
    current_instance= current_instance_stack[current_instance_next_stack - 1];
    return NULL;
}

void task_start(task_t task)
{
    assert(task);
    
    if (task->start) {
        int i;
        for (i= 0; i < task->team_size; i++) {
            pthread_create(&task->instances[i].pthread, NULL, instance_start, &task->instances[i]);
        }
    } else {
        assert(task->team_size == 1);
        assert(current_instance_next_stack < MAX_INSTANCE_STACK);
        current_instance_stack[current_instance_next_stack]= &task->instances[0];
        current_instance_next_stack++;
        current_instance= &task->instances[0];
    }
}

void task_wait(task_t task)
{
    assert(task);
    
    if (task->start) {
        int i;
        for (i= 0; i < task->team_size; i++) {
            pthread_join(task->instances[i].pthread, NULL);
        }
    } else {
        assert(current_instance);
        assert(current_instance_next_stack > 1);

        current_instance_next_stack--;
        current_instance= current_instance_stack[current_instance_next_stack - 1];    
    }
}

void task_close()
{
    instance_t instance= current_instance;
    int i, j, k;
    
    for (i= 0; i < instance->port_count; i++) {
        port_t port= &instance->ports[i];
        for (j= 0; j < port->costream_count; j++) {
            costream_t costream= &port->costreams[j];
            for (k= 0; k < costream->tsize; k++) {
                ostream_close(&costream->ostreams[k]);
            }
        }
    }
}

int task_allopen()
{
    instance_t instance= current_instance;
    
    return instance->allopen;
}



/* ********************************************************************
 * * Copyin and copy out
 * ********************************************************************/

static void task_state(task_t task, int number, int size)
{
    int i, j;
    
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        
        // ensure capacity
        int new_capacity= instance->state_capacity;
        if (new_capacity <= number) {
            while (new_capacity <= number) new_capacity*= 2;
            instance->states= (state_t)realloc(instance->states, sizeof(struct state) * new_capacity);
            instance->state_capacity= new_capacity;
        }
    
        // initialize new states
        if (instance->state_count <= number) {
            for (j= instance->state_count; j <= number; j++) {
                state_t state= &instance->states[j];
                memset(state, 0, sizeof(struct state));
            }
            instance->state_count= number + 1;
        }
        
        // create the state
        state_t state= &instance->states[number];
        if (!state->buffer) {
            state->size= size;
            state->buffer= (char*)malloc(size);
            state->number= number;
        }
    }
}

void task_copyin(task_t task, int number, void * src, int size)
{
    assert(task);
    assert(number >= 0);
    assert(src);
    assert(size > 0);
    
    int i;
    
    task_state(task, number, size);
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        state_t state= &instance->states[number];
        state->is_copy_in_state= 1;
        memcpy(state->buffer, src, size);
    }
}

void task_copyout(task_t task, int number, void * dst, int size)
{
    assert(task);
    assert(number >= 0);
    assert(dst);
    assert(size > 0);
    
    int i;
    
    task_state(task, number, size);
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        state_t state= &instance->states[number];
        state->is_copy_out_state= 1;
        state->copy_out_buffer= dst;
    }
}

void* copyin_acquire(int number)
{
    instance_t instance= current_instance;
    state_t state= &instance->states[number];
 
    assert(0 <= number);
    assert(number < instance->state_count);
    assert(state->is_copy_in_state);
    
    return state->buffer;
}

void* copyout_acquire(int number)
{
    instance_t instance= current_instance;
    state_t state= &instance->states[number];
 
    assert(0 <= number);
    assert(number < instance->state_count);
    assert(state->is_copy_out_state);
    
    return state->copy_out_buffer;
}


/* ********************************************************************
 * * Ports
 * ********************************************************************/

static void task_port(task_t task, int number, int size, int bsize)
{
    int i, j;
    
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        
        // ensure capacity
        int new_capacity= instance->port_capacity;
        if (new_capacity <= number) {
            while (new_capacity <= number) new_capacity*= 2;
            instance->ports= (port_t)realloc(instance->ports, sizeof(struct port) * new_capacity);
            instance->port_capacity= new_capacity;
        }
    
        // initialize new ports
        if (instance->port_count <= number) {
            for (j= instance->port_count; j <= number; j++) {
                port_t port= &instance->ports[j];
                memset(port, 0, sizeof(struct port));
                port->costream_capacity= 8;
                port->costreams= (costream_t) malloc(sizeof(struct costream) * 8);
            }
            instance->port_count= number + 1;
        }
        
        // create the state
        port_t port= &instance->ports[number];        
        assert(!port->buffer);
        assert(!port->is_inport);
        assert(!port->is_outport);
        port->esize= size;
        port->bsize= bsize;
        if (size > 0) {
            port->buffer= (char*)malloc(size * bsize);
            port->number= number;
        }
    }
}

void task_oport(task_t task, int number, int esize, int gcount, int bsize)
{
    assert(task);
    assert(number >= 0);
    assert(esize >= 0);
    assert(gcount >= 1);
    
    int i;
    
    task_port(task, number, esize, bsize);
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        port_t port= &instance->ports[number];
        
        port->is_outport= 1;
        port->gcount= gcount;
    }
}

void task_iport(task_t task, int number, int esize, int gcount, int bsize, void *ivalues, int isize)
{
    assert(task);
    assert(number >= 0);
    assert(esize >= 0);
    assert(gcount >= 1);
    assert(!esize || bsize > 0);
    assert(!ivalues || isize);
    assert(!ivalues || isize <= bsize);
 
    if (bsize < MIN_BUFFER_SIZE) {
        bsize= MIN_BUFFER_SIZE;
    }
    
    int i;
    task_port(task, number, esize, 1);
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        port_t port= &instance->ports[number];
        
        port->is_inport= 1;
        port->bsize= bsize;
        port->gcount= gcount;
        
        if (ivalues) {
            port->ivalues= malloc(isize * esize);
            memcpy(port->ivalues, ivalues, isize * esize);
            port->isize= isize;
        }
    }
}

static int gcd(int a, int b)
{
    if (b == 0) { return a; }
    else { return gcd(b, a % b); }
}

static int lcm(int a, int b)
{
    return (a*b) / gcd(a,b);
}

void port_connect(task_t ot, int onumber, task_t it, int inumber)
{
    // Validate Tasks
    assert(ot);
    assert(it);
    
    // Validate port numbers
    assert(0 <= onumber);
    assert(onumber < ot->instances[0].port_count);
    assert(ot->instances[0].ports[onumber].is_outport);
    assert(0 <= inumber);
    assert(inumber < it->instances[0].port_count);
    assert(it->instances[0].ports[inumber].is_inport);

    // Obtain team and groupcount properties
    int otsize= ot->team_size;
    int ogcount= ot->instances[0].ports[onumber].gcount;
    int itsize= it->team_size;
    int igcount= it->instances[0].ports[inumber].gcount;
    int replicate= it->instances[0].ports[inumber].replicate;
    
    int oinumber;
    int iinumber;
        
    // Verify that input port is not already connected
    assert(!it->instances[0].ports[inumber].cistream);
    // Create input connection stream structure
    for (iinumber= 0; iinumber < itsize; iinumber++) {
        
        // Gets the current instance
        instance_t iinstance= &it->instances[iinumber];
        port_t iport= &iinstance->ports[inumber];
        
        // Creates the new cistream
        iport->cistream= (cistream_t) malloc(sizeof(struct cistream));
        cistream_t cistream= iport->cistream;
        cistream->gcount= ogcount;
        cistream->tsize= otsize;
        cistream->istreams= (istream_t) malloc(sizeof(struct istream) * otsize);
    }

    // Create the oport connection stream structures
    for (oinumber= 0; oinumber < otsize; oinumber++) {
        
        // Gets the current instance
        instance_t oinstance= &ot->instances[oinumber];
        port_t oport= &oinstance->ports[onumber];
        
        // Increase oport capacity... if it is needed
        assert(oport->costream_count <= oport->costream_capacity);
        if (oport->costream_count == oport->costream_capacity)
        {
            int new_capacity= oport->costream_capacity * 2;
            oport->costreams= (costream_t)realloc(oport->costreams, sizeof(struct costream) * new_capacity);
            oport->costream_capacity= new_capacity;
        }

        // Initializes the costream
        oport->costream_count= oport->costream_count + 1;
        costream_t costream= &oport->costreams[oport->costream_count - 1];
        costream->replicate= replicate;
        costream->gcount= igcount;
        costream->tsize= itsize;
        costream->ostreams= (ostream_t) malloc(sizeof(struct ostream) * itsize);
        
        // Creates the istreams
        for (iinumber= 0; iinumber < itsize; iinumber++) {

            // Gets the current instance
            instance_t iinstance= &it->instances[iinumber];
            port_t iport= &iinstance->ports[inumber];
            cistream_t cistream= iport->cistream;
            
            // Gets the stream ends
            ostream_t ostream= &costream->ostreams[iinumber];
            istream_t istream= &cistream->istreams[oinumber];
            
            // Initializate ostream
            ostream->size= oport->esize;
            ostream->first= 0;
            ostream->next= 0;

            // Initializate istream
            istream->size= iport->esize;
            istream->capacity= iport->esize * iport->bsize;
            istream->is_open= 0;
            istream->first= 0;
            istream->next= 0;
            istream->eos= 0;
            istream->buffer= malloc(iport->esize * iport->bsize);

            // connect both
            ostream->capacity= istream->capacity;
            ostream->istream= istream;
            istream->ostream= ostream;
            istream->is_open= 1;
        }
    }
    
    // Feed the istreams with the initialization values
    for (iinumber= 0; iinumber < itsize; iinumber++) {
        
        // Gets the current instance
        instance_t iinstance= &it->instances[iinumber];
        port_t iport= &iinstance->ports[inumber];
        cistream_t cistream= iport->cistream;

        // For each element number
        int esize= iport->esize;
        int isize= iport->isize;
        int ienumber; // input element number
        for (ienumber= 0; ienumber < isize; ienumber++) {

            // Must be added if it is for this instance
            if ((ienumber/igcount) % itsize == iinumber
                    || replicate) {

                oinumber= (ienumber/ogcount) % otsize; // who is the sender
                istream_t istream= &cistream->istreams[oinumber]; // where is received
                ostream_t ostream= istream->ostream;
                int next= istream->next;
                int ipos= ienumber * esize;
                int bpos= next;
                memcpy(&istream->buffer[bpos], &iport->ivalues[ipos], esize);
                istream->next= next + esize;
                ostream->next= next + esize;
                iport->aposition++;
            }
        }
    }    
    
}

void pthread_yield();
static void ostream_push(ostream_t ostream, void* buffer)
{
    int size= ostream->size;
    int next= ostream->next;
    istream_t istream= ostream->istream;

    int state= mintaka_get_state();
    mintaka_wait_send((uintptr_t)ostream, 1* size);
    
    if (size != 0) {
        int count= next - ostream->first;
        int capacity= ostream->capacity;
        int index= next % capacity;
        
        while (capacity - count < size)
        {
            assert(ostream->istream);
            assert(ostream->istream->is_open);
            pthread_yield();

            count= next - ostream->first;
        }

        mintaka_send((uintptr_t)ostream, 1 * size);
        memcpy(&istream->buffer[index], buffer, size);
        ostream->next= next + size;
        istream->next= next + size;
    } else {
        mintaka_send((uintptr_t)ostream, 1);
        ostream->next= next + 1;
        istream->next= next + 1;
    }
    mintaka_set_state(state);
}

static void* istream_peek(istream_t istream, int enumber)
{
    assert(istream);
    assert(enumber >= 0);
    assert(enumber * istream->size <= istream->capacity);

    instance_t instance= current_instance;
    
    int size= istream->size;
    int first= istream->first;
    int next= istream->next;
    ostream_t ostream= istream->ostream;
    
    int state= mintaka_get_state();
    
    if (size != 0) {
        int wait= first/size + enumber - istream->trace + 1;
        if (wait > 0)
        {
            mintaka_wait_receive((uintptr_t)ostream, wait * size);
        }
        
        //int offsetCount= (next - first) / size;
        int capacity= istream->capacity;
        int offset= enumber * size;
        int index= (first + offset) % capacity;

        while (next - first <= (offset + size - 1))
        {
            if (!istream->is_open)
            {
                istream->eos= 1;
                instance->allopen= 0;
                if (wait)
                {
                    mintaka_receive((uintptr_t)ostream, wait * size);
                    mintaka_set_state(state);
                }
                return &istream->buffer[index];
            }
            assert(istream->ostream);
            pthread_yield();

            next= istream->next;
        }

        if (wait > 0)
        {
            istream->trace+= wait;
            mintaka_receive((uintptr_t)ostream, wait * size);
            mintaka_set_state(state);
        }
        return &istream->buffer[index];
    } else {
        assert(first == 0);
        int wait= enumber - istream->trace + 1;
        if (wait)
        {
            mintaka_wait_receive((uintptr_t)ostream, wait);
        }

        while (next <= enumber)
        {
            if (!istream->is_open)
            {
                istream->eos= 1;
                instance->allopen= 0;
                if (wait)
                {
                    mintaka_receive((uintptr_t)ostream, wait);
                    mintaka_set_state(state);
                }
                return NULL;
            }
            assert(istream->ostream);
            pthread_yield();

            next= istream->next;
        }
        
        if (wait)
        {
            istream->trace+= wait;
            mintaka_receive((uintptr_t)ostream, wait);
            mintaka_set_state(state);
        }
        return NULL;
    }
}

static void istream_pop(istream_t istream, int ecount)
{
    assert(istream);
    assert(istream->ostream);
    assert(ecount >= 0);
    assert(istream->next - istream->first  >= ecount * istream->size);

    int size= istream->size * ecount;
    int first= istream->first;
    ostream_t ostream= istream->ostream;

    istream->first= first + size;
    ostream->first= first + size;
}

static int position2nelement(instance_t instance, port_t port, int position)
{
    int result;
    
    if (port->replicate) {
        result= position;
    } else {
        result= position % port->gcount
        + (position / port->gcount) * port->gcount * instance->task->team_size
        + instance->number * port->gcount;
    }
    
    return result;
}

static int nelement2position(cistream_t cistream, int nelement)
{
    int result
        = (nelement/(cistream->gcount*cistream->tsize)) * cistream->gcount
        + nelement % cistream->gcount;
    
    return result;
}

static void costream_push_replicate(costream_t costream, char* buffer)
{
    assert(costream->replicate);
    
    int i;
    for (i= 0; i < costream->tsize; i++)
    {
        ostream_t ostream= &costream->ostreams[i];
        ostream_push(ostream, buffer);
    }
}

static void costream_push_single(costream_t costream, int nelement, char* buffer)
{
    assert(!costream->replicate);

    int i= (nelement / costream->gcount) % costream->tsize;
    
    ostream_t ostream= &costream->ostreams[i];
    ostream_push(ostream, buffer);
}

static void costream_push(costream_t costream, int nelement, char* buffer)
{
    assert(costream);
    assert(0 <= nelement);
    
    if (costream->replicate) {
        costream_push_replicate(costream, buffer);
    } else {
        costream_push_single(costream, nelement, buffer);
    }
}

static char* cistream_peek(cistream_t cistream, int wnelement, int pnelement)
{
    assert(cistream);
    assert(0 <= wnelement);
    assert(0 <= pnelement);
    assert(wnelement <= pnelement);

    int i= (pnelement / cistream->gcount) % cistream->tsize;
    int j= (wnelement / cistream->gcount) % cistream->tsize;
    while (i != j) {
        wnelement= wnelement + 1;
        j= (wnelement / cistream->gcount) % cistream->tsize;
    }
    
    int wposition= nelement2position(cistream, wnelement);
    int pposition= nelement2position(cistream, pnelement);
    int distance= pposition - wposition;
    
    istream_t istream= &cistream->istreams[i];
    char* buffer= istream_peek(istream, distance);
    
    return buffer;
}

static void cistream_pop(cistream_t cistream, int nelement)
{
    assert(cistream);
    assert(0 <= nelement);

    int i= (nelement / cistream->gcount) % cistream->tsize;
    istream_t istream= &cistream->istreams[i];
    istream_pop(istream, 1);
}

void oport_acquire(int number, int ecount)
{
    // obtain current task instance and port
    instance_t instance= current_instance;
    assert(instance);
    
    // verify port number is correct
    assert(0 <= number);                   // number is positive
    assert(number < instance->port_count); // port number is valid
    port_t port= &instance->ports[number]; // [obtain port]
    // verify supports oport_acquire operation
    assert(port->is_outport);              // port is output port
    assert(port->esize != 0);              // virtual oport do not acquire
    
    // verify ecount argument is correct
    assert(0 <= ecount);                   // ecount is positive
    assert(ecount <= port->bsize);         // ecount is less or equal than bsize
    
    // check invariant
    int aposition= port->aposition;
    int wposition= port->wposition;
    int bsize= port->bsize;
    assert(wposition <= aposition + ecount);
    assert(0 <= (aposition + ecount) - wposition);
    assert((aposition + ecount) - wposition <= bsize);
    
    // Update aquire position
    port->aposition= aposition + ecount;
}

int oport_tryacquire(int number, int ecount)
{
    oport_acquire(number, ecount);
    return ecount;
}

void* oport_peek(int number, int enumber)
{
    instance_t instance= current_instance;
    assert(instance);
    
    // verify port number is correct
    assert(0 <= number);                   // number is positive
    assert(number < instance->port_count); // port number is valid
    port_t port= &instance->ports[number]; // [obtain port]
    // verify supports oport_acquire operation
    assert(port->is_outport);              // port is output port
    assert(port->esize != 0);              // virtual oport do not peek
    
    // verify ecount argument is correct
    assert(0 <= enumber);                  // enumber is positive
    assert(enumber < port->bsize);         // enumber is less than bsize

    /* peek invariant:
     *  valid range: [wposition .. aposition)
     *  0 <= wposition + enumber < aposition
     */
    int aposition= port->aposition;
    int wposition= port->wposition;
    assert(wposition + enumber < aposition);
    
    void* buffer= &port->buffer[port->esize * ((wposition + enumber) % port->bsize)];
    return buffer;
}

void oport_push(int number, int ecount)
{
    instance_t instance= current_instance;
    assert(instance);
    task_t task= instance->task;
    
    // verify port number is correct
    assert(0 <= number);                   // number is positive
    assert(number < instance->port_count); // port number is valid
    port_t port= &instance->ports[number]; // [obtain port]
    // verify supports oport_acquire operation
    assert(port->is_outport);              // port is output port
    
    int aposition= port->aposition;
    int wposition= port->wposition;
    int bsize= port->bsize;
    if (port->esize > 0)
    {
        // verify ecount value
        assert(0 <= ecount);                   // ecount is positive
        assert(ecount <= port->bsize);         // ecount is less or equal than bsize

        // check invariant
        assert(wposition <= aposition + ecount);
        assert(0 <= aposition - (wposition + ecount));
        assert(aposition - (wposition + ecount) <= bsize);
    } 
    else 
    {
        // verify ecount value
        assert(0 <= ecount);
        // lie about bsize to get a valid buffer
        bsize= 1;

        // verify no need for else, no negative values
        assert(port->esize == 0);
    }

    int i, j;
    int position;
    int otsize= task->team_size;
    int oinumber= instance->number;
    int ogcount= port->gcount;
    for (j= 0; j < ecount; j++) {
        position= wposition + j;
        int nelement= position2nelement(instance, port, position);
        int bpos= port->esize * (position % bsize);
        char* buffer= &port->buffer[bpos];
        for (i= 0; i < port->costream_count; i++) {
            costream_t costream= &port->costreams[i];
            costream_push(costream, nelement, buffer);
        }
    }
    port->wposition= wposition + ecount;
}

void iport_acquire(int number, int ecount)
{
    instance_t instance= current_instance;
    assert(instance);
    port_t port= &instance->ports[number];

    assert(0 <= number);
    assert(number < instance->port_count);
    assert(port->is_inport);
    assert(!port->esize || ecount + port->aposition <= port->bsize + port->wposition);
    
    port->aposition= port->aposition + ecount;
    if (port->esize != 0) {
        int i;
        int last= ecount - 1 + port->isize;
        for (i= 0; i <= last; i++) {
            iport_peek(number, i);
        }
        // iport_peek(number, ecount - 1 + port->isize);
    } else {
        iport_peek(number, port->aposition - 1);
    }
}

int iport_tryacquire(int number, int ecount)
{
    iport_acquire(number, ecount);
    return ecount;
}

void* iport_peek(int number, int enumber)
{
    instance_t instance= current_instance;
    assert(instance);
    port_t port= &instance->ports[number];

    assert(0 <= enumber);
    assert(0 <= number);
    assert(number < instance->port_count);
    assert(port->is_inport);
    assert(port->wposition + enumber < port->aposition);
    
    void* buffer= port->buffer;
    int wposition= port->wposition;
    int pposition= port->wposition + enumber;
    int wnelement= position2nelement(instance, port, wposition);
    int pnelement= position2nelement(instance, port, pposition);
    buffer= cistream_peek(port->cistream, wnelement, pnelement);

    return buffer;
}

void iport_pop(int number, int ecount)
{
    instance_t instance= current_instance;
    assert(instance);
    port_t port= &instance->ports[number];
    
    assert(0 <= number);
    assert(number < instance->port_count);
    assert(port->is_inport);
    assert(port->esize != 0); 
    assert(port->wposition + ecount <= port->aposition);
    
    cistream_t cistream= port->cistream;
    int i;
    for (i= 0; i < ecount; i++)
    {
        int position= port->wposition;
        int nelement= position2nelement(instance, port, position);
        cistream_pop(cistream, nelement);
    }
    port->wposition= port->wposition + ecount;
}

void ioport_bypass(int iport, int oport, int ecount);



/* ********************************************************************
 * * Shared
 * ********************************************************************/

// static void task_state(task_t task, int number, int size);
void task_shared(task_t task, int number, void* ivalue, int size)
{
    assert(task);
    assert(number >= 0);
    
    task_state(task, number, size);
    int i;
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        state_t state= &instance->states[number];
        
        assert(number < instance->state_count);
        assert(!state->is_shared_state);
        state->is_shared_state= 1;
        memcpy(state->buffer,ivalue,size);
        state->shared_buffer= state->buffer;
    }
    
}

void shared_async(task_t utask, int unumber, task_t atask, int anumber)
{
    assert(utask);
    assert(atask);
    assert(unumber >= 0);
    assert(anumber >= 0);
    
    int j;
    instance_t uinstance= &utask->instances[0];
    state_t ustate= &uinstance->states[unumber];
    assert(unumber < uinstance->state_count);
    assert(ustate->is_shared_state);
        
    for (j= 0; j < atask->team_size; j++) {
        instance_t ainstance= &atask->instances[j];
        state_t astate= &ainstance->states[anumber];
        assert(anumber < ainstance->state_count);
        assert(astate->is_shared_state);

        // assert(astate->shared_buffer == ustate->shared_buffer);
        astate->shared_buffer= ustate->shared_buffer;
    }
}

void shared_sync(task_t utask, int unumber, task_t atask, int anumber)
{
    shared_async(utask, unumber, atask, anumber);
}

void* shared_acquire(int number)
{
    instance_t instance= current_instance;
    
    assert(instance);
    assert(0 <= number);
    assert(number < instance->state_count);
    
    state_t state= &instance->states[number];
    assert(state->is_shared_state);
    
    char* result= state->shared_buffer;
    return result;
}

void shared_update(int state)
{}

void shared_check(int state)
{}



/* ********************************************************************
 * * Task team
 * ********************************************************************/

int task_leader()
{
    instance_t instance= current_instance;
    
    assert(instance);
    int result;
    if (instance->is_replicate) {
        task_t task= instance->task;
        int inumber= instance->number;
        int tsize= task->team_size;
        port_t replicate_port= instance->replicate_port;
        int wposition= replicate_port->wposition;
        int gsize= replicate_port->gcount;
        
        result= (wposition / gsize) % tsize == inumber;
    } else {
        result= 1;
    }
    
    return result;
}

int task_team_me()
{
    instance_t instance= current_instance;
    
    assert(instance);
    int result= instance->number;
    return result;
}

void iport_replicate(task_t task, int number)
{
    assert(task);
    assert(0 <= number);
    
    int i;
    for (i= 0; i < task->team_size; i++) {
        instance_t instance= &task->instances[i];
        port_t port= &instance->ports[number];
        
        assert(number < instance->port_count);
        assert(port->is_inport);
        assert(!port->replicate);
        
        port->replicate= 1;
        instance->is_replicate= 1;
        instance->replicate_port= port;
    }
}



/* ********************************************************************
 * * Tracing facilities
 * ********************************************************************/

#define TASK_EVENT          13578
#define INSTANCE_EVENT      13579
#define ITERATION_EVENT     13580
#define TEAMREPLICATE_EVENT 13581

static __thread int next_iteration= 1;
static __thread int next_teamreplicate= 1;

void trace_app_begin(char* name)
{
    mintaka_set_filebase(name);
    mintaka_app_begin();
    mintaka_thread_begin(1, 1);
}
void trace_app_end()
{
    mintaka_thread_end();
    mintaka_app_end();
    fprintf(stderr, "Acolib: Merging trace... "); fflush(NULL);
    mintaka_merge();
    fprintf(stderr, "done.\n");
}

void trace_instance_begin()
{
    mintaka_thread_begin(1, (uint64_t)current_instance->thread_id);
    mintaka_event(TASK_EVENT, current_instance->task->number);
    mintaka_event(INSTANCE_EVENT, current_instance->number + 1);
    mintaka_state_schedule();
}

void trace_instance_end()
{
    mintaka_event(INSTANCE_EVENT, 0);
    mintaka_event(TASK_EVENT, 0);
    mintaka_thread_end();
}

void trace_iteration_begin()
{
    mintaka_event(ITERATION_EVENT, next_iteration);
    mintaka_state_run();
    next_iteration++;
}

void trace_iteration_end()
{
    mintaka_state_schedule();
    mintaka_event(ITERATION_EVENT, 0);
}

void trace_teamreplicate_begin()
{
    mintaka_event(TEAMREPLICATE_EVENT, next_teamreplicate);
    mintaka_state_run();
    next_teamreplicate++;
}

void trace_teamreplicate_end()
{
    mintaka_state_schedule();
    mintaka_event(TEAMREPLICATE_EVENT, 0);
}
