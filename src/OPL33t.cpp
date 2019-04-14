#include "OPL33t.hpp"

Plugin *plugin;


void init(Plugin *p) {
	plugin = p;
	p->slug = TOSTRING(SLUG);
	p->version = TOSTRING(VERSION);
	p->addModel(modelPlayer);
	p->addModel(model6x4);
}
