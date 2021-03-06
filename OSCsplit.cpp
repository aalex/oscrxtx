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
#include <string>
#include "m_pd.h"

using namespace std;
#define UNUSED(x) ((void) (x))
	
static t_class *OSCsplit_class;

typedef struct _OSCsplit
{
	t_object x_obj;

	t_outlet *leftOutlet;
	t_outlet *rightOutlet;
	
} t_OSCsplit;


static t_atom *atomsFromString(string theString, int &count)
{
	t_atom *atomList;
	t_binbuf *b;
	if (theString.size())
	{
		b = binbuf_new();
		binbuf_text(b, (char*) theString.c_str(), theString.size());
		count = binbuf_getnatom(b);
		atomList = (t_atom*) copybytes(binbuf_getvec(b), sizeof(*binbuf_getvec(b)) * count);
		binbuf_free(b);
	}
	if (count) return atomList;
	else return NULL;
}

static void OSCsplit_anything(t_OSCsplit *x, t_symbol *s, int argc, t_atom *argv)
{
	string OSCpath;
	
	if (((s==&s_list)||(s==&s_symbol)) && (argc) && (argv[0].a_type == A_SYMBOL) && ((argv[0].a_w.w_symbol->s_name)[0]=='/'))
	{
		OSCpath = argv[0].a_w.w_symbol->s_name;
		argc--;
		if (argc) argv = argv+1;
	}
	
	else if (s->s_name[0] == '/')
	{		
		OSCpath = s->s_name;
	}
	
	else if (argc)
	{
		// if there is no OSC pattern to match, then just send the atoms out the
		// rightmost outlet
		outlet_anything(x->rightOutlet, &s_list, argc, argv);
		return;
	}
	
	else return;
	
	// okay - we have an OSC message
	
	/*
	int numTokens = count(OSCpath.begin(), OSCpath.end(), '/');
	t_atom OSCatoms[numTokens];
	size_t index;
	
	OSCpath = OSCpath.substr(1); // ignore first slash
	for (int i=0; i<numTokens; i++)
	{
		index = OSCpath.find("/");
		if (index==string::npos)
		{
			
			SETSYMBOL(OSCatoms+i, gensym(OSCpath.c_str()));
		} else {
			SETSYMBOL(OSCatoms+i, gensym((OSCpath.substr(0,index)).c_str()));
			OSCpath = OSCpath.substr(index+1);
		}
	}
	*/
	
	size_t index;
	
	OSCpath = OSCpath.substr(1); // ignore first slash
	while ((index = OSCpath.find("/")) != string::npos)
	{
		// replace remaining slashes with spaces
		OSCpath.replace(index, 1, " ");
	}
	int numTokens = 0;
	t_atom *OSCatoms = atomsFromString(OSCpath, numTokens);
			
	if (numTokens)
	{
		if (OSCatoms[0].a_type==A_SYMBOL) outlet_anything(x->rightOutlet, OSCatoms[0].a_w.w_symbol, numTokens-1, OSCatoms+1);
		else outlet_anything(x->rightOutlet, &s_list, numTokens, OSCatoms);
	}
	if (argc)
	{
		if (argv[0].a_type==A_SYMBOL) outlet_anything(x->leftOutlet, argv[0].a_w.w_symbol, argc-1, argv+1);
		else outlet_anything(x->leftOutlet, &s_list, argc, argv);
	}
}

static void *OSCsplit_new(t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);
    UNUSED(argc);
    UNUSED(argv);
	t_OSCsplit *x = (t_OSCsplit *)pd_new(OSCsplit_class);

	x->leftOutlet = outlet_new(&x->x_obj, &s_list);
	x->rightOutlet = outlet_new(&x->x_obj, &s_list);	
	
	return (x);
}

void OSCsplit_free(t_OSCsplit *x)
{
    UNUSED(x);
}

extern "C" void OSCsplit_setup(void)
{
	OSCsplit_class = class_new(gensym("OSCsplit"), (t_newmethod)OSCsplit_new, (t_method)OSCsplit_free, sizeof(t_OSCsplit), 0, A_GIMME, 0);
	class_addanything(OSCsplit_class, (t_method) OSCsplit_anything);
}
