// register.c
//
// Copyright (C) 1998 Jens Winkler <jwin@gmx.net>
//
// CODEC register access routines
//
// This program is  free  software;  you can redistribute it and/or modify it
// under the terms of the  GNU Lesser General Public License  as published by 
// the  Free Software Foundation;  either version 2.1 of the License,  or (at 
// your option) any later version.
//
// This  program  is  distributed in  the  hope that it will  be useful,  but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or  FITNESS FOR A  PARTICULAR PURPOSE.  See the  GNU Lesser General Public  
// License for more details.
//
// You should  have received  a copy of the GNU Lesser General Public License
// along with  this program;  if not, write to the  Free Software Foundation,
// Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <KernelExport.h>
#include <Drivers.h>

#include "audio.h"
#include "adi.h"

extern isa_module_info	*isa_bus;
extern wss_info			wss;


// gain register access
void
acquire_reg_access(void)
{
  if (0 < atomic_add(&wss.reg_cnt, 1))
    {
       acquire_sem_etc(wss.reg_lck, 1, B_CAN_INTERRUPT, 0);
    }
}


// release register access
void
release_reg_access(void)
{
  if (1 < atomic_add(&wss.reg_cnt, -1))
    {
      release_sem(wss.reg_lck);
    }
}


// get indirect register
uint32
get_ireg(uint8 index)
{
  uint32 ret;

  OUTB(wss.port, index);     // set indirect address
  ret  = INB(wss.port+2);    // get low byte
  ret |= INB(wss.port+3)<<8; // get high byte

  return ret;
}


// set indirect register
void
set_ireg(uint8 index, uint32 data)
{
  OUTB(wss.port, index);     // set indirect address
  OUTB(wss.port+2, data);    // set low byte
  OUTB(wss.port+3, data>>8); // set high byte
}


// set bits in codec indirect register
void
change_ireg_bits(uint8 index, uint32 mask, uint32 data)
{
  uint32 tmp = ~mask;

  data &= mask;              // mask data

  acquire_reg_access();      // acquire register access

  tmp &= get_ireg(index);    // read indirect register

  set_ireg(index, tmp|data); // write indirect register

  release_reg_access();      // release register access
}


// get bits from codec register
uint32
get_ireg_bits(uint8 index, uint32 mask)
{
  uint32 ret;

  acquire_reg_access();  // acquire register access

  ret = get_ireg(index); // read indirect register

  release_reg_access();  // release register access

  return mask & ret;     // mask value and return
}
