/* Dream SNES Image file */

#include "global.h"
RCSID("$Id: dsi.c,v 1.1 2000/10/08 22:39:32 per Exp $");

#include "image_machine.h"

#include "pike_macros.h"
#include "object.h"
#include "constants.h"
#include "module_support.h"
#include "interpret.h"
#include "object.h"
#include "svalue.h"
#include "threads.h"
#include "array.h"
#include "interpret.h"
#include "svalue.h"
#include "mapping.h"
#include "error.h"
#include "stralloc.h"
#include "builtin_functions.h"
#include "operators.h"
#include "dynamic_buffer.h"
#include "signal_handler.h"
#include "bignum.h"

#include "image.h"
#include "colortable.h"

/* MUST BE INCLUDED LAST */
#include "module_magic.h"

extern struct program *image_program;

void f__decode( INT32 args )
{
  int xs, ys, x, y;
  unsigned char *data, *dp;
  unsigned int len;
  struct object *i, *a;
  struct image *ip, *ap;
  rgb_group black = {0,0,0};
  if( sp[-args].type != T_STRING )
    error("Illegal argument 1 to Image.DSI._decode\n");
  data = (unsigned char *)sp[-args].u.string->str;
  len = (unsigned int )sp[-args].u.string->len;

  if( len < 10 ) error("Data too short\n");

  xs = data[0] | (data[1]<<8) | (data[2]<<16) | (data[3]<<24);
  ys = data[4] | (data[5]<<8) | (data[6]<<16) | (data[7]<<24);

  if( (xs * ys * 2) != (int)(len-8) )
    error("Not a DSI %d * %d + 8 != %d\n", xs,ys, len);

  push_int( xs );
  push_int( ys );
  push_int( 255 );
  push_int( 255 );
  push_int( 255 );
  a = clone_object( image_program, 5 );
  push_int( xs );
  push_int( ys );
  i = clone_object( image_program, 2 );
  ip = (struct image *)i->storage;
  ap = (struct image *)a->storage;

  dp = data+8;
  for( y = 0; y<ys; y++ )
    for( x = 0; x<xs; x++,dp+=2 )
    {
      unsigned short px = dp[0] | (dp[1]<<8);
      int r, g, b;
      rgb_group p;
      if( px == ((31<<11) | 31) )
        ap->img[ x + y*xs ] = black;
      else
      {
        r = ((px>>11) & 31);
        g = ((px>>5) & 63);
        b = ((px) & 31);
        p.r = (r*255)/31;
        p.g = (g*255)/63;
        p.b = (b*255)/31;
        ip->img[ x + y*xs ] = p;
      }
    }

  push_constant_text( "image" );
  push_object( i );
  push_constant_text( "alpha" );
  push_object( a );
  f_aggregate_mapping( 4 );
}

void f_decode( INT32 args )
{
  f__decode( args );
  push_constant_text( "image" );
  f_index( 2 );
}

void init_image_dsi()
{
  add_function("_decode", f__decode, "function(string:mapping)", 0);
  add_function("decode", f_decode, "function(string:object)", 0);
}


void exit_image_dsi()
{
}
