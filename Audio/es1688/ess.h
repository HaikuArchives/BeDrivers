/*	sbpro.h*/
/*	Sound Blaster Pro*/
/*	Joseph Wang*/
/*	4.4.99*/

#ifndef ESS_H
#define ESS_H


#define ENABLE0	0x0
#define ENABLE1	0x9
#define ENABLE2	0xb

#define ESS_VENDOR_ID				0x00007316
#define ESS_CARD_ID_MASK			0x00007316 // was 0x00187316
#define DMA_TRANSFER_SIZE			2*B_PAGE_SIZE
#define ESS_DELAY					3

/*port offsets*/
#define ESS_RESET					0x6
#define ESS_READ_BUFFER_STATUS		0xe
#define ESS_READ_DATA				0xa
#define ESS_WRITE_BUFFER_STATUS		0xc
#define ESS_WRITE_DATA				0xc
#define ESS_REG						0x4
#define ESS_DATA					0x5

/*commands*/
#define ESS_EXTENDED				0xc6
//#define SBPRO_VERSION				0xe1
//#define SBPRO_SPEAKER_ENABLE		0xd1
//#define SBPRO_SPEAKER_DISABLE		0xd3
//#define SBPRO_TIME_CONSTANT			0x40
//#define SBPRO_TRANSFER_SIZE			0x48
//#define SBPRO_PLAYBACK_8_BIT		0x90
//#define SBPRO_EXIT_DMA				0xda

/*mixer commands*/
#define ESS_ADC_GAIN				0x69
#define ESS_MIC_GAIN				0x68
#define ESS_CD_OUT_GAIN				0x38
#define ESS_MIDI_OUT_GAIN			0x3a
#define ESS_LINE_OUT_GAIN			0x3e
#define ESS_DAC_OUT_GAIN			0x14
#define ESS_MASTER_GAIN				0x32
//#define SBPRO_VOICE					0x04
//#define SBPRO_MIC					0x0a
//#define SBPRO_INPUT_MUX				0x0c
//#define SBPRO_OUTPUT_MUX			0x0e
//#define SBPRO_MASTER				0x22
//#define SBPRO_MIDI					0x26
//#define SBPRO_CD					0x28
//#define SBPRO_LINE					0x2e
typedef struct dma_buffer {
	char *data;
	int32 size;
	area_id area;
	} dma_buffer;

dma_buffer write_buffer, read_buffer;
struct isa_module_info *isamod_info;
int dma8;
#endif
