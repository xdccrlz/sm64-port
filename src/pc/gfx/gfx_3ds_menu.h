#ifndef GFX_3DS_MENU_H
#define GFX_3DS_MENU_H

#include <stdio.h>
#include <stdbool.h>

struct gfx_configuration
{
   bool useAA;
   bool useWide;
};

int display_menu(struct gfx_configuration *config);

#endif
