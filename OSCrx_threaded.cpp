// OSCrx.c
//
// written by mike@mikewoz.com
//
// Requires liblo OSC library:
// http://liblo.sourceforge.net/
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "m_pd.h"
#include "lo/lo.h"


using namespace std;

// We keep a global vector of liblo threads so that multiple Pd
// objects can use the same thread. ie, there can be multiple
// instances of an [OSCrx] object listening to one port.
vector<lo_server_thread> g_libloThreads;

// The following 2 structs are needed by the OSCrx_free() method:
typedef struct _lo_method {
        const char        *path;
        const char        *typespec;
        lo_method_handler  handler;
        char              *user_data;
        struct _lo_method *next;
} *internal_lo_method;

typedef struct _lo_server {
        int                      socket;
        struct addrinfo         *ai;
        lo_method                first;
        lo_err_handler           err_h;
        int                      port;
        char                    *hostname;
        char                    *path;
        int                      protocol;
        void                    *queued;
        struct sockaddr_storage  addr;
        socklen_t                addr_len;
} *internal_lo_server;



// The Pd t_class instance, and related object struct:
static t_class *OSCrx_class;

typedef struct _OSCrx
{
	t_object x_obj;
	int port;
} t_OSCrx;


// This is a generic callback that gets called by liblo for incoming
// messages. The messages are converted to Pd atoms and output in list
// format from the correct instance, held in the user_data pointer.
int OSCrx_liblo_callback(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	/*
	printf("************ OSCrx_liblo_callback() got message: %s\n", (char*)path);
	for (int i=0; i<argc; i++) {
		printf("arg %d '%c' ", i, types[i]);
    	lo_arg_pp((lo_type) types[i], argv[i]);
    	printf("\n");
	}
	printf("\n");
	fflush(stdout);
	*/
	
	// get the pointer to the Pd instance from *user_data:
	t_OSCrx *x = (t_OSCrx*) user_data;	
	
	// parse the data, and convert from OSC to Pd atoms:
	t_atom outAtoms[argc];
	for (int i=0; i<argc; i++)
	{
		if (lo_is_numerical_type((lo_type)types[i]))
		{
			SETFLOAT(outAtoms+i, (float) lo_hires_val((lo_type)types[i], argv[i]));
		} else {
			SETSYMBOL(outAtoms+i, gensym((char*)argv[i]));
		}
	}
	
	// output a list with the path as the selector:
	outlet_anything(x->x_obj.ob_outlet, gensym((char*)path), argc, outAtoms);
	
	return 1;
}


void OSCrx_liblo_error(int num, const char *msg, const char *path)
{
	post("OSCrx (liblo) error %d in path %s: %s", num, path, msg);
}


static void OSCrx_bang()
{
	post("The following %d threads are listening to OSC messages:", g_libloThreads.size());
	vector<lo_server_thread>::iterator iter;
	for (iter = g_libloThreads.begin(); iter != g_libloThreads.end(); iter++)
	{
		post("%s, port %d", lo_server_thread_get_url(*iter), lo_server_thread_get_port(*iter));
	}	
	
}

static void *OSCrx_new(t_floatarg port)
{
	t_OSCrx *x = (t_OSCrx *) pd_new(OSCrx_class);

  
	// we look through the list of threads and see if a thread already
	// exists for the given port. If not, we create a new liblo thread.
	// Otherwise, we just add a handler to that thread:
	  
	vector<lo_server_thread>::iterator iter;  
	bool found = false;
	for (iter = g_libloThreads.begin(); iter != g_libloThreads.end(); iter++)
	{
		if (lo_server_thread_get_port(*iter) == (int) port)
		{
			// briefly stop the thread (necessary?)
			lo_server_thread_stop(*iter);
			
			// Add a new callback for the thread that will be called for any path
			// and any types of arguments (hence the NULL, NULL). Also, make sure
			// to pass the pointer to this Pd object as user_data.
			lo_server_thread_add_method((*iter), NULL, NULL, OSCrx_liblo_callback, x);
			
			// restart the thread:
			lo_server_thread_start(*iter);
			
		  	post("OSCrx: joining existing OSC listener on port %d", port);
			
			found = true;
		}
	}
	
	if (!found)
	{
		// if a thread for this port was not found, then we need to create one:
		
		lo_server_thread t;
		char portString[10]; // note: lo_server_thread_new() expects the port as a string
		sprintf(portString, "%d", (int)port);
		t = lo_server_thread_new(portString, OSCrx_liblo_error);

		// add it to the global 
		g_libloThreads.push_back(t);
		
		// add the callback (same as above), and start the thread:
	  	lo_server_thread_add_method(t, NULL, NULL, OSCrx_liblo_callback, x);
	  	lo_server_thread_start(t);
	  	
	  	post("OSCrx: created new OSC listener on port %s", portString);
	}
	
	x->port = (int) port;
	outlet_new(&x->x_obj, 0);

	return (x);
}

static void OSCrx_free(t_OSCrx *x)
{
	// Here, we need to free methods from threads if one Pd object instance
	// is destroyed but others may still exist. When no other Pd instances
	// require the thread, then we can destroy the entire thread.
	//
	// But, you can't do this using high-level liblo, since a method can
	// only be removed like this:
	// lo_server_thread_del_method(lo_server_thread t, const char *path, const char *typespec)
	// For us, path and typespec are both NULL, so ALL methods will be deleted!
	//
	// THUS, THE SUPER-LOW-LEVEL STRATEGY:
	
	
	// get the lo_server for this thread:
	internal_lo_server s;
	bool found = false;
	vector<lo_server_thread>::iterator iter;  
	for (iter = g_libloThreads.begin(); iter != g_libloThreads.end(); iter++)
	{
		if (lo_server_thread_get_port(*iter) == x->port) 
		{
			s = (internal_lo_server) lo_server_thread_get_server(*iter);
			found = true;
			break;
		}
	}
	
	if (!found)
	{
		post("OSCrx error: Could not free method! The server doesn't exist.");
		return;
	}
	
    if (!s->first)
    {
		post("OSCrx error: Could not free method! The server has no registered callbacks.");   	
    	return;
    }

    internal_lo_method it, prev, next;
    it = (internal_lo_method) s->first;
    prev = it;
    while (it)
    {
    	// in case we free it:
    	next = it->next;

    	// if the user_data points to this Pd instance:
    	if (it->user_data == (char*)x)
    	{
    		// Take care when removing the head:
    		if (it == s->first) {
    			s->first = it->next;
    		} else {
    			prev->next = it->next;
    		}
    		next = it->next;
    		free((void *)it->path);
    		free((void *)it->typespec);
    		free(it);
    		it = prev;
    	}
    	prev = it;
	    if (it) it = next;
    }
	
	// if no other OSCrx objects need this thread, destroy it:
    if (!s->first)
    {
    	lo_server_thread_free(*iter);
    	g_libloThreads.erase(iter);
    	post("OSCrx: No more listeners need port %d, so destroyed the server.", x->port);
    }
    
    
}


extern "C" void OSCrx_setup(void)
{
	g_libloThreads.clear();
	OSCrx_class = class_new(gensym("OSCrx"), (t_newmethod)OSCrx_new, (t_method)OSCrx_free, sizeof(t_OSCrx), CLASS_DEFAULT, A_DEFFLOAT, 0);
    class_addbang(OSCrx_class, (t_method)OSCrx_bang);
}
