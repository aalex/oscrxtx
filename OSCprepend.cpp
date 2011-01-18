/* 
 * This file is part of OSCrxtx.
 * 
 * Copyright (c) 2010 Mike Wozniewski <mike@mikewoz.com>
 * 
 * OSCrxtx is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * OSCrxtx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with OSCrxtx.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "m_pd.h"
#include <string>

using namespace std;

#define UNUSED(x) ((void) (x))

static t_class *OSCprepend_class;

typedef struct _OSCprepend
{
	t_object x_obj;
	t_symbol *x_prefix;
} t_OSCprepend;


static void OSCprepend_anything(t_OSCprepend *x, t_symbol *s, int argc, t_atom *argv)
{
	//int offset = 0;
	t_symbol *finalPrefix = x->x_prefix;
	t_atom *atomList = argv; // default 
	
	
	if (s->s_name[0] == '/')
	{
		// if the selector starts with a slash, then we append it to the prefix
		finalPrefix = gensym((string(x->x_prefix->s_name) + string(s->s_name)).c_str());
		
		// and we keep the atom list exactly as is:
		atomList = argv;
	}
	
	else if ((s==&s_list) && (argc) && (argv[0].a_type == A_SYMBOL) && ((argv[0].a_w.w_symbol->s_name)[0]=='/'))
	{
		// if the 'list' selector is the first arg is a symbol that starts with
		// a slash, then append it to the prefix:
		finalPrefix = gensym((string(x->x_prefix->s_name) + string(argv[0].a_w.w_symbol->s_name)).c_str());
		atomList = argv+1;
		argc--;
	}
	
	else if ((s==&s_symbol) && (argc) && ((argv[0].a_w.w_symbol->s_name)[0]=='/'))
	{
		finalPrefix = gensym((string(x->x_prefix->s_name) + string(argv[0].a_w.w_symbol->s_name)).c_str());
		argc=0;
		argv=NULL;
	}
	
	else if ( ((s==&s_symbol)||(s==&s_list)) && (argc) )
	{
		// if there is no additional prefix to prepend, then just send the atom
		// list as is:
		atomList = argv;	
	}
	
	else
	{
		// otherwise, s is a symbolic argument that must be prepended to the 
		// list of atoms
		atomList = (t_atom*) getbytes((argc+1)*sizeof(t_atom));
		SETSYMBOL(atomList, s);
		// copy the args:
		for (int i=0; i<argc; i++)
		{
			(atomList+i+1)->a_type = (argv+i)->a_type;
			(atomList+i+1)->a_w = (argv+i)->a_w;
		}
		argc++;
	}
	
	if (finalPrefix == gensym(""))
	{
		post("OSCprepend ERROR: There needs to be an OSC prefix (with a slash) preceding the message");
		return;
	}
	
	outlet_anything(x->x_obj.ob_outlet, finalPrefix, argc, atomList);

}

static void OSCprepend_set(t_OSCprepend *x, t_symbol *s)
{
	// if symbol doesn't start with a slash, add one to the beginning:
	if (s->s_name[0] == '/') x->x_prefix = s;
	else x->x_prefix = gensym(("/" + string(s->s_name)).c_str());
}

static void *OSCprepend_new(t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);
	t_OSCprepend *x = (t_OSCprepend *)pd_new(OSCprepend_class);
	x->x_prefix = gensym("");

	if ((argc==1) && (argv[0].a_type == A_SYMBOL))
		x->x_prefix = argv[0].a_w.w_symbol;
	else if (argc>1)
		post("ERROR: OSCprepend takes one (optional) symbol argument");
	
	// create a right inlet to dynamically change the OSCpath:
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_symbol, gensym("set"));
	
	outlet_new(&x->x_obj, 0);
	
	return (x);
}


extern "C" void OSCprepend_setup(void)
{
	OSCprepend_class = class_new(gensym("OSCprepend"), (t_newmethod)OSCprepend_new, 0, sizeof(t_OSCprepend), 0, A_GIMME, 0);
	class_addanything(OSCprepend_class, (t_method) OSCprepend_anything);
	class_addmethod(OSCprepend_class, (t_method)OSCprepend_set, gensym("set"), A_SYMBOL, 0);
}
