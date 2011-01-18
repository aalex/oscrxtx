// OSCrx.c
//
// written by mike@mikewoz.com
//
// Requires liblo OSC library:
// http://liblo.sourceforge.net/
//

#include <stdlib.h>
#include <string>
#include <vector>

#include <cstdio>  // added by ZS. to work with Ubuntu 9.1

#define UNUSED(x) ((void) (x))

#include "m_pd.h"
//#include "s_stuff.h"
#include "lo/lo.h"

using namespace std;


//#define OSC_DEBUG True


// The followind is support for Pd polling functions:
typedef void (*t_fdpollfn)(void *ptr, int fd);
extern "C" void sys_addpollfn(int fd, t_fdpollfn fn, void *ptr);
extern "C" void sys_rmpollfn(int fd);

// need to declare this function in advance:
void OSCrx_liblo_error(int num, const char *msg, const char *path)
{
	post("OSCrx (liblo) error %d in path %s: %s", num, path, msg);
}

// We keep a global vector of OSCrx servers (each with an lo_server), so that
// multiple Pd objects can use the reuse the same resource. ie, there can be
// multiple instances of an [OSCrx] object listening to the same port.
class OSCrx_server {
public:
	OSCrx_server(t_symbol *a, int pt, int pr);
	~OSCrx_server();

	lo_server lo_serv;
	int port;
	int proto;
	t_symbol *addr;
	t_clock *x_clock; // Pd clock used for periodic polling
};

vector<OSCrx_server*> g_OSCrx_servers;
vector<OSCrx_server*>::iterator g_iter;

void OSCrx_server_poll(OSCrx_server *s)
{
    int recv = 0;
    if (s->lo_serv) recv = lo_server_recv_noblock(s->lo_serv, 0);

    // If we've just received something, then assume there is more, and set the
    // delay to something very small. If nothing was received, assume we can
    // wait a little longer:

    if (recv) clock_delay(s->x_clock, 0);
    else clock_delay(s->x_clock, 5);
}

OSCrx_server::OSCrx_server(t_symbol *a, int pt, int pr)
{
    addr = a;
	port = pt;
	proto = pr;
		
	char portString[10]; // note: lo_server_new() expects the port as a string
	sprintf(portString, "%d", port);
		
	if (addr==gensym("NULL"))
	{
		lo_serv = lo_server_new_with_proto(portString, proto, OSCrx_liblo_error);
	} else {
		lo_serv = lo_server_new_multicast(addr->s_name, portString, OSCrx_liblo_error);
	}
        
    x_clock = clock_new(this, (t_method)OSCrx_server_poll);
    OSCrx_server_poll(this); // poll once to start recursive polling
}
	
OSCrx_server::~OSCrx_server()
{
    clock_free(x_clock);
	lo_server_free(lo_serv);
}

	
// The following 2 structs are needed by the OSCrx_free() method:
typedef struct _lo_method {
        const char        *path;
        const char        *typespec;
        lo_method_handler  handler;
        char              *user_data;
        struct _lo_method *next;
} *internal_lo_method;

#ifdef LO_VERSION_25
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
#else
typedef struct _lo_server {
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
        int                  sockets_len;
        int                  sockets_alloc;
#ifdef HAVE_POLL
        struct pollfd        *sockets;
#else
        struct { int fd; }   *sockets;
#endif
} *internal_lo_server;
#endif



// The Pd t_class instance, and related object struct:
static t_class *OSCrx_class;

typedef struct _OSCrx
{
	t_object x_obj;
	OSCrx_server *serv;
} t_OSCrx;


// This is a generic callback that gets called by liblo for incoming
// messages. The messages are converted to Pd atoms and output in list
// format from the correct instance, held in the user_data pointer.
static int OSCrx_liblo_callback(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    UNUSED(data);

#ifdef OSC_DEBUG
	printf("************ OSCrx_liblo_callback() got message: %s\n", (char*)path);
	for (int i=0; i<argc; i++) {
		printf("arg %d '%c' ", i, types[i]);
    	lo_arg_pp((lo_type) types[i], argv[i]);
    	printf("\n");
	}
	printf("\n");
	fflush(stdout);


	startpost("************ OSCrx_liblo_callback() got message: %s ", (char*)path);
	for (int i=0; i<argc; i++) {
        if (lo_is_numerical_type((lo_type)types[i]))
            postfloat(lo_hires_val((lo_type)types[i], argv[i]));
        else
        	poststring((const char*) argv[i]);
	}
	endpost();

#endif
	


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



static void OSCrx_bang(t_OSCrx *x)
{
    UNUSED(x);
	post("The following %d OSC servers are listening for messages:", g_OSCrx_servers.size());
	for (g_iter = g_OSCrx_servers.begin(); g_iter != g_OSCrx_servers.end(); g_iter++)
	{
		post("%s", lo_server_get_url((*g_iter)->lo_serv));
	}	
	
}

void OSCrx_poll(lo_server *s, int sockfd)
{
    UNUSED(sockfd);
	// This function checks if liblo has any pending messages and
	// dispatches a matching method if one is found.
	//
	// The 2nd argument is the amount of time to wait. By passing 0
	// the function will return immediately.
	
	lo_server_recv_noblock(s, 0);
}

static void *OSCrx_new(t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);
	t_OSCrx *x = (t_OSCrx *) pd_new(OSCrx_class);
	
	int port;
	int proto = LO_UDP;
	t_symbol *host = gensym("NULL");
	
	// The user can provide a multicast group and port:
	if ((argc==1) && (argv->a_type == A_FLOAT))
	{
		port = (int) atom_getfloat(argv);
	} else if ((argc==2) && (argv->a_type==A_SYMBOL) && ((argv+1)->a_type==A_FLOAT))
	{
		host = atom_getsymbol(argv);
		port = (int) atom_getfloat(argv+1);
	} else if ((argc==2) && (argv->a_type==A_FLOAT) && ((argv+1)->a_type==A_SYMBOL))
	{
		port = (int) atom_getfloat(argv);
		if (atom_getsymbol(argv+1)==gensym("tcp")) proto = LO_TCP;
	} else {
		post("OSCrx ERROR: creation arguments must be either <port> or <port tcp> or <host port>");
		return NULL;
	}
	
  
	// we look through the global server and see if a server already exists for
	// the given addr/port/proto. If not, we create a new one.
	  
	bool found = false;
	//for (libloIter = g_libloVector.begin(); libloIter != g_libloVector.end(); libloIter++)
	for (g_iter = g_OSCrx_servers.begin(); g_iter != g_OSCrx_servers.end(); g_iter++)
	{
		if ( ((*g_iter)->port == port) && ((*g_iter)->addr == host) && ((*g_iter)->proto == proto) )
		{
			x->serv = (*g_iter);
			found = true;

#ifdef OSC_DEBUG
		  	post("OSCrx: joining existing OSC listener on port %d", port);
#endif

		}
	}
	
	if (!found)
	{

		x->serv = new OSCrx_server(host, port, proto);

		if (!x->serv->lo_serv)
		{
			post("OSCrx ERROR: cannot bind to port %d.", port);
			return NULL;
		}

#ifdef OSC_DEBUG
	  	post("OSCrx: created new OSC listener. proto=%d, port=%d, multicast=%s", proto, port, host->s_name);
#endif


		// add it to the global vector:
		g_OSCrx_servers.push_back(x->serv);

		// Create a polling function that will force this server to be read at
		// regular intervals by the Pd scheduler.
	  	// OOPS: this messes up liblo when using TCP, since liblo is waiting
	  	// on a termination string in the stream before it can say that the
	  	// message is complete. So... have switched to using a t_clock within
        // the OSCrx_server instance
		/*
	  	int lo_fd = lo_server_get_socket_fd(x->serv->lo_serv);
		sys_addpollfn(lo_fd, (t_fdpollfn)OSCrx_poll, x->serv->lo_serv);
		*/

	}

	// Add a new callback for the thread that will be called for any path
	// and any types of arguments (hence the NULL, NULL). Also, make sure
	// to pass the pointer to this Pd object as user_data.
	lo_server_add_method(x->serv->lo_serv, NULL, NULL, OSCrx_liblo_callback, x);
	
	// create an outlet for OSC messages:
	outlet_new(&x->x_obj, 0);	

	return (x);
}


static void OSCrx_free(t_OSCrx *x)
{
	// Here, we need to free methods from the liblo server if one Pd object
	// is destroyed but others still exist. When no other remaining instances
	// require the liblo server, then we can destroy it as well.
	//
	// Unfortunately, the liblo library doesn't provide the hooks to do this;
	// A method can only be removed like this:
	// lo_server_del_method(lo_server s, const char *path, const char *typespec)
	//
	// For us, path and typespec are both NULL, so ALL methods will be deleted!
	//
	// Thus, we've copied the lo_server struct (and named it internal_lo_server)
	// so that we can remove methods if their user_data matches

	internal_lo_server s = (internal_lo_server) (x->serv->lo_serv);
	
#ifdef OSC_DEBUG
	post("OSCrx_free for %s", lo_server_get_url(x->serv->lo_serv));
#endif
	
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
    		free((char *)it->path);
    		free((char *)it->typespec);
    		free(it);
    		it = prev;
    	   	
    	}
    	prev = it;
	    if (it) it = next;
	    
    }
    
    
	// if no other OSCrx_server objects need this liblo server, destroy it:
    if (!s->first)
    {
    	
    	for (g_iter = g_OSCrx_servers.begin(); g_iter != g_OSCrx_servers.end(); g_iter++)
    	{
        	// find the correct lo_server in the g_libloVector, and remove it:
    		if ((*g_iter) == x->serv)
    		{
 	    		g_OSCrx_servers.erase(g_iter);
	    		break;
    		}
       	}

    	//sys_rmpollfn(lo_server_get_socket_fd(x->serv->lo_serv));
        delete x->serv;
    	
#ifdef OSC_DEBUG
    	post("OSCrx: No more listeners need port %d, so destroyed the server.", x->serv->port);
#endif
    	
    }

}


extern "C" void OSCrx_setup(void)
{
	//post("OSCrx loaded");
	g_OSCrx_servers.clear();
	OSCrx_class = class_new(gensym("OSCrx"), (t_newmethod)OSCrx_new, (t_method)OSCrx_free, sizeof(t_OSCrx), CLASS_DEFAULT, A_GIMME, 0);
    class_addbang(OSCrx_class, (t_method)OSCrx_bang);
}
