#include <string>
#include <algorithm>

#include "m_pd.h"
#include "lo/lo.h"
#include "lo/lo_lowlevel.h"

#define UNUSED(x) ((void) (x))

using namespace std;

	
static t_class *OSCparse_class;

typedef struct _OSCparse
{
	t_object x_obj;
	
	t_symbol  *OSCpath;
	int numSlashes;

	t_outlet *leftOutlet;
	t_outlet *rightOutlet;
	
} t_OSCparse;


static void OSCparse_compare(t_OSCparse *x, t_symbol *s, int argc, t_atom *argv)
{
	string fullPathToMatch;
	
	if ((s==&s_list) && (argc) && (argv[0].a_type == A_SYMBOL) && ((argv[0].a_w.w_symbol->s_name)[0]=='/'))
	{
		fullPathToMatch = argv[0].a_w.w_symbol->s_name;
		argc--;
		if (argc) argv = argv+1;
	}
	
	else if (s->s_name[0] == '/')
	{		
		fullPathToMatch = s->s_name;
	}
	
	else if (argc)
	{
		// if there is no OSC pattern to match, then just send the atoms out the
		// rightmost outlet
		outlet_anything(x->rightOutlet, s, argc, argv);
		return;
	}
	
	else return;
	
	// okay - we have something to match
	
	int numSlashesToMatch = count(fullPathToMatch.begin(), fullPathToMatch.end(), '/');

	size_t index;
	string pathToMatch = fullPathToMatch;
	string theRemainder;
		
	if (numSlashesToMatch > x->numSlashes)
	{
		// shave off the rest of this path before comparing
		theRemainder = pathToMatch.substr(1);
		pathToMatch = "";
		for (int i=1; i<=x->numSlashes; i++)
		{
			//post("pathToMatch=%s, theRemainder=%s", pathToMatch.c_str(), theRemainder.c_str());
			index = theRemainder.find_first_of("/");
			pathToMatch += "/" + theRemainder.substr(0,index);
			theRemainder = theRemainder.substr(index+1);
		}
		theRemainder = "/" + theRemainder;
	}
		
	else if (x->numSlashes == numSlashesToMatch)
	{
		theRemainder = "";
	}
		
	else
	{
		// this path is shorter than the pathToMatch, thus no match!
		// output the list on the right:
		if ((argc) && (argv->a_type==A_SYMBOL)) outlet_anything(x->rightOutlet, argv[0].a_w.w_symbol, argc-1, argv+1);
		else outlet_anything(x->rightOutlet, &s_list, argc, argv);
		return;
	}
		
			
	if (lo_pattern_match(pathToMatch.c_str(), x->OSCpath->s_name))
	{
		if (theRemainder.size())
		{
			outlet_anything(x->leftOutlet, gensym(theRemainder.c_str()), argc, argv);
		} else {
			if ((argc) && (argv->a_type==A_SYMBOL)) outlet_anything(x->leftOutlet, argv[0].a_w.w_symbol, argc-1, argv+1);
			else outlet_anything(x->leftOutlet, &s_list, argc, argv);
		}
	} else {
		// no match:
		outlet_anything(x->rightOutlet, gensym(fullPathToMatch.c_str()), argc, argv);
	}
	
}

static void OSCparse_debug(t_OSCparse *x)
{
	post("OSCparse selector: %s (%d slashes)", x->OSCpath->s_name, x->numSlashes);
	
}

static void OSCparse_set(t_OSCparse *x, t_symbol *s)
{
	// if symbol doesn't start with a slash, add one to the beginning:
	if (s->s_name[0] == '/') x->OSCpath = s;
	else x->OSCpath = gensym(("/" + string(s->s_name)).c_str());
	
	string str = string(x->OSCpath->s_name);
	x->numSlashes = count(str.begin(), str.end(), '/');
}

static void *OSCparse_new(t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);
	t_OSCparse *x = (t_OSCparse *)pd_new(OSCparse_class);

	if ((argc==1) && (argv[0].a_type == A_SYMBOL) && (argv[0].a_w.w_symbol->s_name[0] == '/'))
	{
		OSCparse_set(x, argv[0].a_w.w_symbol);
	}	
	else
	{
		post("ERROR: OSCparse requires one OSC-style symbolic argument (must begin with a forward-slash)");
		return NULL;
	}
	
	
	// create a right inlet to dynamically change the OSCpath:
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_symbol, gensym("set"));
	
	x->leftOutlet = outlet_new(&x->x_obj, &s_list);
	x->rightOutlet = outlet_new(&x->x_obj, &s_list);
	
	
	return (x);
}

void OSCparse_free(t_OSCparse *x)
{
    UNUSED(x);
}

extern "C" void OSCparse_setup(void)
{
	OSCparse_class = class_new(gensym("OSCparse"), (t_newmethod)OSCparse_new, (t_method)OSCparse_free, sizeof(t_OSCparse), 0, A_GIMME, 0);
	class_addanything(OSCparse_class, (t_method) OSCparse_compare); 
	class_addmethod(OSCparse_class, (t_method)OSCparse_set, gensym("set"), A_SYMBOL, 0);
	class_addmethod(OSCparse_class, (t_method)OSCparse_debug, gensym("debug"), A_DEFFLOAT, 0);
}
