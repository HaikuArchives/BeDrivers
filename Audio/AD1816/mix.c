// mix.c
//
// Copyright (C) 1999 Jens Winkler <jwin@gmx.net>
//
// AD1816(A) mixer functions
//
//  - this code is heavily based on the
//    R4 sonic_vibes mix sample code
//
//	- as nobody currently uses this api,
//	  this code is totally untested.
//	  Especially it lacks consistancy
//	  in case of using both the old
//	  and the new api simultaneously
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
#include <Errors.h>

#include "audio.h"
#include "adi.h"

extern isa_module_info *isa_bus;

static int32 mix_open_count = 0;

static status_t mixer_open(const char *, uint32, void **);
static status_t mixer_close(void *);
static status_t mixer_free(void *);
static status_t mixer_control(void *, uint32, void *, size_t);
static status_t mixer_read(void *, off_t, void *, size_t *);
static status_t mixer_write(void *, off_t, const void *, size_t *);

device_hooks
adi_mix_hooks =
{
  mixer_open,
  mixer_close,
  mixer_free,
  mixer_control,
  mixer_read,
  mixer_write
};

typedef struct
{
  int	 selector;  // identifier
  int    port;      // indirect register address
  float  div;       // dB per step       
  float  sub;       // max gain in dB
  int    minval;    // min register value
  int    maxval;    // max register value
  int    mask;      // significant register bits
  int    mutemask;  // mute bit
  int    leftshift; // position of byte in register
} mixer_info;

// mute is special -- when it's 0x01, it means enable...
static mixer_info
the_mixers[] =
{
	{B_AUDIO_MIX_ADC_LEFT,			20,  1.5, 22.5, 0, 15, 0x0f, 0x00, 8},
	{B_AUDIO_MIX_ADC_RIGHT,			20,  1.5, 22.5, 0, 15, 0x0f, 0x00, 0},
	{B_AUDIO_MIX_DAC_LEFT,			 4, -1.5,  0.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_DAC_RIGHT,			 4, -1.5,  0.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_LINE_IN_LEFT,		18, -1.5, 12.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_LINE_IN_RIGHT,		18, -1.5, 12.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_CD_LEFT,			15, -1.5, 12.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_CD_RIGHT,			15, -1.5, 12.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_VIDEO_LEFT,		17, -1.5, 12.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_VIDEO_RIGHT,		17, -1.5, 12.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_SYNTH_LEFT,		16, -1.5, 12.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_SYNTH_RIGHT,		16, -1.5, 12.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_AUX_LEFT,			 5, -1.5,  0.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_AUX_RIGHT,			 5, -1.5,  0.0, 0, 31, 0x1f, 0x80, 0},
	{B_AUDIO_MIX_MIC,				19, -1.5, 12.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_LINE_OUT_LEFT,		14, -1.5,  0.0, 0, 31, 0x1f, 0x80, 8},
	{B_AUDIO_MIX_LINE_OUT_RIGHT,	14, -1.5,  0.0, 0, 31, 0x1f, 0x80, 0},
};

#define N_MIXERS (sizeof(the_mixers)/sizeof(the_mixers[0]))

static int8
map_mixer(uint8 selector)
{
  int8 i;

  for (i=0; i<N_MIXERS; i++)
    if (the_mixers[i].selector == selector)
	  return i;

  return -1;
}


static status_t
mixer_open(const char *name, uint32 flags, void **cookie)
{
  DBG("AD1816: mixer_open()\n");

//	if (strcmp(name, MIX_NAME))
//		return ENODEV;

  atomic_add(&mix_open_count, 1);

  return B_OK;
}


static status_t
mixer_close(void *cookie)
{
  atomic_add(&mix_open_count, -1);
  return B_OK;
}


static status_t
mixer_free(void *cookie)
{
  DBG("AD1816: mixer_free()\n");

  if (0 != mix_open_count)
    dprintf("AD1816: mixer open_count is bad in mixer_free()!\n");

  return B_OK;  // already done in close
}


static int
get_mixer_value(wss_info *info, audio_level *lev)
{
  int8 ix = map_mixer(lev->selector);
  uint16 mask, val;

  if (0 > ix)
    return B_BAD_VALUE;

  mask = (the_mixers[ix].mask|the_mixers[ix].mutemask) << the_mixers[ix].leftshift;
  val = get_ireg_bits(the_mixers[ix].port, mask);
  lev->flags = 0;

  val >>= the_mixers[ix].leftshift;

  if (0x01 == the_mixers[ix].mutemask)
    {
      if (!(val & 0x01))
	    lev->flags |= B_AUDIO_LEVEL_MUTED;
    }
  else if (val & the_mixers[ix].mutemask)
    lev->flags |= B_AUDIO_LEVEL_MUTED;

  lev->value = ((float)val)*the_mixers[ix].div+the_mixers[ix].sub;

  return B_OK;
}


static int
gather_info(wss_info *info, audio_level *data, int count)
{
  uint32 ix;

  for (ix=0; ix<count; ix++)
    if (B_OK > get_mixer_value(info, &data[ix]))
      break;

  return ix;
}


static status_t
set_mixer_value(wss_info *info, audio_level *lev)
{
  int8 selector = map_mixer(lev->selector);
  uint16 mask, value;

  if (0 > selector)
    return EINVAL;

  value = (lev->value-the_mixers[selector].sub)/the_mixers[selector].div;

  if (value < the_mixers[selector].minval)
    value = the_mixers[selector].minval;

  if (value > the_mixers[selector].maxval)
    value = the_mixers[selector].maxval;

  if (the_mixers[selector].mutemask)
    {
      if (the_mixers[selector].mutemask == 0x01)
        {
          if (!(lev->flags & B_AUDIO_LEVEL_MUTED))
            value |= the_mixers[selector].mutemask;
        }
      else
        {
          if (lev->flags & B_AUDIO_LEVEL_MUTED)
            value |= the_mixers[selector].mutemask;
        }
    }

  value <<= the_mixers[selector].leftshift;
  mask = (the_mixers[selector].mask|the_mixers[selector].mutemask) << the_mixers[selector].leftshift;
  change_ireg_bits(the_mixers[selector].port, mask, value);

  return B_OK;
}


static int
disperse_info(wss_info *info, audio_level *data, int count)
{
  uint32 ix;

  for (ix=0; ix<count; ix++)
    if (B_OK > set_mixer_value(info, &data[ix]))
      break;

  return ix;
}


static status_t
mixer_control(void *cookie, uint32 iop, void *data, size_t len)
{
  wss_info *info = (wss_info *)cookie;

  //DBG("AD1816: mixer_control()\n");

  if (!data)
    return B_BAD_VALUE;

  switch (iop)
    {
      case B_MIXER_GET_VALUES:
	    ((audio_level_cmd *)data)->count =
         gather_info(info, ((audio_level_cmd *)data)->data, ((audio_level_cmd *)data)->count);
        return B_OK;

      case B_MIXER_SET_VALUES:
        ((audio_level_cmd *)data)->count =
         disperse_info(info, ((audio_level_cmd *)data)->data, ((audio_level_cmd *)data)->count);
        return B_OK;
    }
  return B_BAD_VALUE;
}


static status_t
mixer_read(void *cookie, off_t pos, void *data, size_t *nread)
{
  return EPERM;
}


static status_t
mixer_write(void *cookie, off_t pos, const void *data, size_t *nwritten)
{
  return EPERM;
}
