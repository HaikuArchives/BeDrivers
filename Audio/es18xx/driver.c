/*	driver.c*/
/*	Sound Blaster Pro*/
/*	Joseph Wang*/
/*	4.4.99*/
/*	driver stub for setting up and tearing down driver*/

#include <OS.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <config_manager.h>
#include <ISA.h>
#include <isapnp.h>
#include <stdlib.h>
#include <string.h>
#include "ess.h"
#include "driver.h"
#include "pcm.h"

//#define kprintf


spinlock lock;
cpu_status status;
int port, irq, dma16, mpu;
int32 hold_value;
sem_id write_sem;
sem_id write_sync_sem;


int32 api_version = B_CUR_DRIVER_API_VERSION;

const char *dev_name[] = {
	"audio/raw/es18xx/1",
	"audio/old/es18xx/1",
	NULL
	};

device_hooks dev_hook[] = {
	{	pcm_open,
		pcm_close,
		pcm_free,
		pcm_control,
		pcm_read,
		pcm_write,
		NULL,NULL,NULL,NULL
		},
	{	pcm_open,
		pcm_close,
		pcm_free,
		pcm_control,
		pcm_read,
		pcm_write,
		NULL,NULL,NULL,NULL
		}
	};

/*driver functions*/

/*execute first time driver is loaded*/
status_t init_hardware() {
	return B_OK;
	}

/*execute each time driver is loaded*/
status_t init_driver() {
	status_t err;
	physical_entry table;
	uint64 cookie=0;
	struct device_info info;
	int32 result, num;
	struct device_configuration *dev_config;
	config_manager_for_driver_module_info *config_info;
	resource_descriptor resource;
/*	create dma buffers*/
	err=write_buffer.area = create_area("audio buffer", (void **)&write_buffer.data,
			B_ANY_KERNEL_ADDRESS, DMA_TRANSFER_SIZE, B_LOMEM, B_READ_AREA|B_WRITE_AREA);
	if(get_memory_map(write_buffer.data, write_buffer.size, &table, 1) < B_OK) goto BAD_INIT;
	memset(write_buffer.data, 128, write_buffer.size);
	write_buffer.size=DMA_TRANSFER_SIZE;
	if((uint32)table.address & 0xff000000) goto BAD_INIT;
	if ((((uint32)table.address)+B_PAGE_SIZE) & 0xff000000) goto BAD_INIT;

/*	init config manager*/
	err=get_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME,
			(module_info **)&config_info);
	if(err!=B_OK) return err;

	err=B_ERROR;
/*	check for supported card*/
	while(config_info->get_next_device_info(B_ISA_BUS, &cookie, &info,
			sizeof(info)) == B_OK) {
		struct device_info *dinfo;
		struct isa_info *iinfo;
		if(info.config_status!=B_OK) continue;
		dinfo=(struct device_info *)malloc(info.size);
		config_info->get_device_info_for(cookie, dinfo, info.size);
		iinfo=(struct isa_info *)((char *)dinfo+info.bus_dependent_info_offset);
		if((iinfo->vendor_id&0x00ffffff) != ESS_CARD_ID_MASK) goto END;
		err=B_OK;
		free(dinfo);
		break;
END:
		free(dinfo);
		}
	if(err!=B_OK) {
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return err;
		}
/*	get card configuration*/
	result=config_info->get_size_of_current_configuration_for(cookie);
	if(result<0) return B_ERROR;
	dev_config=malloc(result);
	if(!dev_config) {
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
	if(config_info->get_current_configuration_for(cookie, dev_config, result)!=B_OK) {
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		free(dev_config);
		return B_ERROR;
		}
/*	fill in variables*/
/*	get port*/
	num=config_info->count_resource_descriptors_of_type(dev_config, B_IO_PORT_RESOURCE);
	if(num<=0) {
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
	err=config_info->get_nth_resource_descriptor_of_type(dev_config, 0,
			B_IO_PORT_RESOURCE, &resource, sizeof(resource));
	if(err!=B_OK) {
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return err;
		}
	port=resource.d.r.minbase;
	if(num>1) {
		err=config_info->get_nth_resource_descriptor_of_type(dev_config, 1,
				B_IO_PORT_RESOURCE, &resource, sizeof(resource));
		if(err!=B_OK) {
			free(dev_config);
			put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
			return err;
			}
		mpu=resource.d.r.minbase;
		}
/*	get irq*/
	num=config_info->count_resource_descriptors_of_type(dev_config, B_IRQ_RESOURCE);
	if(num<=0) {
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
	err=config_info->get_nth_resource_descriptor_of_type(dev_config, 0, B_IRQ_RESOURCE,
			&resource, sizeof(resource));
	result=resource.d.m.mask;
	for(irq=0;; irq++) {
		if((result&1)==1) break;
		result>>=1;
		if(irq<15) continue;
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
/*	get dma*/
	num=config_info->count_resource_descriptors_of_type(dev_config, B_DMA_RESOURCE);
	if(num<=0) {
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
	err=config_info->get_nth_resource_descriptor_of_type(dev_config, 0, B_DMA_RESOURCE,
			&resource, sizeof(resource));
	result=resource.d.m.mask;
	for(dma8=0;; dma8++) {
		if((result&1)==1) break;
		result>>=1;
		if(dma8<7) continue;
		free(dev_config);
		put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
		return B_ERROR;
		}
	if(num>1) {
		err=config_info->get_nth_resource_descriptor_of_type(dev_config, 1, B_DMA_RESOURCE,
				&resource, sizeof(resource));
		result=resource.d.m.mask;
		for(dma16=0;; dma16++) {
			if((result&1)==1) break;
			result>>=1;
			if(dma16<7) continue;
			free(dev_config);
			put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
			return B_ERROR;
			}
		}
	free(dev_config);
	put_module(B_CONFIG_MANAGER_FOR_DRIVER_MODULE_NAME);
/*	init isa*/
	err=get_module(B_ISA_MODULE_NAME, (module_info **)&isamod_info);
	if(err!=B_OK) return err;
/*	init chip*/

	err=check_hw();
	if(err!=B_OK) return err;
	install_io_interrupt_handler(irq, select_inth, NULL, 0);
	write_sem=create_sem(1, "ess18xx write mutex");
    return B_OK;
BAD_INIT:
	delete_area(write_buffer.area);
	return B_ERROR;
	}

const char **publish_devices() {
	return dev_name;
	}

device_hooks *find_device(const char *name) {
	int i;
	for(i=0; dev_name[i]; i++) {
		if(!strncmp(name, dev_name[i], B_OS_NAME_LENGTH)) {
			return &dev_hook[i];
			}
		}
	return NULL;
	}

void uninit_driver() {
	remove_io_interrupt_handler(irq, select_inth, NULL);
	delete_area(write_buffer.area);
	delete_sem(write_sem);
	put_module(B_ISA_MODULE_NAME);
	}

/*support functions*/

status_t check_hw() {
	status_t err;
	err=reset_hw();
	if(err!=B_OK) return err;
/*	lock the 8-bit dma channel*/
   	isamod_info->lock_isa_dma_channel(dma8);
	return B_OK;
	}

status_t reset_hw() {
/*	reset dsp*/
	int i;
	acquire_sl();
	for(i=333; i>=0; i--) {
/*		poll dsp for 100usec, 3usec each poll*/
		uint8 value;
		write_io(ESS_RESET, 3);
//		snooze(ESS_DELAY);
		spin(ESS_DELAY);
		write_io(ESS_RESET, 0);
		if(read_data(&value)!=B_OK) continue;
		if(value==0xaa) {
			write_data(ESS_EXTENDED);
			release_sl();
			return B_OK;
			}
		}
	release_sl();
	return B_ERROR;
	}

void write_io(int offset, uint8 value) {
	isamod_info->write_io_8(port+offset, value);
	}

uint8 read_io(int offset) {
	return isamod_info->read_io_8(port+offset);
	}

status_t write_data(uint8 value) {
	int i;
/*	is bit 7 high?*/
	for(i=33; i>0; i--) {
		if(read_io(ESS_WRITE_BUFFER_STATUS)&0x80) {
/*			snooze(SBPRO_DELAY);*/
			spin(1);
			continue;
			}
		write_io(ESS_WRITE_DATA, value);
		return B_OK;
		}
	return B_ERROR;
	}

status_t read_data(uint8 *value) {
	int i;
/*	is bit 7 high?*/
	for(i=33; i>0; i--) {
		if(read_io(ESS_READ_BUFFER_STATUS)&0x80) {
			*value=read_io(ESS_READ_DATA);
			return B_OK;
			}
			spin(1);
/*		snooze(SBPRO_DELAY);*/
		}
	return B_ERROR;
	}

void write_reg(uint8 reg, uint8 value) {
	write_data(reg);
	write_data(value);
	}

uint8 read_reg(uint8 reg) {
	status_t err;
	uint8 value;
	write_data(0xc0);
	write_data(reg);
	err=read_data(&value);
	return value;
	}

void write_mixer(uint8 reg, uint8 value) {
	write_io(ESS_REG, reg);
	write_io(ESS_DATA, value);
	}

uint8 read_mixer(uint8 reg) {
	write_io(ESS_REG, reg);
	return read_io(ESS_DATA);
	}

void acquire_sl() {
	cpu_status st = disable_interrupts();
	acquire_spinlock(&lock);
	status = st;
	}

void release_sl() {
	cpu_status st = status;
	release_spinlock(&lock);
	restore_interrupts(st);
	}

int32 select_inth(void *data) {
	return pcm_write_inth(data);
	}
