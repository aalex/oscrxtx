#include "m_pd.h"
#include <string>
#include <sstream>

using namespace std;



static t_class *OSCsymbol_class;

typedef struct _OSCsymbol
{
	t_object x_obj;
	t_symbol *OSCpath;
} t_OSCsymbol;


static t_symbol *makeOSCsymbol(t_symbol *s, int argc, t_atom *argv)
{
	ostringstream newPath("");
	
	if ((s!=&s_list) && (s!=&s_symbol) && (s!=&s_float))
	{
		if (s->s_name[0] == '/') newPath << s->s_name;
		else newPath << "/" << s->s_name;
	}
	
	for (int i=0; i<argc; i++)
	{
		if (argv[i].a_type == A_SYMBOL)
		{
			if ((argv[i].a_w.w_symbol->s_name)[0]=='/') newPath << (char*) argv[i].a_w.w_symbol->s_name;
			else newPath << "/" << (char*) argv[i].a_w.w_symbol->s_name;
		} else if (argv[i].a_type == A_FLOAT)
		{
			newPath << "/" << (float)atom_getfloat(&argv[i]);
		}
	}
	return gensym(newPath.str().c_str());
}


static void OSCsymbol_bang(t_OSCsymbol *x)
{
	outlet_symbol(x->x_obj.ob_outlet, x->OSCpath);
}

static void OSCsymbol_anything(t_OSCsymbol *x, t_symbol *s, int argc, t_atom *argv)
{
	x->OSCpath = makeOSCsymbol(s, argc, argv);
	OSCsymbol_bang(x);
}


static void *OSCsymbol_new(t_symbol *s, int argc, t_atom *argv)
{
	t_OSCsymbol *x = (t_OSCsymbol *)pd_new(OSCsymbol_class);

	if (argc)
		x->OSCpath = makeOSCsymbol(s, argc, argv);
	else x->OSCpath = gensym("");

	outlet_new(&x->x_obj, 0);
	
	return (x);
}


extern "C" void OSCsymbol_setup(void)
{
	OSCsymbol_class = class_new(gensym("OSCsymbol"), (t_newmethod)OSCsymbol_new, 0, sizeof(t_OSCsymbol), 0, A_GIMME, 0);
	class_addbang(OSCsymbol_class, (t_method) OSCsymbol_bang);
	class_addanything(OSCsymbol_class, (t_method) OSCsymbol_anything);
}
