// mux.c
//
// Copyright (C) 1999 Jens Winkler <jwin@gmx.net>
//
// AD1816(A) multiplexer functions
//
//  - this code is heavily based on the
//    R4 sonic_vibes mux sample code
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

static int32 mux_open_count = 0;

static status_t mux_open(const char *, uint32, void **);
static status_t mux_close(void *);
static status_t mux_free(void *);
static status_t mux_control(void *, uint32, void *, size_t);
static status_t mux_read(void *, off_t, void *, size_t *);
static status_t mux_write(void *, off_t, const void *, size_t *);


device_hooks
adi_mux_hooks =
{
	mux_open,
	mux_close,
	mux_free,
	mux_control,
	mux_read,
	mux_write
};


// input src mapping
static const uint16
r4_adc_sources[11] =
{
	1,			// => B_AUDIO_INPUT_NONE
	1,			// => B_AUDIO_INPUT_DAC
	0x0000,		// => B_AUDIO_INPUT_LINE_IN
	0x2020,		// => B_AUDIO_INPUT_CD
	0x4040,		// => B_AUDIO_INPUT_VIDEO
	0x3030,		// => B_AUDIO_INPUT_AUX1
	1,			// => B_AUDIO_INPUT_AUX2
	1,			// => B_AUDIO_INPUT_PHONE
	0x0050,		// => B_AUDIO_INPUT_MIC
	0x1010,		// => B_AUDIO_INPUT_MIX_OUT
	0x5000		// => B_AUDIO_INPUT_MONO_OUT
};


static status_t
mux_open(const char *name, uint32 flags, void **cookie)
{
	DBG("AD1816: mux_open()\n");

//	if (strcmp(name, MUX_NAME))
//		return ENODEV;

	atomic_add(&mux_open_count, 1);
//	*cookie = &wss;

	return B_OK;
}


static status_t
mux_close(void *cookie)
{
	atomic_add(&mux_open_count, -1);
	return B_OK;
}


static status_t
mux_free(void *cookie)
{
	DBG("AD1816: mux_free()\n");

	if (0 != mux_open_count)
		dprintf("AD1816: mux open_count is bad in mux_free()!\n");

	return B_OK;
}


static status_t
mux_control(void *cookie, uint32 op, void *arg, size_t len)
{
	audio_routing_cmd *cmd = (audio_routing_cmd *)arg;
	uint32 i;

	//DBG("AD1816: mux_control()\n");

	switch (op)
		{
			case B_ROUTING_GET_VALUES:
				for (i=0; i<cmd->count; i++)
					{
						audio_routing *route = &cmd->data[i];

						switch (route->selector)
							{
								case B_AUDIO_INPUT_SELECT:
									{
										uint8 tmp = get_ireg_bits(ADI_ADC_SRC, ADI_SRC_MASK);

										for (route->value=0; route->value<sizeof(r4_adc_sources); route->value++)
											if (tmp == r4_adc_sources[route->value])
												break;
									}
									break;

								case B_AUDIO_MIC_BOOST:
									route->value = (get_ireg_bits(ADI_MIC_ATTN, 0x4000)) ? 1 : 0;
									break;
							}
					}
				return B_OK;

			case B_ROUTING_SET_VALUES:
				for (i=0; i<cmd->count; i++)
					{
						audio_routing *route = &cmd->data[i];

						switch (route->selector)
							{
								case B_AUDIO_INPUT_SELECT:
									{
										uint16 tmp = r4_adc_sources[route->value];

										if (1 != tmp)
											change_ireg_bits(ADI_ADC_SRC, ADI_SRC_MASK, tmp);
									}
									break;

								case B_AUDIO_MIC_BOOST:
									change_ireg_bits(ADI_MIC_ATTN, 0x4000, (1 & route->value) ? 0x4000 : 0);
									break;
							}
					}
				return B_OK;
		}
	return EINVAL;
}


static status_t
mux_read(void *cookie, off_t pos, void *data, size_t *len)
{
	return EPERM;
}


static status_t
mux_write(void *cookie, off_t pos, const void *data, size_t *len)
{
	return EPERM;
}
