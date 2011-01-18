#include <string>
#include <vector>
#include <algorithm>

#include "m_pd.h"
#include "lo/lo.h"
#include "lo/lo_lowlevel.h"


using namespace std;

	
static t_class *OSCrouter_class;

typedef struct _OSCrouter
{
	t_object x_obj;
	
	std::vector<t_symbol*> selectors;
	std::vector<t_outlet*> outlets;
	
	t_outlet *unmatched_outlet;
	
} t_OSCrouter;


static void OSCrouter_route(t_OSCrouter *x, t_symbol *s, int argc, t_atom *argv)
{
	

	string pathToMatch;
	
	// if the list starts with the s_list selector:
	if ((s==&s_list) && (argc) && (argv[0].a_type == A_SYMBOL) && ((argv[0].a_w.w_symbol->s_name)[0]=='/'))
	{
		pathToMatch = argv[0].a_w.w_symbol->s_name;
		argc--;
		if (argc) argv = argv+1;
	}
	
	// otherwise, the path to match is the *s selector:
	else if (s->s_name[0] == '/')
	{		
		pathToMatch = s->s_name;
	}
	
	// otherwise assume no match:
	else
	{
		outlet_anything(x->unmatched_outlet, s, argc, argv);
		return;
	}
	
	// okay - we have something to match
	bool match = false;
	for (int i=0; i<x->selectors.size(); i++)
	{
		if (lo_pattern_match(x->selectors[i]->s_name, pathToMatch.c_str()))
		{
			if (argc)
			{
				if (argv[0].a_type==A_SYMBOL)
					outlet_anything(x->outlets[i], argv[0].a_w.w_symbol, argc-1, argv+1);
				else
					outlet_anything(x->outlets[i], &s_list, argc, argv);
			} else {
				outlet_bang(x->outlets[i]);
			}
			match = true;
		}
	}
	
	// if we got no match:
	if (!match)
	{
		outlet_anything(x->unmatched_outlet, s, argc, argv);
	}
}

static void *OSCrouter_new(t_symbol *s, int argc, t_atom *argv)
{
	t_OSCrouter *x = (t_OSCrouter *)pd_new(OSCrouter_class);

	for (int i=0; i<argc; i++)
	{
		if (argv[i].a_type == A_SYMBOL) 
		{
			if ((argv[i].a_w.w_symbol->s_name[0] != '/'))
			{
				post("ERROR: argument %d is not an OSC symbol", i);
				return NULL;
			}
			x->selectors.push_back(argv[i].a_w.w_symbol);
			x->outlets.push_back(outlet_new(&x->x_obj, &s_list));
		}
		
		else
		{
			post("ERROR: argument %d is not an OSC symbol", i);
			return NULL;
		}
	}

	// one extra outlet for unmatched messages:
	x->unmatched_outlet = outlet_new(&x->x_obj, &s_list);

	
	return (x);
}

void OSCrouter_free(t_OSCrouter *x)
{

}

extern "C" void OSCrouter_setup(void)
{
	OSCrouter_class = class_new(gensym("OSCrouter"), (t_newmethod)OSCrouter_new, (t_method)OSCrouter_free, sizeof(t_OSCrouter), 0, A_GIMME, 0);
	class_addanything(OSCrouter_class, (t_method) OSCrouter_route); 
	class_sethelpsymbol(OSCrouter_class, gensym("help-OSCrxtx"));
}
