// adi.h
//
// Copyright (C) 1998-2000 Jens Winkler <jwin@gmx.net>
//
// AD1816(A) driver private definitions
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

#include <ISA.h>

#include "midi_driver.h"


typedef struct
{
  generic_mpu401_module *module;

  void   *driver;
  uint32 port;
  uint8  irq;
  uint8  isr_installed;
} Midi;


typedef struct
{
  // hw setup
  uint32     port;       // codec io port
  uint32     irq;        // codec interrupt
  uint32     dma;        // codec playback dma
  uint32     dma2;       // codec record dma

  area_id   low_mem;     // low memory area

  void      *wr_buf;     // physical dma buffer addresses
  void      *rd_buf;

  int32     reg_cnt;     // benaphore count
  sem_id    reg_lck;     // indirect register access lock

  // playback
  int32     wr_cnt;      // actual num of pending transfers
  sem_id    wr_wait;     // io completion semaphore
  void      *wr_base1;   // virtual first half base
  void      *wr_base2;   // virtual second half base
  void      *wr_curr;    // actual half for copy

  // old playback api
  bigtime_t wr_time;     // play time
  uint64    wr_total;    // total # of samples played
  sem_id    wr_ack;      // completion semaphore
  uint8     wr_lat;      // playback interrupt latency

  // capture
  int32     rd_cnt;      // actual num of pending transfers
  sem_id    rd_wait;     // io completion semaphore
  void      *rd_base1;   // virtual first half base
  void      *rd_base2;   // virtual second half base
  void      *rd_curr;    // actual half for copy

  // old capture api
  bigtime_t	rd_time;     // rec time
  uint64    rd_total;    // total # of samples recorded
  sem_id    rd_ack;      // completion semaphore
  uint8     rd_lat;      // record interrupt latency

  // midi port
  Midi		midi;
} wss_info;

typedef struct
{
  int32        cfg_cnt;
  sem_id       cfg_lck;
  audio_format setup;
} audio_setup;


// dma buffer area size
#define DMA_AREA_LEN        (B_PAGE_SIZE*3)

// dma cycling buffer size
#define PB_BUF_LEN			(B_PAGE_SIZE)	// 4k
#define CAP_BUF_LEN			(B_PAGE_SIZE*2)	// 8k

// length of dma transfers
#define PB_DMA_LEN			(PB_BUF_LEN/2)
#define CAP_DMA_LEN			(CAP_BUF_LEN/2)

// 8237 mode flags
#define DMA_BLOCK		0x80	// block transfer
#define DMA_SINGLE		0x40	// single transfer
#define	DMA_DECR		0x20	// address decr
#define DMA_AUTO		0x10	// autoinit
#define DMA_READ		0x08	// mem -> io
#define DMA_WRITE		0x04	// io -> mem

#define DMA_IO_READ		DMA_SINGLE|DMA_AUTO|DMA_WRITE
#define DMA_IO_WRITE	DMA_SINGLE|DMA_AUTO|DMA_READ

#define DMA_EMODE		0		// extended mode ??? flags

// AD1816 indirect registers
#define ADI_INT_ENABLE		 1		// interrupt enable reg
#define ADI_PB_RATE			 2		// playback sample rate
#define ADI_CAP_RATE		 3		// capture sample rate
#define ADI_DAC_ATTN		 4		// voice vol
#define ADI_OPL_ATTN		 5		// opl3 vol
#define ADI_PB_BASE			 8		// playback transfer length
#define ADI_PB_CURRENT       9      // current playback sample-ptr
#define ADI_CAP_BASE		10		// capture transfer length
#define ADI_CAP_CURRENT     11      // current capture sample-ptr
#define ADI_TIMER_BASE		12		// timer length
#define ADI_MASTER_ATTN		14		// master vol
#define	ADI_CD_ATTN			15		// cd vol
#define ADI_SYNTH_ATTN		16		// wave vol
#define ADI_VID_ATTN		17		// vid vol
#define ADI_LINE_ATTN		18		// line-in vol
#define ADI_MIC_ATTN		19		// mic vol
#define ADI_ADC_SRC			20		// source select
#define ADI_CHIP_CNF		32		// chip config
#define ADI_CTRL_3D			39		// 3D effect control
#define ADI_PWDN_CNF		44		// power management config

// AD1816 direct registers
#define ADI_PB_CTRL			 8			// playback control
#define ADI_CAP_CTRL		 9			// capture control

// enable transfer bits
#define ADI_PEN				 0x01		// playback enable
#define ADI_CEN				 0x01		// capture enable

// int enable bits
#define ADI_PIE				0x8000		// playback int enable
#define ADI_CIE				0x4000		// capture int enable
#define ADI_TIE				0x2000		// timer int enable
#define ADI_TE				0x0080		// timer enable

// input src mask
#define ADI_SRC_MASK		0x7070

// io port read/write
#define INB(x)		isa_bus->read_io_8(x)
#define OUTB(x,y)	isa_bus->write_io_8(x,y)

typedef void (*MEMCPY)(void *, const void *, uint32);

// memory macros
#define MEMCLR(d,l) __asm__ __volatile__        \
                    ("   cld              \n"   \
                     "\t xorl %%eax,%%eax \n"   \
                     "\t rep stosl        \n"   \
                     : /* no return value */    \
                     : "D" (d), "c" (l/4)     \
                       /* d->edi, l/4->ecx */ \
                    );

#if 0
#define BSWAP32(d,s) __asm__ __volatile__\
                     ("bswap %0\n"       \
                      : "=r" (d)         \
                      : "0" (s)          \
                     );
#endif


// prototypes (register.c)
void    acquire_reg_access(void);
void    release_reg_access(void);
uint32	get_ireg(uint8);
void	set_ireg(uint8, uint32);
void	change_ireg_bits(uint8, uint32, uint32);
uint32	get_ireg_bits(uint8, uint32);


// debugging stuff
#ifndef NDEBUG
# define DBG(x...) dprintf(x)
#else
# define DBG(x...)
#endif

