// OSCtx.c
//
// written by mike@mikewoz.com
//
// Requires liblo OSC library:
// http://liblo.sourceforge.net/
//

#include <stdlib.h>
#include <string>
#include <sstream>
#include <vector>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include <cstdio>  // added by ZS. to work with Ubuntu 9.1


#include "m_pd.h"
#include "lo/lo.h"
#include "lo/lo_lowlevel.h"

// The following is support for Pd polling functions:
typedef void (*t_fdpollfn)(void *ptr, int fd);
extern "C" void sys_addpollfn(int fd, t_fdpollfn fn, void *ptr);
extern "C" void sys_rmpollfn(int fd);


typedef struct _lo_address {
        char            *host;
        int              socket;
        char            *port;
        int              protocol;
        struct addrinfo *ai;
        int              errnum;
        const char      *errstr;
        int              ttl;
} *internal_lo_address;


// The Pd t_class instance, and related object struct:
static t_class *OSCtx_class;

typedef struct _OSCtx
{
	t_object x_obj;
	lo_server srvr;
	t_symbol *proto;
	t_symbol *serverType;
	lo_address addr;
	
	bool bundleOpen;
	lo_bundle bundle;
	
	t_clock *x_clock; // Pd clock used for periodic polling

	t_outlet *outlet_status;
	t_outlet *outlet_recv;

} t_OSCtx;



static void OSCtx_liblo_error(int num, const char *msg, const char *path)
{
	post("OSCtx (liblo) error %d in path %s: %s", num, path, msg);
}


std::string OSCtx_getIPaddress()
{

	struct ifaddrs *interfaceArray = NULL, *tempIfAddr = NULL;
	void *tempAddrPtr = NULL;
	int rc = 0;
	char addressOutputBuffer[INET6_ADDRSTRLEN];

	//char *IPaddress;
	std::string IPaddress;

	
	rc = getifaddrs(&interfaceArray);  /* retrieve the current interfaces */
	if (rc == 0)
	{    
		for (tempIfAddr = interfaceArray; tempIfAddr != NULL; tempIfAddr = tempIfAddr->ifa_next)
		{
			if (tempIfAddr->ifa_addr->sa_family == AF_INET) // check if it is IP4
			{
				tempAddrPtr = &((struct sockaddr_in *)tempIfAddr->ifa_addr)->sin_addr;

				if (std::string(tempIfAddr->ifa_name).find("lo") == std::string::npos) // skip loopback
				{
					IPaddress = inet_ntop(tempIfAddr->ifa_addr->sa_family, tempAddrPtr, addressOutputBuffer, sizeof(addressOutputBuffer));
					
					//printf("Internet Address: [%s] %s \n", tempIfAddr->ifa_name, IPaddress.c_str());
				
					// TODO: for now we just return the first address found. Eventually, we could ask for a specific address (eg, "eth0" vs "eth1")
					break;					
				}
			}
		}
	}
	return IPaddress;
}

static void OSCtx_poll(lo_server *s, int sockfd)
{
	// This function checks if liblo has any pending messages and
	// dispatches the OSCtx_liblo_callback() method if one is found.

	lo_server_recv_noblock(s, 0); // 0 timeout
}

static int OSCtx_liblo_callback(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	t_OSCtx *x = (t_OSCtx*) user_data;

	/*
	printf("************ OSCtx got message on outgoing port (%s): %s\n",lo_server_get_url(x->srvr), (char*)path);
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
	*/

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
	outlet_anything(x->outlet_recv, gensym((char*)path), argc, outAtoms);

}

static void OSCtx_timed_poll(t_OSCtx *x)
{
	// This function checks if liblo has any pending messages and
	// dispatches a matching method if one is found.

	int recv = 0;

	if (x->srvr)
	{
		// The 2nd argument is the amount of time to wait. By passing 0
		// the function will return immediately.
		recv = lo_server_recv_noblock(x->srvr, 0);

		//clock_delay(x->x_clock, 5);


		// If we've just received something, then assume there is more, and set the
		// delay to something very small. If nothing was received, assume we can
		// wait a little longer:

		if (recv) clock_delay(x->x_clock, 0);
		else clock_delay(x->x_clock, 5);
	}

}


static void OSCtx_output_status(t_OSCtx *x)
{

	/* OLD (just output 0 or 1):
	if (x->srvr) outlet_float(x->outlet_status, 1.0);
	else outlet_float(x->outlet_status, 0.0);
	*/

	if (x->srvr) outlet_float(x->outlet_status, lo_server_get_port(x->srvr));
	else outlet_float(x->outlet_status, 0.0);

}

static void OSCtx_info(t_OSCtx *x)
{
	post("\nOSCtx is transmitting in mode: %s", x->serverType->s_name);
	if (x->addr) post("   to address:  %s", lo_address_get_url(x->addr));
	else post("   destination address:  NONE DEFINED!");
	post("   from socket: %s", lo_server_get_url(x->srvr));

	if (x->bundleOpen) post("   current bundle size: %d", lo_bundle_length(x->bundle));
	else post("   current bundle size: NONE");
}

static void OSCtx_disconnect(t_OSCtx *x)
{
	// remember to remove polling function:
	//if (x->srvr) sys_rmpollfn(lo_server_get_socket_fd(x->srvr));

	lo_address_free(x->addr);
	lo_server_free(x->srvr);

	x->addr = 0;
	x->srvr = 0;

	OSCtx_output_status(x);
}



static void OSCtx_setAddress(t_OSCtx *x, t_symbol *addr, t_floatarg port)
{
	OSCtx_disconnect(x);
	
	// parse the port:
	char portString[10]; // note: lo_server_new() expects the port as a string
	sprintf(portString, "%d", (int)port);
	
	if (x->proto == gensym("tcp"))
		x->addr = lo_address_new_with_proto( LO_TCP, addr->s_name, portString );
	else 
		x->addr = lo_address_new( addr->s_name, portString );

	
	if (!x->addr)
	{
		post("OSCtx ERROR: could not create socket on %s:%d. Perhaps the port is not available?", addr->s_name, (int)port);
		return;
	}

	/*
	if (x->proto == gensym("tcp"))
	{
		// there is no method to create a TCP address, so we do it manually:
		((internal_lo_address)x->addr)->protocol = LO_TCP;
	}
	*/
	
	// check to see if the address is destined for a broadcast or multicast address:
	x->serverType = gensym("unicast");
	try {
		std::string s = std::string(addr->s_name);
		int i = atoi(s.substr(0,s.find(".")).c_str());
		if (s.substr(s.rfind(".")+1) == "255")
			x->serverType = gensym("broadcast");
		else if ((i>=224) && (i<=239))
			x->serverType = gensym("multicast");		
	}
	catch (int i)
	{
		// do nothing
	}



	// create the appropriate lo_server:
	if ((x->serverType==gensym("unicast")) && (x->proto==gensym("udp")))
	{
		x->srvr = lo_server_new_with_proto(NULL, LO_UDP, OSCtx_liblo_error);
		//x->srvr = lo_server_new_with_proto(lo_address_get_port(x->addr), LO_UDP, OSCtx_liblo_error);
	}
		
	else if ((x->serverType==gensym("unicast")) && (x->proto==gensym("tcp")))
	{
		x->srvr = lo_server_new_with_proto(NULL, LO_TCP, OSCtx_liblo_error);
	}
	
	else if (x->serverType==gensym("broadcast"))
	{
		x->srvr = lo_server_new(NULL, OSCtx_liblo_error);
		if (x->srvr)
		{
			int sock = lo_server_get_socket_fd(x->srvr);
			int sockopt = 1;
			if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &sockopt, sizeof(sockopt)))
				post("OSCtx ERROR: could not set SO_BROADCAST flag");
			//if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &sockopt, sizeof(sockopt)))
			//	post("OSCtx ERROR: could not set SO_REUSEPORT flag");
		}
	}
		
	else if (x->serverType==gensym("multicast"))
	{
		x->srvr = lo_server_new_multicast(addr->s_name, NULL, OSCtx_liblo_error);
	}

	
	if (!x->srvr)
	{
		post("OSCtx ERROR: could not create socket on %s:%d. Perhaps the port is not available?", addr->s_name, (int)port);
	}
	

	// We'll also create a receiver for this liblo server, because for TCP and
	// bidirectional UDP scenarios, we will want to receive on the same port as
	// we send on. So, here we create a polling function (polled by the pd
	// scheduler), and add a callback for any message that just outputs to the
	// right outlet:

	lo_server_add_method(x->srvr, NULL, NULL, OSCtx_liblo_callback, x);

	/*
	int lo_fd = lo_server_get_socket_fd(x->srvr);
	sys_addpollfn(lo_fd, (t_fdpollfn)OSCtx_poll, x->srvr);
	*/
	x->x_clock = clock_new(x, (t_method)OSCtx_timed_poll);
	OSCtx_timed_poll(x); // start polling


	// finally, set the status outlet:
	OSCtx_output_status(x);
}



static void OSCtx_send(t_OSCtx *x, t_symbol *s, int argc, t_atom *argv)
{
	t_symbol *OSCpath;
	
	if ((x->srvr) && (x->addr))
	{

		if (s->s_name[0] == '[')
		{
			x->bundleOpen = true;
			x->bundle = lo_bundle_new(LO_TT_IMMEDIATE);
			return;
		}
		
		else if (s->s_name[0] == ']')
		{
			lo_send_bundle_from(x->addr, x->srvr, x->bundle);
			x->bundleOpen = false;
			return;
		}
		
		if ((s == &s_list) && (argv->a_type == A_SYMBOL))
		{
			OSCpath = atom_getsymbol(argv);
			argv++;
			argc--;
		}
		
		// check that s is a valid OSC path:
		else if ((s->s_name)[0] == '/')
		{
			OSCpath = s;
		}
		
		else
		{
			post("OSCtx ERROR: messages must be preceded by an OSC-style path (eg, /audio/channels/2/gain)");
			return;
		}
	
		lo_message msg = lo_message_new();
		
		for (int i=0; i<argc; i++)
		{			
			if ((argv+i)->a_type == A_FLOAT) lo_message_add_float(msg, (float)atom_getfloat(&argv[i]));
			else lo_message_add_string(msg, (char*)atom_getsymbol(argv+i)->s_name);
		}
		
	
#ifdef OSC_DEBUG
		lo_arg **loarg = lo_message_get_argv(msg);
		char *lotypes = lo_message_get_types(msg);
			
		printf("************ OSCtx is sending: %s \n", (char*)s->s_name);
		for (int i=0; i<lo_message_get_argc(msg); i++) {
			printf("arg %d '%c' ", i, lotypes[i]);
	    	lo_arg_pp((lo_type) lotypes[i], loarg[i]);
	    	printf("\n");
		}
		printf("\n");
		fflush(stdout);
#endif
		

		//============


		if (x->bundleOpen)
		{
			lo_bundle_add_message(x->bundle, (char*)OSCpath->s_name, msg);
		} else {

			int ret;

			if (x->proto==gensym("tcp"))
			{
				// liblo has some bug where you can't send TCP messages using
				// the lo_send_message_from, so even though we created a server,
				// we can't use it and send the message in the slightly less
				// efficient way:

				ret = lo_send_message(x->addr, (char*)OSCpath->s_name, msg);
			}
			else
			{
				ret = lo_send_message_from(x->addr, x->srvr, (char*)OSCpath->s_name, msg);
			}

			if (ret < 0)
			{
				startpost("Warning: OSCtx could not send message: %s", (char*)OSCpath->s_name);
				postatom(argc, argv);
				endpost();
			}

			lo_message_free(msg);
		}
	}
		
	else post("OSCtx ERROR: No destination has been defined using 'setAddress' method.");
	
}

static void OSCtx_sendpoint(t_OSCtx *x, t_symbol *OSCpath, t_floatarg ptX, t_floatarg ptY)
{
	if (x->srvr)
	{

		float blobdata[2] = {ptX, ptY};
		//int blobdata[2] = {(int)ptX, (int)ptY};
		lo_blob blob = lo_blob_new(sizeof(blobdata), blobdata);

		lo_message msg = lo_message_new();
		lo_message_add_string(msg, "Bus 1 Position");
		lo_message_add_blob(msg, blob);


		printf("sending blob:");
		lo_arg_pp(LO_BLOB, blob);
		printf("\n");


		int ret;
		if (x->proto==gensym("tcp"))
			ret = lo_send_message(x->addr, (char*)OSCpath->s_name, msg);
		else
			ret = lo_send_message_from(x->addr, x->srvr, (char*)OSCpath->s_name, msg);

		if (ret<0) post("Warning: OSCtx could not send point: %s %f %f", (char*)OSCpath->s_name, ptX, ptY);
	}
}

static void OSCtx_senddelay(t_OSCtx *x, t_symbol *OSCpath, t_floatarg del)
{
	if (x->srvr)
	{
		lo_message msg = lo_message_new();
		lo_message_add_string(msg, "Bus 1 Delay");
		lo_message_add_int64(msg, (long int) del);

		printf("sending delay: %f", del);
		lo_message_pp(msg);
		printf("\n");

		int ret;
		if (x->proto==gensym("tcp"))
			ret = lo_send_message(x->addr, (char*)OSCpath->s_name, msg);
		else
			ret = lo_send_message_from(x->addr, x->srvr, (char*)OSCpath->s_name, msg);

		if (ret<0) post("Warning: OSCtx could not send delay: %s %f", (char*)OSCpath->s_name, del);
	}
}

static void *OSCtx_new(t_symbol *s, int argc, t_atom *argv)
{
	t_OSCtx *x = (t_OSCtx *) pd_new(OSCtx_class);

	x->bundleOpen = false;
	x->proto = gensym("udp"); // default
	x->serverType = gensym("unicast");
	
	// The user can provide a the serverType as an argument:
	if (argc)
	{
		if (argv->a_type == A_SYMBOL) x->proto = atom_getsymbol(argv);
	}
	
	
	if ((x->proto == gensym("udp")) ||
		(x->proto == gensym("tcp")))
	{
		// create outlets:
		x->outlet_status = outlet_new(&x->x_obj, 0);
		x->outlet_recv = outlet_new(&x->x_obj, &s_list);

		return(x);
	}
	else
	{
		error("OSCtx: Could not create OSCtx; possible arguments are: udp or tcp");
		return(NULL);
	}
}


static void OSCtx_free(t_OSCtx *x)
{
	// start by stopping the clock:
	//clock_free(x->x_clock);

	if (x->addr) lo_address_free(x->addr);
	if (x->srvr) lo_server_free(x->srvr);
	x->addr = 0;
	x->srvr = 0;
}


extern "C" void OSCtx_setup(void)
{
	OSCtx_class = class_new(gensym("OSCtx"), (t_newmethod)OSCtx_new, (t_method)OSCtx_free, sizeof(t_OSCtx), CLASS_DEFAULT, A_GIMME, 0);
	class_addmethod(OSCtx_class, (t_method) OSCtx_setAddress, gensym("setAddress"), A_SYMBOL, A_DEFFLOAT, 0); 
	class_addmethod(OSCtx_class, (t_method) OSCtx_disconnect, gensym("disconnect"), A_DEFFLOAT, 0); 
	class_addmethod(OSCtx_class, (t_method) OSCtx_sendpoint, gensym("sendpoint"), A_SYMBOL, A_DEFFLOAT, A_DEFFLOAT, 0);
	//class_addmethod(OSCtx_class, (t_method) OSCtx_send_with_format, gensym("send"), A_GIMME, 0);
	class_addmethod(OSCtx_class, (t_method) OSCtx_senddelay, gensym("send"), A_SYMBOL, A_DEFFLOAT, 0);
	class_addanything(OSCtx_class, (t_method) OSCtx_send); 
	class_addbang(OSCtx_class, (t_method) OSCtx_info); 
	class_sethelpsymbol(OSCtx_class, gensym("help-OSCrxtx"));
	//post("OSCtx polling enabled");
}
