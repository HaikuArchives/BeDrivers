// 	driver.c
//
// 	Copyright (C) 1999-2000 Jens Winkler <jwin@gmx.net>
//
//	AD1816(A) init functions
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
//	the PNP part of this software is based on code from the BeOS R4 SB16
//  driver by Carlos Hasan (which is again based on sample-code provided by
//  Be Inc.):
//
// ---------------------------------------------------------------------------
//	@(#)sb16.c  0.9 1998/10/25 Carlos Hasan (chasan@dcc.uchile.cl)
//              1.0 1998/12/23 Carlos Hasan (chasan@dcc.uchile.cl)
//
//		Sound Blaster 16 device driver for Intel BeOS Release 4.
//
//		Copyright (C) 1998 Carlos Hasan. All Rights Reserved.
//
//		This program is free software; you can redistribute it and/or modify
//		it under the terms of the GNU General Public License as published by
//		the Free Software Foundation; either version 2 of the License, or
//		(at your option) any later version.
// ---------------------------------------------------------------------------
//

#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>

#include <isapnp.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "adi.h"

extern device_hooks adi_dev_hooks;
extern device_hooks adi_mix_hooks;
extern device_hooks adi_mux_hooks;

int32 api_version = B_CUR_DRIVER_API_VERSION;

static const char *version = "AD1816(A) kernel driver v1.2\n";

isa_module_info	*isa_bus;

wss_info wss;

MEMCPY Memcpy;

extern audio_setup config;


static struct {
  spinlock lock;
  uint8 IA, LBT, int_stat;
} isr_tmp; 

// interrupt service routine
static int32
adi_inth(void *arg)
{
  // run atomic
  acquire_spinlock(&isr_tmp.lock); // the ISR is called with interrupts disabled

  // save codec state
  isr_tmp.IA = INB(wss.port);
  OUTB(wss.port, 0);
  isr_tmp.LBT = INB(wss.port + 2);

  // get interrupt status
  isr_tmp.int_stat = INB(wss.port + 1);

  if (0x80 & isr_tmp.int_stat) // handle playback interrupt
    {
      // ack playback interrupt
      OUTB(wss.port + 1, ~0x80 & isr_tmp.int_stat);

      // if no thread is waiting for io -> disable playback transfer
      if (1 == atomic_add(&wss.wr_cnt, -1))
        {
          // stop the transfer
          OUTB(wss.port + ADI_PB_CTRL, ~0x01 & INB(wss.port + ADI_PB_CTRL));
        }
      else // release wait semaphore
        {
          release_sem_etc(wss.wr_wait, 1, B_DO_NOT_RESCHEDULE);
        }

      // get playback interrupt latency
      OUTB(wss.port, ADI_PB_CURRENT);
      wss.wr_lat = 0xff - INB(wss.port + 2);

      // save timestamp
      wss.wr_time   = system_time();
      wss.wr_total += PB_DMA_LEN;
    }

  if (0x40 & isr_tmp.int_stat) // handle capture interrupt
    {
      // ack capture interrupt
      OUTB(wss.port + 1, ~0x40 & isr_tmp.int_stat);

      // if no thread is waiting for io -> disable capture transfer
      if (0 == atomic_add(&wss.rd_cnt, -1))
        {
          // stop transfer
          OUTB(wss.port + ADI_CAP_CTRL, ~0x01 & INB(wss.port + ADI_CAP_CTRL));

          wss.rd_cnt = 0;

          // update offset as the last dma buffer will not be copied
          wss.rd_curr = (wss.rd_base2 == wss.rd_curr) ? wss.rd_base1 : wss.rd_base2;
        }
      else // release wait semaphore
        {
          release_sem_etc(wss.rd_wait, 1, B_DO_NOT_RESCHEDULE);
        }

      // get capture interrupt latency
      OUTB(wss.port, ADI_CAP_CURRENT);
      wss.rd_lat = 0xff - INB(wss.port + 2);

      wss.rd_time   = system_time();
      wss.rd_total += CAP_DMA_LEN;
    }

  // restore codec state
  OUTB(wss.port + 2, isr_tmp.LBT);
  OUTB(wss.port, isr_tmp.IA);

  release_spinlock(&isr_tmp.lock);

  return B_INVOKE_SCHEDULER;
}


static void
movsd_cpy(void *d, const void *s, uint32 l)
{
  __asm__ __volatile__
  ("   cld       \n"
   "\t rep movsl \n"
   : /* no return value */
   : "D" (d), "S" (s), "c" (l/4)
     /* d->edi, s->esi, len/4->ecx */ \
  );
}


static void
mmx_cpy(void *d, const void *s, uint32 l)
{
  __asm__ __volatile__
  (".MOV16B: \n"
   "\t movq  0(%%eax),%%mm0 \n"
   "\t movq  8(%%eax),%%mm1 \n"
   "\t movq %%mm0, 0(%%edx) \n"
   "\t movq %%mm1, 8(%%edx) \n"
   "\t lea  16(%%eax),%%eax \n"
   "\t lea  16(%%edx),%%edx \n"
   "\t dec  %%ecx \n"
   "\t jnz  .MOV16B \n"
   : /* no return value */
   : "d" (d), "a" (s), "c" (l/16)
  );

  __asm__ ("emms");
}


#ifndef NDEBUG
extern audio_setup config;

static int
debug_adi(int argc, char *argv[])
{
  uint8 pb_ctrl, cap_ctrl;

  kprintf(version);

  // disable playback during debugging
  pb_ctrl = INB(wss.port+8);
  OUTB(wss.port+8, 0xFE & pb_ctrl);

  // disable capture during debugging
  cap_ctrl = INB(wss.port+9);
  OUTB(wss.port+9, 0xFE & cap_ctrl);
			
  if (argc > 1 && argv[1][0] == 'd')
    {
      uint8 i;
      uint8 data = 0;

      kprintf("  8237 slave controller registers:\n");
      kprintf("   status      = 0x%02x\n", INB(0x08));
      kprintf("   request     = 0x%02x\n", INB(0x09));
      kprintf("   command     = 0x%02x\n", INB(0x0A));

      INB(0x0E); // clear mode reg counter

      for (i=0; i<=wss.dma; i++)
        {
          data = INB(0x0B);
        }

      kprintf("   mode (dma)  = 0x%02x\n", data);

      INB(0x0E); // clear mode reg counter

      for (i=0; i<=wss.dma2; i++)
        {
          data = INB(0x0B);
        }

      kprintf("   mode (dma2) = 0x%02x\n", data);

      kprintf("   mask        = 0x%02x\n", INB(0x0F));
    }
  else if (argc > 1 && argv[1][0] == 'f')
    {
      kprintf("  audio_format structure:\n");
      kprintf("   sample_rate   = %ld\n", (uint32)config.setup.sample_rate);
      kprintf("   channels      = %ld\n", config.setup.channels);
      kprintf("   format        = 0x%02lx\n", config.setup.format);
      kprintf("   big_endian    = %ld\n", config.setup.big_endian);
      kprintf("   buf_header    = %ld\n", config.setup.buf_header);
      kprintf("   play_buf_size = %ld\n", config.setup.play_buf_size);
      kprintf("   rec_buf_size  = %ld\n", config.setup.rec_buf_size);
    }
  else if (argc > 1 && argv[1][0] == 'a')
    {
      kprintf("  driver config structure wss_info:\n");
      kprintf("   port     = 0x%lx\n", wss.port);
      kprintf("   irq      = %ld\n", wss.irq);
      kprintf("   dma      = %ld\n", wss.dma);
      kprintf("   dma2     = %ld\n", wss.dma2);
      kprintf("   low_mem  = 0x%08lx\n", wss.low_mem);
      kprintf("   wr_buf   = 0x%08lx\n", (uint32)wss.wr_buf);
      kprintf("   rd_buf   = 0x%08lx\n", (uint32)wss.rd_buf);
      kprintf("   reg_cnt  = %ld\n", wss.reg_cnt);
      kprintf("   reg_lck  = 0x%08lx\n", wss.reg_lck);
      kprintf("   wr_cnt   = %ld\n", wss.wr_cnt);
      kprintf("   wr_wait  = 0x%08lx\n", wss.wr_wait);
      kprintf("   wr_base1 = 0x%08lx\n", (uint32)wss.wr_base1);
      kprintf("   wr_base2 = 0x%08lx\n", (uint32)wss.wr_base2);
      kprintf("   wr_curr  = 0x%08lx\n", (uint32)wss.wr_curr);
      kprintf("   wr_time  = %Ld\n", wss.wr_time);
      kprintf("   wr_total = %Ld\n", wss.wr_total);
      kprintf("   wr_ack   = 0x%08lx\n", (uint32)wss.wr_ack);
      kprintf("   rd_cnt   = %ld\n", wss.rd_cnt);
      kprintf("   rd_wait  = 0x%08lx\n", wss.rd_wait);
      kprintf("   rd_base1 = 0x%08lx\n", (uint32)wss.rd_base1);
      kprintf("   rd_base2 = 0x%08lx\n", (uint32)wss.rd_base2);
      kprintf("   rd_curr  = 0x%08lx\n", (uint32)wss.rd_curr);
      kprintf("   rd_time  = %Ld\n", wss.rd_time);
      kprintf("   rd_total = %Ld\n", wss.rd_total);
    }
  else
    {
      uint8 tmp_lbt, tmp_ia;

      tmp_ia = INB(wss.port);

      OUTB(wss.port, 0);
      tmp_lbt = INB(wss.port+2);

      kprintf("  ADI CODEC configuration: io=0x%lx  irq=%ld  dma=%ld  dma2=%ld\n", wss.port, wss.irq, wss.dma, wss.dma2);
      kprintf("  MIDI port configuration: io=0x%lx  irq=%d\n", wss.midi.port, wss.midi.irq);
      kprintf("  memcpy() uses mmx           : %s\n", (mmx_cpy == Memcpy) ? "yes" : "no");
      kprintf("  pending transfers (play/rec): %ld/%ld\n", wss.wr_cnt, wss.rd_cnt);
      kprintf("  last play/rec int: %Ld/%Ld\n", wss.wr_time, wss.rd_time);
      kprintf("  last int latency (play/rec) %dus/%dus:\n", wss.wr_lat, wss.rd_lat);

      kprintf("\n  direct registers:\n");
      kprintf("\tbase+0: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
              INB(wss.port), INB(wss.port+1), INB(wss.port+2), INB(wss.port+3),
              INB(wss.port+4), INB(wss.port+5), INB(wss.port+6), INB(wss.port+7),
              pb_ctrl, cap_ctrl);

      kprintf("\n  indirect registers:\n");
      kprintf("\t00: 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx\n",
              get_ireg(0), get_ireg(1), get_ireg(2), get_ireg(3),
              get_ireg(4), get_ireg(5), get_ireg(6), get_ireg(7));
      kprintf("\t08: 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx\n",
              get_ireg(8), get_ireg(9), get_ireg(10), get_ireg(11),
              get_ireg(12), get_ireg(13), get_ireg(14), get_ireg(15));
      kprintf("\t16: 0x%04lx 0x%04lx 0x%04lx 0x%04lx 0x%04lx\n",
              get_ireg(16), get_ireg(17), get_ireg(18), get_ireg(19),
              get_ireg(20));
      kprintf("\t32: 0x%04lx 0x%04lx 0x%04lx\n", get_ireg(32), get_ireg(33), get_ireg(34));
      kprintf("\t44: 0x%04lx 0x%04lx\n", get_ireg(44), get_ireg(45));

      OUTB(wss.port, 0);
      OUTB(wss.port+2, tmp_lbt);

      OUTB(wss.port, tmp_ia);
   }

  // restore capture state
  OUTB(wss.port+9, cap_ctrl);

  // restore playback state
  OUTB(wss.port+8, pb_ctrl);

  return 0;
}
#endif


// forward references
static void midi_interrupt_op(int32, void *);
static void init_midi( void );
static void uninit_midi( void );


/* ----------
	init_hardware - called once the first time the driver is loaded
----- */
status_t
init_hardware( void )
{
  DBG("AD1816: init_hardware()\n");

  // hardware detection code

  return B_OK;
}


/* ----------
	init_driver - optional function - called every time the driver
	is loaded.
----- */
status_t
init_driver( void )
{
  status_t result;
  uint8 found_codec = 0;
  uint8 found_admpu = 0;

  DBG("AD1816: init_driver()\n");

  memset(&wss, 0, sizeof(wss_info));

  // this pnp code is based on pnp code from the sb16 driver 
  {
    // some useful definitions
    #define PNP_ISA_ID(ch0, ch1, ch2, prod, rev) \
            (((ch0 & 0x1f) << 2) | ((ch1 & 0x18) >> 3) | ((ch1 & 0x07) << 13) | \
            ((ch2 & 0x1f) << 8) | ((prod & 0xff0) << 12) | ((prod & 0x00f) << 28) | ((rev & 0xf) << 24))

    #define PNP_IS_AD1816_DEVICE(id) ((id == PNP_ISA_ID('A', 'D', 'S', 0x718, 0x0)))

    #define PNP_IS_ADMPU_DEVICE(id) ((id == PNP_ISA_ID('A', 'D', 'S', 0x718, 0x1)))

    // pnp code
    resource_descriptor                   resource[4];
    struct device_configuration	          *dev_config;
    struct isa_info	                      *i_info;
    struct device_info                    *dev_info;
    struct device_info                    info;
    uint64                                cookie;
    config_manager_for_driver_module_info *config_mgr;
    int									  size;

    // open plug and play configuration manager 
    result = get_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME, (module_info **)&config_mgr);

    if (B_OK > result)
      return B_ERROR;

    // scan isa bus
    cookie = 0;
    while (B_OK == config_mgr->get_next_device_info(B_ISA_BUS, &cookie, &info, sizeof(info)))
      {
        // get isa dev info
        dev_info = (struct device_info *)malloc(info.size);
        config_mgr->get_device_info_for(cookie, dev_info, info.size);
        i_info = (struct isa_info *) ((char *) dev_info + dev_info->bus_dependent_info_offset);

        // check logical dev id
        if (!found_codec && PNP_IS_AD1816_DEVICE(i_info->logical_device_id))
          {
            // get PnP ISA device configuration structure
            size = config_mgr->get_size_of_current_configuration_for(cookie);
            dev_config = (struct device_configuration *)malloc(size);
            config_mgr->get_current_configuration_for(cookie, dev_config, size);

            // scan dev config resources
            if (B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 2, B_IO_PORT_RESOURCE, &resource[0], sizeof(resource[0])) &&
                B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 0, B_IRQ_RESOURCE, &resource[1], sizeof(resource[1])) &&
                B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 0, B_DMA_RESOURCE, &resource[2], sizeof(resource[2])) &&
                B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 1, B_DMA_RESOURCE, &resource[3], sizeof(resource[3])))
              {
                // parse dev resource descriptors
                uint8 i;

                // get io port
                wss.port = resource[0].d.r.minbase;

                // get codec irq
                for (i=0; 16>i && !(resource[1].d.m.mask & (1 << i)); i++);
                wss.irq = i;

                // get pb dma
                for (i=0; 4>i && !(resource[2].d.m.mask & (1 << i)); i++);
                wss.dma = i;

                // why do I get dma=0 and dma2=1 here?
                // (the codec wants it vice versa)

                // get cap dma
                for (i=0; 4>i && !(resource[3].d.m.mask & (1 << i)); i++);
                wss.dma2 = i;

                found_codec = 1;
              }

            // release dev struct
            free(dev_config);
          }
        else if (!found_admpu && PNP_IS_ADMPU_DEVICE(i_info->logical_device_id))
          {
            // get PnP ISA device configuration structure
            size = config_mgr->get_size_of_current_configuration_for(cookie);
            dev_config = (struct device_configuration *)malloc(size);
            config_mgr->get_current_configuration_for(cookie, dev_config, size);

            // scan dev config resources
            if (B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 0, B_IO_PORT_RESOURCE, &resource[0], sizeof(resource[0])) &&
                B_OK == config_mgr->get_nth_resource_descriptor_of_type(dev_config, 0, B_IRQ_RESOURCE, &resource[1], sizeof(resource[1])))
              {
                // parse dev resource descriptors
                uint8 i;

                // get midi io port
                wss.midi.port = resource[0].d.r.minbase;

                // get midi irq
                for (i=0; 16>i && !(resource[1].d.m.mask & (1 << i)); i++);
                wss.midi.irq = i;

                found_admpu = 1;
              }

            // release dev struct
            free(dev_config);
          }

        free(dev_info);
      }

    put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
  }

  // Codec detected?
  if (!found_codec)
    return B_ERROR;

  // open bus driver
  if (B_OK != get_module(B_ISA_MODULE_NAME, (module_info **)&isa_bus))
    goto init_err1;

  // initialize codec
  set_ireg(ADI_CHIP_CNF, 0x80F0);      // enable wss mode
  set_ireg(ADI_PWDN_CNF, 0x00C0);      // bypass and power down the 3D effect section
  OUTB(wss.port + ADI_PB_CTRL, 0x54);  // set playback sample format to 16LE (STEREO)
  OUTB(wss.port + ADI_CAP_CTRL, 0x14); // set capture sample format to 16LE (STEREO)

  // lock dma channels
  isa_bus->lock_isa_dma_channel(wss.dma);
  isa_bus->lock_isa_dma_channel(wss.dma2);

  // setup register lock benaphore
  if (B_OK > (wss.reg_lck = create_sem(0, "ad1816 register access")))
    goto init_err2;

  set_sem_owner(wss.reg_lck, B_SYSTEM_TEAM);
  wss.reg_cnt = 0;

  // setup config lock benaphore
  if (B_OK > (config.cfg_lck = create_sem(0, "ad1816 setup")))
    goto init_err3;

  set_sem_owner(config.cfg_lck, B_SYSTEM_TEAM);
  config.cfg_cnt = 0;

  // pb dma benaphore
  if (B_OK > (wss.wr_wait = create_sem(0, "ad1816 playback wait")))
    goto init_err4;

  set_sem_owner(wss.wr_wait, B_SYSTEM_TEAM);

  // cap dma benaphore
  if (B_OK > (wss.rd_wait = create_sem(0, "ad1816 record wait")))
    goto init_err5;

  set_sem_owner(wss.rd_wait, B_SYSTEM_TEAM);

  // dma buffers
  wss.wr_base1 = (char *)(DMA_AREA_LEN);                  // buffer alignment
  wss.low_mem  = create_area("ad1816 dma buffer",         // area name
                             (void **)&wss.wr_base1,      // ptr to base address
                             B_ANY_ADDRESS,               // no base constraints
                             DMA_AREA_LEN,                // length of area
                             B_LOMEM,                     // locked, contiguous, below 16MB
                             B_WRITE_AREA | B_READ_AREA); // read and write access

  if (B_OK > wss.low_mem)
    goto init_err6;

  // init dma buffer bases
  wss.wr_base2 = wss.wr_base1 + PB_DMA_LEN;
  wss.rd_base1 = wss.wr_base1 + PB_BUF_LEN;
  wss.rd_base2 = wss.rd_base1 + CAP_DMA_LEN;

  // lock playback dma buffer
  lock_memory(wss.wr_base1, PB_BUF_LEN, B_DMA_IO);

  // lock record dma buffer
  lock_memory(wss.rd_base1, CAP_BUF_LEN, B_READ_DEVICE+B_DMA_IO);

  // get physical base addresses of dma buffers
  {
    physical_entry table;

    get_memory_map(wss.wr_base1, PB_BUF_LEN, &table, 1);
    wss.wr_buf = table.address;

    get_memory_map(wss.rd_base1, CAP_BUF_LEN, &table, 1);
    wss.rd_buf = table.address;
  }

  // init isr MP lock
  isr_tmp.lock = 0;

  // install isr
  install_io_interrupt_handler(wss.irq,
                               (interrupt_handler)adi_inth,
                               NULL,
                               0);
  // enable interrupts
  change_ireg_bits(ADI_INT_ENABLE,
                   ADI_CIE|ADI_PIE,
                   ADI_CIE|ADI_PIE);

  // default assignment
  Memcpy = movsd_cpy;

  // set Memcpy
  __asm__ __volatile__
  (// CPUID support is assumed because BeOS
   // doesn't support x86s without CPUID

   // see if extended CPUID is supported
   "   movl  $0x80000000,%%eax \n"
   "\t cpuid                   \n"
   "\t testl $0x80000000,%%eax \n"
   "\t jz    StdTest           \n"

   // extended CPUID supported; check for mmx
   "\t movl  $0x80000001,%%eax \n"
   "\t cpuid                   \n"
   "\t testl $0x00800000,%%edx \n" // test for bit 23
   "\t jnz   SetMMX            \n"
   "\t jmp   done              \n" // no mmx -> exit

   // standard CPUID; check for mmx
   "StdTest:                   \n"
   "\t movl  $1,%%eax          \n"
   "\t cpuid                   \n"
   "\t testl $0x00800000,%%edx \n" // test for bit 23
   "\t jz    done              \n" // no mmx -> exit

   "SetMMX:               \n" // we have mmx, use movq
   "\t leal mmx_cpy,%%eax \n"
   "\t leal Memcpy,%%edx  \n"
   "\t movl %%eax,(%%edx) \n"
   "done:                 \n"
   : // no return value
   : // no parameters
   : "eax", "ecx", "edx" // registers spilled
  );

  #ifndef NDEBUG
    add_debugger_command("adi",
                         debug_adi,
                         "adi - AD1816(A) kernel driver stats");
  #endif

  // init midi port
  if (found_admpu)
    {
       DBG("AD_MPU: found mpu port at io=0x%lx, irq=%d\n", wss.midi.port, wss.midi.irq);

       wss.midi.module = NULL;
       wss.midi.driver = NULL;

       if (B_OK == get_module(B_MPU_401_MODULE_NAME, (module_info **)&wss.midi.module))
         {
           if (B_OK == wss.midi.module->create_device(wss.midi.port, &wss.midi.driver, 0, midi_interrupt_op, 0))
             {
               init_midi();
             }
           else
             {
               put_module(B_MPU_401_MODULE_NAME);
             }
         }
     }

  return B_OK;

init_err6:
  delete_sem(wss.rd_wait);

init_err5:
  delete_sem(wss.wr_wait); 

init_err4:
  delete_sem(config.cfg_lck);

init_err3:
  delete_sem(wss.reg_lck);

init_err2:
  // unlock dma channels
  isa_bus->unlock_isa_dma_channel(wss.dma2);
  isa_bus->unlock_isa_dma_channel(wss.dma);

  put_module(B_ISA_MODULE_NAME);

init_err1:
  return B_ERROR;
}


/* ----------
	uninit_driver - optional function - called every time the driver
	is unloaded
----- */
void
uninit_driver( void )
{
  DBG("AD1816: uninit_driver()\n");

  // shutdown midi
  uninit_midi();

  #ifndef NDEBUG
   remove_debugger_command("adi", debug_adi);
  #endif

  // wait until the buffers are empty
  snooze(config.setup.play_buf_size * 1000 / (config.setup.sample_rate *
         config.setup.channels * (config.setup.format & 0x0f) / 1000));

  // disable interrupts
  change_ireg_bits(ADI_INT_ENABLE, ADI_CIE|ADI_PIE, 0);

  // remove ISR
  remove_io_interrupt_handler(wss.irq, (interrupt_handler)adi_inth, NULL);

  // unlock dma buffers
  unlock_memory(wss.rd_base1, CAP_BUF_LEN, B_READ_DEVICE+B_DMA_IO);
  unlock_memory(wss.wr_base1, PB_BUF_LEN, B_DMA_IO);

  // deallocate dma area
  delete_area(wss.low_mem);

  // delete record benaphore
  delete_sem(wss.rd_wait);

  // delete playback benaphore
  delete_sem(wss.wr_wait);

  // delete configuration lock
  delete_sem(config.cfg_lck);

  // delete register access lock
  delete_sem(wss.reg_lck);

  // unlock dma channels
  isa_bus->unlock_isa_dma_channel(wss.dma2);
  isa_bus->unlock_isa_dma_channel(wss.dma);

  // free isa bus
  put_module(B_ISA_MODULE_NAME);
}


static void
install_midi_isr( void )
{
  if (0 == wss.midi.isr_installed)
    {
      install_io_interrupt_handler(wss.midi.irq, (interrupt_handler)wss.midi.module->interrupt_hook, wss.midi.driver, 0);
      wss.midi.isr_installed = 1;
    }
  return;			
}


static void
remove_midi_isr( void )
{
  if (1 == wss.midi.isr_installed)
    {
      remove_io_interrupt_handler(wss.midi.irq, (interrupt_handler)wss.midi.module->interrupt_hook, wss.midi.driver);
      wss.midi.isr_installed = 0;
    }
  return;			
}


static void
midi_interrupt_op(int32 op, void *card)
{
  switch (op)
    {
      case B_MPU_401_ENABLE_CARD_INT:
        install_midi_isr();
        return;

      case B_MPU_401_DISABLE_CARD_INT:
        remove_midi_isr();
        return;

      default:
        return;
   }
}


static status_t
midi_open(const char *name, uint32 flags, void **cookie)
{
  return wss.midi.module->open_hook(wss.midi.driver, flags, cookie);
}


static device_hooks
midi_hooks =
{
  midi_open,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};


static const char *midi_name = "midi/ad1816/1";

/* -----
	null-terminated array of device names supported by this driver
----- */
static const char *
adi_device_names[] =
{
  "audio/raw/ad1816/1",
  "audio/mix/ad1816/1",
  "audio/mux/ad1816/1",
  "audio/old/ad1816/1",
  NULL,
  NULL
};


static void
init_midi( void )
{
  /* copy hook functions */
  midi_hooks.close   = wss.midi.module->close_hook;	
  midi_hooks.free    = wss.midi.module->free_hook;	
  midi_hooks.control = wss.midi.module->control_hook;	
  midi_hooks.read    = wss.midi.module->read_hook;	
  midi_hooks.write   = wss.midi.module->write_hook;	

  adi_device_names[4] = midi_name;

  // install isr
  install_midi_isr();
}


static void
uninit_midi( void )
{
  // remove isr
  remove_midi_isr();

  if (NULL != adi_device_names[4])
    {
      adi_device_names[4] = NULL;
      wss.midi.module->delete_device(wss.midi.driver);
      put_module(B_MPU_401_MODULE_NAME);
    }
}


/* ----------
	publish_devices - return a null-terminated array of devices
	supported by this driver.
----- */
const char **
publish_devices( void )
{
  return adi_device_names;
}


/* ----------
	find_device - return ptr to device hooks structure for a
	given device name
----- */
device_hooks *
find_device( const char *name )
{
  if (!strcmp(name, adi_device_names[0]) ||
      !strcmp(name, adi_device_names[3]))
    return &adi_dev_hooks;

  if (!strcmp(name, adi_device_names[1]))
    return &adi_mix_hooks;

  if (!strcmp(name, adi_device_names[2]))
    return &adi_mux_hooks;

  if (NULL != adi_device_names[4])
    if (!strcmp(name, adi_device_names[4]))
      return &midi_hooks;

  return NULL;
}
