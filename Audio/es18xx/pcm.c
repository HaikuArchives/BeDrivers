/*	pcm.c*/
/*	Sound Blaster Pro*/
/*	Joseph Wang*/
/*	11.4.99*/

#include <Drivers.h>
#include <KernelExport.h>
#include <ISA.h>
#include <malloc.h>
#include "ess.h"
#include "pcm.h"
#include "driver.h"

//#define kprintf (void)

#define SNTIME 500

size_t write_limit, stall_state;
int32 open_count=0;
sem_id write_sem;

static sem_id write_sync_sem = B_BAD_SEM_ID;

static int32 mode, write_offset, write_size, write_state, write_latency;
static bigtime_t write_time, write_clock;

/*static status_t pcm_open(const char *name, uint32 flags, void **cookie);*/
/*static status_t pcm_close(void *cookie);*/
/*static status_t pcm_free(void *cookie);*/
/*static status_t pcm_control(void *cookie, uint32 op, void *data, size_t len);*/
/*static status_t pcm_read(void *cookie, off_t pos, void *data, size_t *len);*/
/*static status_t pcm_write(void *cookie, off_t pos, const void *data, size_t *len);*/

static status_t pcm_set_params(sound_setup *setup);
static status_t pcm_get_params(sound_setup *setup);
static void set_dma_mode(int32 new_mode, const char *data, int32 len);
/*static int32 pcm_write_inth(void *data);*/

status_t pcm_open(const char *name, uint32 flags, void **cookie) {
	audio_format *format;
	if(atomic_add(&open_count, 1)!=0) ;//return B_ERROR;
	format=(audio_format *)malloc(sizeof(audio_format));
	format->sample_rate=DEFAULT_SAMPLE_RATE;
	format->channels=DEFAULT_CHANNELS;
	format->format=DEFAULT_FORMAT;
	format->big_endian=DEFAULT_ENDIAN;
	format->buffer_header=DEFAULT_BUFFER_HEADER;
	format->write_buf_size=DEFAULT_WRITE_BUF_SIZE;
	format->rec_buf_size=DEFAULT_REC_BUF_SIZE;
	cookie=(void **)&format;
	mode=MODE_NONE;

	//boost gain
	acquire_sl();
	write_mixer(ESS_MASTER_GAIN, 0xee);
	release_sl();
	return B_OK;
	}

status_t pcm_close(void *cookie) {
	atomic_add(&open_count, -1);
	return B_OK;
	}

status_t pcm_free(void *cookie) {
	return B_OK;
	}

status_t pcm_control(void *cookie, uint32 op, void *data, size_t len) {
	status_t err;
	audio_buffer_header *header;
	static float rates[]={44100.0, 44100.0, 44100.0, 44100.0};
	switch(op) {
	case B_AUDIO_SET_AUDIO_FORMAT:
		memcpy(cookie, data, min(sizeof(audio_format), len));
		return B_OK;
	case B_AUDIO_GET_AUDIO_FORMAT:
		memcpy(data, cookie, sizeof(audio_format));
		return B_OK;
	case B_AUDIO_GET_PREFERRED_SAMPLE_RATES:
		memcpy(data, rates, sizeof(rates));
		return B_OK;
	case SOUND_GET_PARAMS:
		return pcm_get_params((sound_setup *)data);
	case SOUND_SET_PARAMS:
		return pcm_set_params((sound_setup *)data);
/*	case SOUND_SET_CAPTURE_COMPLETION_SEM:*/
	case SOUND_SET_PLAYBACK_COMPLETION_SEM:
		write_sync_sem=*((sem_id *)data);
		return B_OK;
	case SOUND_UNSAFE_WRITE: 
		header=(audio_buffer_header *)data;
		len=header[0].reserved_1-sizeof(header[0]);
		err=pcm_write(cookie, 0, (void *)&header[1], &len);
		header[0].sample_clock=(write_clock*1000)/1764;
		header[0].time=header[0].sample_clock;//system_time()-write_time;
		return release_sem(write_sync_sem);
	case 10012:
	case 10013:
		*(int32*)data = B_PAGE_SIZE;
		return B_OK;
		}
	return EINVAL;
	}

status_t pcm_read(void *cookie, off_t pos, void *data, size_t *len) {
	return B_OK;
	}

status_t pcm_write(void *cookie, off_t pos, const void *data, size_t *len) {
	int32 i, togo=*len;
	char *raw=(char *)data;
	if (acquire_sem_etc(write_sem, 1, B_CAN_INTERRUPT | B_TIMEOUT, 1000000LL) < B_OK)
		return EIO;
	set_dma_mode(MODE_WRITE_16BIT_AUTO, data, togo);
	stall_state=0;
	for(;togo>0;) {
		if(write_size>=2*write_latency) {
			snooze(SNTIME);
			continue;
			}
		if(write_state==0) {
			snooze(SNTIME);
			continue;
			}
		i=min(togo, write_latency);
		memcpy(write_buffer.data+write_offset, raw, i);
		togo-=i;
		raw+=i;
		write_offset+=write_latency;
		if(write_offset>=write_limit) write_offset=0;
		write_size+=write_latency;
		}
	acquire_sl();
	write_reg(0xb8, read_reg(0xb8)&0xfb);
	write_reg(0xb8, read_reg(0xb8)&0xfe);
	write_state=0;
	write_clock+=*len;
	release_sl();
	release_sem_etc(write_sem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
	}

/*support functions*/

void set_dma_mode(int32 new_mode, const char *data, int32 len) {
/*	acqu=ire_sl();*/
	physical_entry table;
	if(new_mode==mode) {
/*		unlock dma channel*/
		switch(mode) {
		case MODE_WRITE_16BIT_AUTO:
			get_memory_map(write_buffer.data, write_buffer.size, &table, 1);
			isamod_info->start_isa_dma(dma8, table.address, table.size, 0x58, 0);
			write_offset=0;
			write_state=0;
acquire_sl();
			write_reg(0xb8, read_reg(0xb8)|1);
release_sl();
			}
		return;
		}
	mode=new_mode;
	switch(mode) {
		case MODE_WRITE_16BIT_AUTO: {
			physical_entry table;
			get_memory_map(write_buffer.data, write_buffer.size, &table, 1);
acquire_sl();
			write_data(0xd3);
release_sl();
			isamod_info->start_isa_dma(dma8, table.address, table.size, 0x58, 0);
			write_limit=write_buffer.size;
			write_time=system_time();
			write_clock=0;
			write_state=0;
			write_size=0;
			write_latency=min(len, DEFAULT_WRITE_BUF_SIZE);
			write_offset=0;
acquire_sl();
			write_reg(0xb8, 0);
			write_reg(0xa8, (read_reg(0xa8)&0xfc)|1);
			write_reg(0xb9, 0);
/*			44.1kHz*/
			write_reg(0xa1, 238);
			write_reg(0xa2, 251);
			write_reg(0xa4, (-write_latency)&0xff);
			write_reg(0xa5, (-write_latency)>>8);
			write_reg(0xb6, 0x00);
			write_reg(0xb7, 0x71);
			write_reg(0xb7, 0xbc);
			write_reg(0xb1, (read_reg(0xb1)&0x1f)|0x40);
			write_reg(0xb2, (read_reg(0xb2)&0x1f)|0x40);
			write_reg(0xb8, read_reg(0xb8)|1);
release_sl();
			snooze(10000);
acquire_sl();
			write_data(0xd1);
release_sl();
			}
		}
/*	release_sl();*/
	}

int32 pcm_write_inth(void *data) {
/*	acquire_sl();*/

/*	release_sl();*/
	stall_state=0;
	write_state=1;
	write_size-=write_latency;
acquire_sl();
	read_io(ESS_READ_BUFFER_STATUS);
release_sl();
	return B_INVOKE_SCHEDULER;
	}

status_t pcm_set_params(sound_setup *sound) {
	int value;
/*			no controls for channel.adc_gain*/
	value=((15-sound->left.adc_gain)<<4)+(15-sound->right.adc_gain);
	write_mixer(ESS_ADC_GAIN, value);
/*			set channel.mic levels, 1bit->2bit*/
	value=0;
	if(sound->left.mic_gain_enable) value+=2<<4;
	if(sound->right.mic_gain_enable) value+=2;
	write_mixer(ESS_MIC_GAIN, value);
/*			aux1 maps to CD-ROM, 5bit->4bit*/
	value=0;
	if(!sound->left.aux1_mix_mute)
			value+=((31-sound->left.aux1_mix_gain)<<3)&0xf0;
	if(!sound->right.aux1_mix_mute)
			value+=((31-sound->right.aux1_mix_gain)>>1)&0xf;
	write_mixer(ESS_CD_OUT_GAIN, value);
/*			aux2 maps to MIDI, 5->4*/
	value=0;
	if(!sound->left.aux2_mix_mute)
			value+=((31-sound->left.aux2_mix_gain)<<3)&0xf0;
	if(!sound->right.aux2_mix_mute)
			value+=(31-sound->right.aux2_mix_gain)>>1;
	write_mixer(ESS_MIDI_OUT_GAIN, value);
/*			set line levels, 5->4*/
	value=0;
	if(!sound->left.line_mix_mute)
			value+=((31-sound->left.line_mix_gain)<<3)&0xf0;
	if(!sound->right.line_mix_mute)
			value+=(31-sound->right.line_mix_gain)>>1;
	write_mixer(ESS_LINE_OUT_GAIN, value);
/*			dac, 6->4*/
	value=0;
	if(!sound->left.dac_mute)
			value+=((63-sound->left.dac_attn)*4)&0xf0;
	if(!sound->right.dac_mute)
			value+=((63-sound->right.dac_attn)*4)>>4;
	write_mixer(ESS_DAC_OUT_GAIN, value);
/*			only support 22.1kHz*/
/*			only support 8 bit stereo playback*/
/*			only support 8 bit stereo capture*/
/*			no control for dither_enable*/
/*			no control for loop_attn*/
/*			no control for loop back*/
/*			no control for output boost*/
/*			no control for high pass filter*/
/*			no control for speaker gain*/
/*			un/mute speaker*/
/*			if(sound->mono_mute) hw_codec_write_byte(SB16_SPEAKER_DISABLE);*/
/*			else hw_codec_write_byte(SB16_SPEAKER_ENABLE);*/
	return B_OK;
	}

status_t pcm_get_params(sound_setup *sound) {
	int value, value2;
/*	acquire_sl();*/
/*	release_sl();*/
/*return B_OK;*/
/*			no controls for channel.adc_gain*/
	value=value2=read_mixer(ESS_ADC_GAIN);
	sound->left.adc_gain=15-(value>>4);
	sound->right.adc_gain = 15-(value2&0xf);
/*			set channel.mic levels, 1bit->2bit*/
	value=value2=read_mixer(ESS_MIC_GAIN);
	sound->left.mic_gain_enable=value>>4;
	sound->right.mic_gain_enable=value2&0xf;
/*			CD-ROM maps to aux1, 4bit->5bit, no mute control*/
	value=value2=read_mixer(ESS_CD_OUT_GAIN);
	value=30-((value>>3)&0x1e);
	value2=30-((value2<<1)&0x1e);
	sound->left.aux1_mix_gain=value;
	sound->right.aux1_mix_gain=value2;
/*			MIDI maps to aux2, 4->5*/
	value=value2=read_mixer(ESS_MIDI_OUT_GAIN);
	value=30-((value>>3)%30);
	value2=30-((value2<<1)%30);
	sound->left.aux2_mix_gain=value;
	sound->right.aux2_mix_gain=value2;
	sound->left.aux2_mix_mute=sound->right.aux2_mix_mute=0;
/*			set line levels, 4->5*/
	value=value2=read_mixer(ESS_LINE_OUT_GAIN);
	value=30-((value>>3)&30);
	value2=30-((value2<<1)&30);
	sound->left.line_mix_gain=value;
	sound->right.line_mix_gain=value2;
	sound->left.line_mix_mute=sound->right.line_mix_mute=0;
/*			dac maps to voice, 6->4*/
	value=value2=read_mixer(ESS_DAC_OUT_GAIN);
	value=((value&0xf0)>>4)*61/15;
	value2=((value2&0xf))*61/15;
	sound->left.dac_attn=61-value;
	sound->right.dac_attn=61-value2;
	sound->left.dac_mute=0;
	sound->right.dac_mute=0;

/*			only support 22.05kHz*/
	sound->sample_rate = kHz_44_1;
/*			only support 8 bit stereo playback*/
	sound->playback_format = linear_16bit_little_endian_stereo;
/*			only support 8 bit stereo capture*/
	sound->capture_format = linear_16bit_little_endian_stereo;
/*			no control for dither_enable*/
	sound->dither_enable = 0;
/*			no control for loop_attn*/
	sound->loop_attn = 64;
/*			no control for loop back*/
	sound->loop_enable = 0;
/*			no control for output boost*/
	sound->output_boost = 0;
/*			no control for high pass filter*/
	sound->highpass_enable = 0;
/*			no control for speaker gain*/
	sound->mono_gain = 64;
/*			un/mute speaker*/
	sound->mono_mute = 0;
/*			if(sound->mono_mute) hw_codec_write_byte(SB16_SPEAKER_DISABLE);*/
/*			else hw_codec_write_byte(SB16_SPEAKER_ENABLE);*/
	return B_OK;
	}
