// pcm.c
//
// Copyright (C) 1998-2000 Jens Winkler <jwin@gmx.net>
//
// AD1816(A) playback/record functions
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
extern wss_info wss;
extern MEMCPY Memcpy;

// local prototypes
static status_t adi_dev_open(const char *, uint32, void **); 
static status_t adi_dev_close(void *);
static status_t adi_dev_free(void *);
static status_t adi_dev_control(void *, uint32, void *, size_t);
static status_t adi_dev_read(void *, off_t, void *, size_t *);
static status_t adi_dev_write(void *, off_t, const void *, size_t *);

device_hooks
adi_dev_hooks =
{
  adi_dev_open,    // -> open entry point
  adi_dev_close,   // -> close entry point
  adi_dev_free,    // -> free cookie
  adi_dev_control, // -> control entry point
  adi_dev_read,    // -> read entry point
  adi_dev_write    // -> write entry point
};

audio_setup
config =
{
  0, // lock count
  0, // lock semaphore

  // new audio format struct
  {
    44100.0,
    2,
    0x2,
    0,
    0,
    PB_BUF_LEN,
    CAP_BUF_LEN
  }
};

static int32 open_count = 0, open_mode;


static void
adi_write_buffer(const void *src, size_t *nbytes)
{
  bigtime_t sysTime;
  size_t written = 0;
  size_t len = *nbytes;

  *nbytes = 0;

  sysTime = 0;

  while (0 < len)
    {
      // copy PB_DMA_LEN bytes to dma buffer
      Memcpy(wss.wr_curr, src + written, PB_DMA_LEN);

      written += PB_DMA_LEN;
      len     -= PB_DMA_LEN;

      // toggle buffer
      wss.wr_curr = (wss.wr_base2 == wss.wr_curr) ? wss.wr_base1 : wss.wr_base2;

      // start transfer if required
      if (0 != atomic_add(&wss.wr_cnt, 1))
        {
           // wait for io completion
          acquire_sem_etc(wss.wr_wait, 1, B_CAN_INTERRUPT, 0);
        }
      else
        {
          uint32 interrupted;
          cpu_status former;

          former = disable_interrupts();

          // start transfer
          OUTB(wss.port + ADI_PB_CTRL, 0x01 | INB(wss.port + ADI_PB_CTRL));

          sysTime = system_time();

          // this must be 0 for the first start
          interrupted = (0 != wss.wr_time) ? sysTime - wss.wr_time : 0;

          wss.wr_time = sysTime;

          // correct total sample count
          wss.wr_total += interrupted*4 * 441 / 10000;

          restore_interrupts(former);
        }

    }

  *nbytes = written;
  return;
}


static void
adi_read_buffer(void *dst, size_t *nbytes)
{
  bigtime_t  sysTime = 0;
  size_t     read = 0;
  size_t     len = *nbytes;

  //DBG("AD1816: adi_read_buffer(), len = %ld\n", len);

  *nbytes = 0;

  sysTime = 0;

  while (0 < len)
    {
      if (0 == atomic_add(&wss.rd_cnt, 1))
        {
          uint32     interrupted;
          cpu_status former;

          former = disable_interrupts();

          // start transfer
          OUTB(wss.port + ADI_CAP_CTRL, 0x01 | INB(wss.port + ADI_CAP_CTRL));

          sysTime = system_time();

          // this must be 0 for the first start
          interrupted = (0 != wss.rd_time) ? (sysTime - wss.rd_time) : 0;

          // correct total sample count
          wss.rd_total += interrupted*4 * 441 / 10000;

          restore_interrupts(former);
        }

      // wait for io completion
      acquire_sem_etc(wss.rd_wait, 1, B_CAN_INTERRUPT, 0);

      // copy CAP_DMA_LEN bytes from dma buffer
      Memcpy(dst + read, wss.rd_curr, CAP_DMA_LEN);

      read += CAP_DMA_LEN;
      len  -= CAP_DMA_LEN;

      // toggle buffer
      wss.rd_curr = (wss.rd_base2 == wss.rd_curr) ? wss.rd_base1 : wss.rd_base2;
    }

  *nbytes = read;
  return;
}


// gain config access
static void
acquire_cfg_access(void)
{
  if (0 < atomic_add(&config.cfg_cnt, 1))
    {
      acquire_sem_etc(config.cfg_lck, 1, B_CAN_INTERRUPT, 0);
    }
}


// release config access
static void
release_cfg_access(void)
{
  if (1 < atomic_add(&config.cfg_cnt, -1))
    {
      release_sem(config.cfg_lck);
    }
}


static void
change_config(void)
{
  uint32 data;
  uint8  cap_temp;
  uint8  pb_temp;

  DBG("AD1816: change_config()\n");

  acquire_cfg_access();

  if (0x11 == config.setup.format)
    {
      // format is 8 bit
      data = 0;
    }
  else
    {
      // format is 16 bit
      data = 0x10;

      // set big endian bit
      if (0 != config.setup.big_endian)
        {
          data |= 0x20;
        }
    }

  // set stereo bit
  if (2 == config.setup.channels)
    {
      data |= 0x04;
    }

  pb_temp = cap_temp = (uint8)data;

  // get current playback state, clear the format and
  // channel setting and replace them with the new values
  pb_temp  |= (~0x34 & INB(wss.port + ADI_PB_CTRL));

  // get current capture state, clear the format and
  // channel setting and replace them with the new values
  cap_temp |= (~0x34 & INB(wss.port + ADI_CAP_CTRL));

  // we have to stop playback and capture
  // to change the sample rate

  if (0x01 & pb_temp)
    {
      OUTB(wss.port + ADI_PB_CTRL, ~0x01 & pb_temp);
    }

  if (0x01 & cap_temp)
    {
      OUTB(wss.port + ADI_CAP_CTRL, ~0x01 & cap_temp);
    }

  // set new sample rate
  data = (uint32)config.setup.sample_rate;

  acquire_reg_access();

  set_ireg(ADI_PB_RATE, data);

  release_reg_access();

  // reactivate transfer
  OUTB(wss.port + ADI_PB_CTRL, pb_temp);
  OUTB(wss.port + ADI_CAP_CTRL, cap_temp);

  release_cfg_access();
}


static void
start_pb_dma(void)
{
  // clear dma buffer
  MEMCLR(wss.wr_base1, PB_BUF_LEN);

  // reset buffer toggle
  wss.wr_curr = wss.wr_base1;

  // start dma controller
  isa_bus->start_isa_dma(wss.dma, wss.wr_buf, PB_BUF_LEN, DMA_IO_WRITE, DMA_EMODE);
}


static void
start_cap_dma(void)
{
  // clear dma buffer
  MEMCLR(wss.rd_base1, CAP_BUF_LEN);

  // reset buffer toggle
  wss.rd_curr = wss.rd_base1;

  // start dma controller
  isa_bus->start_isa_dma(wss.dma2, wss.rd_buf, CAP_BUF_LEN, DMA_IO_READ, DMA_EMODE);
}


/* ----------
	my_device_open - handle open() calls
----- */
static status_t
adi_dev_open(const char *name, uint32 flags, void **cookie)
{
  // initialize hardware for the first stream
  if (0 == atomic_add(&open_count, 1))
    {
      wss.wr_time = wss.rd_time = 0;

      // save open mode flags
      open_mode = (flags & O_RWMASK);

      if (O_RDONLY != open_mode)
        {
          start_pb_dma();
        }

      if (O_WRONLY != open_mode)
        {
          start_cap_dma();
        }

      acquire_reg_access();

      // transfer length (codec)
      set_ireg(ADI_PB_BASE, (PB_DMA_LEN>>2)-1);		
      set_ireg(ADI_CAP_BASE, (CAP_DMA_LEN>>2)-1);		

      release_reg_access();

      // init codec
      change_config();
    }
  else if (O_RDWR == (flags & O_RWMASK))
    {
      if (O_RDONLY == open_mode)
        {
          start_pb_dma();
        }
      else if (O_WRONLY == open_mode)
        {
          start_cap_dma();
        }

      open_mode = O_RDWR;
    }

  return B_OK;
}


/* ----------
	my_device_read - handle read() calls
----- */
static status_t
adi_dev_read(void *cookie, off_t position, void *buffer, size_t *num_bytes)
{
  audio_buf_header *header = buffer;

  if (O_WRONLY == open_mode)
    return EPERM;

  buffer     += config.setup.buf_header;
  *num_bytes -= config.setup.buf_header;

  adi_read_buffer(buffer, num_bytes);

  // buffer header requested ?
  if (config.setup.buf_header)
    {
      *num_bytes += config.setup.buf_header;

      // fill buffer header
      header->capture_time = wss.rd_time - (wss.rd_lat * 10000)/441;
      header->capture_size = *num_bytes;
      header->sample_rate  = config.setup.sample_rate;
    }
  return B_OK;
}


/* ----------
	my_device_write - handle write() calls
----- */
static status_t
adi_dev_write(void *cookie, off_t position, const void *buffer, size_t *num_bytes)
{
  if (O_RDONLY == open_mode)
    return EPERM;

  adi_write_buffer(buffer, num_bytes);
  return B_OK;
}


// preferred sample-rates
static const float
rates[4] = { 48000.0, 44100.0, 32000.0, 8000.0 };

// old sound setup struct
static sound_setup
ssetup =
{
  { 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0 },
  { 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0 },
  kHz_44_1,
  linear_16bit_little_endian_stereo,
  linear_16bit_little_endian_stereo,
  0, 0,
  0, 0,
  0, 0
};


/* ----------
	my_device_control - handle ioctl calls
----- */
static status_t
adi_dev_control(void *cookie, uint32 op, void *arg, size_t len)
{
  status_t err = B_BAD_VALUE;
  bool configure = false;

  switch (op)
    {
      case B_AUDIO_GET_AUDIO_FORMAT:
        {
          acquire_cfg_access();

          memcpy(arg, &config.setup, sizeof(audio_format));

          release_cfg_access();
          return B_OK;
        }

      case B_AUDIO_GET_PREFERRED_SAMPLE_RATES:
        memcpy(arg, rates, sizeof(rates));
        return B_OK;

      case B_AUDIO_SET_AUDIO_FORMAT:
        {
          audio_format *format = (audio_format *)arg;

          acquire_cfg_access();

          config.setup.sample_rate = max(min(format->sample_rate, 55000.0), 4000.0);
          config.setup.channels    = max(min(format->channels, 2), 1);
          config.setup.format      = format->format;
          config.setup.big_endian  = format->big_endian;
          config.setup.buf_header  = max(format->buf_header, 0);

          if ((PB_BUF_LEN-1) & format->play_buf_size)
            {
              format->play_buf_size = (format->play_buf_size + PB_BUF_LEN) & ~(PB_BUF_LEN-1);
            }

          config.setup.play_buf_size = max(format->play_buf_size, PB_BUF_LEN);

          if ((CAP_BUF_LEN-1) & format->rec_buf_size)
            {
              format->rec_buf_size = (format->rec_buf_size + CAP_BUF_LEN) & ~(CAP_BUF_LEN-1);
            }

          config.setup.rec_buf_size = max(format->rec_buf_size, CAP_BUF_LEN);

          release_cfg_access();

          configure = true;
          err = B_OK;
        }
        break;

        case SOUND_GET_PARAMS:
          memcpy(arg, &ssetup, sizeof(sound_setup));
          return B_OK;

        case SOUND_SET_PARAMS:
          { // this code is based on the sonic_vibes driver sample
            sound_setup *sound = (sound_setup *)arg;
            uint32 tmp[7];

            acquire_cfg_access();

            err = B_OK;

            switch (sound->left.adc_source)
              {
                case line:
                  tmp[0] = 0;
                  break;

                case aux1:
                  tmp[0] = 0x2020;
                  break;

                case mic:
                  tmp[0] = 0x5050;
                  break;

                default:
                  tmp[0] = 0x1010;
              }

            // left channel
            tmp[0] |= (sound->left.adc_gain & 0xf) << 8;
            tmp[1] = (sound->left.mic_gain_enable) ? 0x4088 : 0x0088;
            tmp[2] = (sound->left.aux1_mix_gain & 0x1f) << 8;
            if (sound->left.aux1_mix_mute) tmp[2] |= 0x8000;
            //tmp[3]  = (sound->left.aux2_mix_gain & 0x1f) << 8;
            //if (sound->left.aux2_mix_mute) tmp[3] |= 0x8000;
            tmp[4] = (sound->left.line_mix_gain & 0x1f) << 8;
            if (sound->left.line_mix_mute) tmp[4] |= 0x8000;
            tmp[5] = (sound->left.dac_attn & 0x3f) << 8;
            if (sound->left.dac_mute) tmp[5] |= 0x8000;

            // right channel
            tmp[0] |= (sound->right.adc_gain & 0xf);
            tmp[2] |= (sound->right.aux1_mix_gain & 0x1f);
            if (sound->right.aux1_mix_mute) tmp[2] |= 0x80;
            //tmp[3] |= (sound->right.aux2_mix_gain & 0x1f);
            //if (sound->right.aux2_mix_mute) tmp[3] |= 0x80;
            tmp[4] |= (sound->right.line_mix_gain & 0x1f);
            if (sound->right.line_mix_mute) tmp[4] |= 0x80;
            tmp[5] |= (sound->right.dac_attn & 0x3f);
            if (sound->right.dac_mute) tmp[5] |= 0x80;

            // since all audio apps currently ignoring the aux2 setting,
            // the DAC setting is also used to control the Wavetable
            tmp[3] = (0x8080 & tmp[5]) | (0x1f1f & (tmp[5]>>1));

            // ignore sample rate
            sound->sample_rate = kHz_44_1;

            if (44100.0 != config.setup.sample_rate)
              {
                config.setup.sample_rate = 44100.0;
                configure = true;
              }

            // we only support 16-bit formats
            if ((sound->playback_format == linear_16bit_big_endian_stereo &&
                ssetup.playback_format != linear_16bit_big_endian_stereo) ||
                (sound->capture_format == linear_16bit_big_endian_stereo &&
                ssetup.capture_format != linear_16bit_big_endian_stereo))
              {
                config.setup.big_endian = 1;
                config.setup.format = 0x2;
                configure = true;
              }
            else if ((sound->playback_format == linear_16bit_little_endian_stereo &&
                     ssetup.playback_format != linear_16bit_little_endian_stereo) ||
                     (sound->capture_format == linear_16bit_little_endian_stereo &&
                     ssetup.capture_format != linear_16bit_little_endian_stereo))
              {
                config.setup.big_endian = 0;
                config.setup.format = 0x2;
                configure = true;
              }

            // ignore these value
            sound->dither_enable = false;

            // this controls the microphone input 
            // on the x86 platform
            tmp[1] |= (sound->loop_attn & 0x3f) << 7;
            if (!sound->loop_enable) tmp[1] |= 0x8000;

            // ignore these values
            sound->output_boost    = 0;
            sound->highpass_enable = 0;
	
            // this is a stereo control in AD1816...
            tmp[6] = 0;
            if (sound->mono_mute) tmp[6] |= 0x8080;

            acquire_reg_access();

            // write setup to codec
            set_ireg(ADI_ADC_SRC, tmp[0]);
            set_ireg(ADI_MIC_ATTN, tmp[1]);
            set_ireg(ADI_CD_ATTN, tmp[2]);
            set_ireg(ADI_SYNTH_ATTN, tmp[3]);
            set_ireg(ADI_LINE_ATTN, tmp[4]);
            set_ireg(ADI_DAC_ATTN, tmp[5]);
            set_ireg(ADI_MASTER_ATTN,tmp[6]);

            release_reg_access();

            memcpy(&ssetup, sound, sizeof(sound_setup));

            release_cfg_access();
          }
          break;

        case SOUND_SET_PLAYBACK_COMPLETION_SEM:
          wss.wr_ack = *((sem_id *)arg);
          return B_OK;

        case SOUND_SET_CAPTURE_COMPLETION_SEM:
          wss.rd_ack = *((sem_id *)arg);
          return B_OK;

        case SOUND_UNSAFE_WRITE:
          {
            audio_buffer_header *header = (audio_buffer_header *)arg;
            size_t len = header->reserved_1 - sizeof(*header);

            adi_write_buffer(header+1, &len);

            header->time         = wss.wr_time - (wss.wr_lat * 10000)/441;
            header->sample_clock = wss.wr_total/4 * 10000  / 441;

            return release_sem_etc(wss.wr_ack, 1, B_DO_NOT_RESCHEDULE);
          }

        case SOUND_UNSAFE_READ:
          {
            audio_buffer_header *header = (audio_buffer_header *)arg;
            size_t len = header->reserved_1 - sizeof(*header);

            adi_read_buffer(header+1, &len);

            header->time         = wss.rd_time - (wss.rd_lat * 10000)/441;
            header->sample_clock = wss.rd_total/4 * 10000 / 441;

            return release_sem_etc(wss.rd_ack, 1, B_DO_NOT_RESCHEDULE);
          }

        case SOUND_LOCK_FOR_DMA:
          return B_OK;

        case SOUND_SET_CAPTURE_PREFERRED_BUF_SIZE:
          {
            int32 size = (int32)arg;

            acquire_cfg_access();

            if ((CAP_BUF_LEN-1) & size)
              {
                config.setup.rec_buf_size = (size + CAP_BUF_LEN) & ~(CAP_BUF_LEN-1);
              }

            // ensure the buffer has at least a size of CAP_BUF_LEN
            config.setup.rec_buf_size = max(config.setup.rec_buf_size, CAP_BUF_LEN);

            release_cfg_access();
            return B_OK;
          }

        case SOUND_SET_PLAYBACK_PREFERRED_BUF_SIZE:
          {
            int32 size = (int32)arg;

            acquire_cfg_access();

            if ((PB_BUF_LEN-1) & size)
              {
                config.setup.play_buf_size = (size + PB_BUF_LEN) & ~(PB_BUF_LEN-1);
              }

            // ensure the buffer has at least a size of PB_BUF_LEN
            config.setup.play_buf_size = max(config.setup.play_buf_size, PB_BUF_LEN);

            release_cfg_access();
            return B_OK;
          }

        case SOUND_GET_CAPTURE_PREFERRED_BUF_SIZE:
          *((int32 *)arg) = config.setup.rec_buf_size;
          return B_OK;

        case SOUND_GET_PLAYBACK_PREFERRED_BUF_SIZE:
          *((int32 *)arg) = config.setup.play_buf_size;
          return B_OK;

        default:
          return B_BAD_VALUE;
      }

  if ((err == B_OK) && configure)
    {
      change_config();
    }

  return err;
}


/* ----------
	my_device_close - handle close() calls
----- */
static status_t
adi_dev_close(void *cookie)
{
  if (0 == atomic_add(&open_count, -1))
    {
      // closed too often
      atomic_add(&open_count, 1);

      return B_ERROR;
    }

  return B_OK;
}


/* -----
	my_device_free - called after the last device is closed, and after
	all i/o is complete.
----- */
static status_t
adi_dev_free(void *cookie)
{
  open_mode = 0;
  return B_OK;
}
