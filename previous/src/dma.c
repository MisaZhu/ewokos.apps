/* NeXT DMA Emulation 
 * Contains informations from QEMU-NeXT
 * NeXT Integrated Channel Processor (ISP) consists of 12 channel processors
 * with 128 bytes internal buffer for each channel.
 * 12 channels:
 * SCSI, Sound in, Sound out, Optical disk, Printer, SCC, DSP, 
 * Ethernet transmit, Ethernet receive, Video, Memory to register, Register to memory
 */

#include "ioMem.h"
#include "ioMemTables.h"
#include "m68000.h"
#include "scsi.h"
#include "esp.h"
#include "mo.h"
#include "scc.h"
#include "sysReg.h"
#include "dma.h"
#include "configuration.h"
#include "ethernet.h"
#include "floppy.h"
#include "printer.h"
#include "snd.h"
#include "dsp.h"
#include "mmu_common.h"
#include "kms.h"
#include "audio.h"

#define LOG_DMA_LEVEL LOG_DEBUG

#define IO_SEG_MASK	0x1FFFF


int get_channel(Uint32 address);
int get_interrupt_type(int channel);
void dma_interrupt(int channel);
void dma_initialize_buffer(int channel, Uint8 offset);


struct {
    Uint8 csr;
    Uint32 saved_next;
    Uint32 saved_limit;
    Uint32 saved_start;
    Uint32 saved_stop;
    Uint32 next;
    Uint32 limit;
    Uint32 start;
    Uint32 stop;
    Uint8 direction;
} dma[12];


/* DMA internal buffers */
#define DMA_BURST_SIZE  16

int espdma_buf_size = 0;
int espdma_buf_limit = 0;
Uint8 espdma_buf[DMA_BURST_SIZE];
int modma_buf_size = 0;
int modma_buf_limit = 0;
Uint8 modma_buf[DMA_BURST_SIZE];


/* Read and write CSR bits for 68030 based NeXT Computer. */

/* read CSR bits */
#define DMA_ENABLE      0x01   /* enable dma transfer */
#define DMA_SUPDATE     0x02   /* single update */
#define DMA_COMPLETE    0x08   /* current dma has completed */
#define DMA_BUSEXC      0x10   /* bus exception occurred */
/* write CSR bits */
#define DMA_SETENABLE   0x01   /* set enable */
#define DMA_SETSUPDATE  0x02   /* set single update */
#define DMA_M2DEV       0x00   /* dma from mem to dev */
#define DMA_DEV2M       0x04   /* dma from dev to mem */
#define DMA_CLRCOMPLETE 0x08   /* clear complete conditional */
#define DMA_RESET       0x10   /* clr cmplt, sup, enable */
#define DMA_INITBUF     0x20   /* initialize DMA buffers */

/* CSR masks */
#define DMA_CMD_MASK    (DMA_SETENABLE|DMA_SETSUPDATE|DMA_CLRCOMPLETE|DMA_RESET|DMA_INITBUF)
#define DMA_STAT_MASK   (DMA_ENABLE|DMA_SUPDATE|DMA_COMPLETE|DMA_BUSEXC)


/* Read and write CSR bits for 68040 based Machines.
 * We convert these to 68030 values before using in functions.
 * read CSR bits *
 #define DMA_ENABLE      0x01000000
 #define DMA_SUPDATE     0x02000000
 #define DMA_COMPLETE    0x08000000
 #define DMA_BUSEXC      0x10000000
 * write CSR bits *
 #define DMA_SETENABLE   0x00010000
 #define DMA_SETSUPDATE  0x00020000
 #define DMA_M2DEV       0x00000000
 #define DMA_DEV2M       0x00040000
 #define DMA_CLRCOMPLETE 0x00080000
 #define DMA_RESET       0x00100000
 #define DMA_INITBUF     0x00200000
 */



static inline Uint32 dma_getlong(Uint8 *buf, Uint32 pos) {
	return (buf[pos] << 24) | (buf[pos+1] << 16) | (buf[pos+2] << 8) | buf[pos+3];
}

static inline void dma_putlong(Uint32 val, Uint8 *buf, Uint32 pos) {
	buf[pos] = val >> 24;
	buf[pos+1] = val >> 16;
	buf[pos+2] = val >> 8;
	buf[pos+3] = val;
}


int get_channel(Uint32 address) {
    int channel = address&IO_SEG_MASK;

    switch (channel) {
        case 0x010:  return CHANNEL_SCSI; break;
        case 0x040:  return CHANNEL_SOUNDOUT; break;
        case 0x050:  return CHANNEL_DISK; break;
        case 0x080:  return CHANNEL_SOUNDIN; break;
        case 0x090:  return CHANNEL_PRINTER; break;
        case 0x0c0:  return CHANNEL_SCC; break;
        case 0x0d0:  return CHANNEL_DSP; break;
        case 0x110:  return CHANNEL_EN_TX; break;
        case 0x150:  return CHANNEL_EN_RX; break;
        case 0x180:  return CHANNEL_VIDEO; break;
        case 0x1d0:  return CHANNEL_M2R; break;
        case 0x1c0:  return CHANNEL_R2M; break;
        default:
            return -1;
            break;
    }
}

int get_interrupt_type(int channel) {
    switch (channel) {
        case CHANNEL_SCSI: return INT_SCSI_DMA; break;
        case CHANNEL_SOUNDOUT: return INT_SND_OUT_DMA; break;
        case CHANNEL_DISK: return INT_DISK_DMA; break;
        case CHANNEL_SOUNDIN: return INT_SND_IN_DMA; break;
        case CHANNEL_PRINTER: return INT_PRINTER_DMA; break;
        case CHANNEL_SCC: return INT_SCC_DMA; break;
        case CHANNEL_DSP: return INT_DSP_DMA; break;
        case CHANNEL_EN_TX: return INT_EN_TX_DMA; break;
        case CHANNEL_EN_RX: return INT_EN_RX_DMA; break;
        case CHANNEL_VIDEO: return INT_VIDEO; break;
        case CHANNEL_M2R: return INT_M2R_DMA; break;
        case CHANNEL_R2M: return INT_R2M_DMA; break;
        default:
            return 0;
            break;
    }
}

void DMA_CSR_Read(void) { // 0x02000010, length of register is byte on 68030 based NeXT Computer
    int channel = get_channel(IoAccessCurrentAddress);
    IoMem[IoAccessCurrentAddress & IO_SEG_MASK] = dma[channel].csr;
    IoMem[(IoAccessCurrentAddress+1) & IO_SEG_MASK] = IoMem[(IoAccessCurrentAddress+2) & IO_SEG_MASK] = IoMem[(IoAccessCurrentAddress+3) & IO_SEG_MASK] = 0x00; // just to be sure
}

void DMA_CSR_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress);
    int interrupt = get_interrupt_type(channel);
    Uint8 writecsr = IoMem[IoAccessCurrentAddress & IO_SEG_MASK]|IoMem[(IoAccessCurrentAddress+1) & IO_SEG_MASK]|IoMem[(IoAccessCurrentAddress+2) & IO_SEG_MASK]|IoMem[(IoAccessCurrentAddress+3) & IO_SEG_MASK];

    /* Handle CSR bits */
    dma[channel].direction = writecsr&DMA_DEV2M;

    if (writecsr&DMA_RESET) {
        dma[channel].csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
    }
    if (writecsr&DMA_INITBUF) {
        dma_initialize_buffer(channel, 0);
    }
    if (writecsr&DMA_SETSUPDATE) {
        dma[channel].csr |= DMA_SUPDATE;
    }
    if (writecsr&DMA_SETENABLE) {
        dma[channel].csr |= DMA_ENABLE;
        /* Enable Memory to Memory DMA, if read and write channels are enabled */
        if (channel == CHANNEL_R2M || channel == CHANNEL_M2R) {
            if (dma[channel].next==dma[channel].limit) {
                dma[channel].csr &= ~DMA_ENABLE;
            }
            dma_m2m();
        }
    }
    if (writecsr&DMA_CLRCOMPLETE) {
        dma[channel].csr &= ~DMA_COMPLETE;
    }

    set_interrupt(interrupt, RELEASE_INT); // experimental
}

void DMA_Saved_Next_Read(void) { // 0x02004000
    int channel = get_channel(IoAccessCurrentAddress-0x3FF0);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].saved_next);
}

void DMA_Saved_Next_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF0);
    dma[channel].saved_next = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Saved_Limit_Read(void) { // 0x02004004
    int channel = get_channel(IoAccessCurrentAddress-0x3FF4);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].saved_limit);
}

void DMA_Saved_Limit_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF4);
    dma[channel].saved_limit = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Saved_Start_Read(void) { // 0x02004008
    int channel = get_channel(IoAccessCurrentAddress-0x3FF8);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].saved_start);
}

void DMA_Saved_Start_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FF8);
    dma[channel].saved_start = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Saved_Stop_Read(void) { // 0x0200400c
    int channel = get_channel(IoAccessCurrentAddress-0x3FFC);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].saved_stop);
}

void DMA_Saved_Stop_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x3FFC);
    dma[channel].saved_stop = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Next_Read(void) { // 0x02004010
    int channel = get_channel(IoAccessCurrentAddress-0x4000);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].next);
}

void DMA_Next_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4000);
    dma[channel].next = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Limit_Read(void) { // 0x02004014
    int channel = get_channel(IoAccessCurrentAddress-0x4004);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].limit);
}

void DMA_Limit_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4004);
    dma[channel].limit = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Start_Read(void) { // 0x02004018
    int channel = get_channel(IoAccessCurrentAddress-0x4008);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].start);
}

void DMA_Start_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4008);
    dma[channel].start = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Stop_Read(void) { // 0x0200401c
    int channel = get_channel(IoAccessCurrentAddress-0x400C);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].stop);
}

void DMA_Stop_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x400C);
    dma[channel].stop = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
}

void DMA_Init_Read(void) { // 0x02004210
    int channel = get_channel(IoAccessCurrentAddress-0x4200);
    IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].next);
}

void DMA_Init_Write(void) {
    int channel = get_channel(IoAccessCurrentAddress-0x4200);
    dma[channel].next = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);
    dma_initialize_buffer(channel, dma[channel].next&0xF);
}

/* Initialize DMA internal buffer */

void dma_initialize_buffer(int channel, Uint8 offset) {
    if (offset>0) {
    }
    switch (channel) {
        case CHANNEL_SCSI:
            esp_dma.status = 0x00; /* just a guess */
            espdma_buf_size = 0;
            espdma_buf_limit = offset;
            break;
        case CHANNEL_DISK:
            modma_buf_size = 0;
            modma_buf_limit = offset;
            break;
        default:
            break;
    }
}

/* DMA interrupt functions */

void dma_interrupt(int channel) {
    int interrupt = get_interrupt_type(channel);

    /* If we have reached limit, generate an interrupt and set the flags */
    if (dma[channel].next==dma[channel].limit) {
        dma[channel].csr |= DMA_COMPLETE;
        if (dma[channel].csr & DMA_SUPDATE) { /* if we are in chaining mode */
            dma[channel].next = dma[channel].start;
            dma[channel].limit = dma[channel].stop;
            /* Set bits in CSR */
            dma[channel].csr &= ~DMA_SUPDATE; /* 1st done */
        } else {
            dma[channel].csr &= ~DMA_ENABLE; /* all done */
        }
        set_interrupt(interrupt, SET_INT);
    } else if (dma[channel].csr&DMA_BUSEXC) {
        set_interrupt(interrupt, SET_INT);
    }
}


/* DMA Read and Write Memory Functions */

/* Channel SCSI (shared with floppy drive) */
void dma_esp_write_memory(void) {
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        return;
    }
    if ((dma[CHANNEL_SCSI].limit%DMA_BURST_SIZE) || (dma[CHANNEL_SCSI].next%4)) {
        abort();
    }

    TRY(prb) {
        if (espdma_buf_size>0) {
        }

        while (dma[CHANNEL_SCSI].next<=dma[CHANNEL_SCSI].limit) {
            /* Fill DMA channel FIFO (only if limit < FIFO size) */
            if (espdma_buf_limit<DMA_BURST_SIZE) {
                if (floppy_select) {
                    while (espdma_buf_limit<DMA_BURST_SIZE && flp_buffer.size>0) {
                        espdma_buf[espdma_buf_limit]=flp_buffer.data[flp_buffer.limit-flp_buffer.size];
                        flp_buffer.size--;
                        espdma_buf_limit++;
                        espdma_buf_size++;
                    }
                } else {
                    while (espdma_buf_limit<DMA_BURST_SIZE && esp_counter>0 && SCSIbus.phase==PHASE_DI) {
                        espdma_buf[espdma_buf_limit]=SCSIdisk_Send_Data();
                        esp_counter--;
                        espdma_buf_limit++;
                        espdma_buf_size++;
                    }
                }
            }
            if (espdma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                ESP_DMA_set_status();

                while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit && espdma_buf_size>0) {
                    NEXTMemory_WriteLong(dma[CHANNEL_SCSI].next, dma_getlong(espdma_buf, DMA_BURST_SIZE-espdma_buf_size));
                    dma[CHANNEL_SCSI].next+=4;
                    espdma_buf_size-=4;
                }
                if (espdma_buf_size>0) { /* Not complete, stop */
                    break;
                }
                espdma_buf_limit = espdma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    dma_interrupt(CHANNEL_SCSI);
}

void dma_esp_flush_buffer(void) {
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        return;
    }
    if (dma[CHANNEL_SCSI].direction!=DMA_DEV2M) {
        return;
    }

    TRY(prb) {
        if (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
            if (espdma_buf_size>0) {
                /* Write one long word to memory */
                NEXTMemory_WriteLong(dma[CHANNEL_SCSI].next, dma_getlong(espdma_buf, espdma_buf_limit-espdma_buf_size));
                espdma_buf_size-=4;
            }
            dma[CHANNEL_SCSI].next+=4;
        } else {
        }
    } CATCH(prb) {
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    dma_interrupt(CHANNEL_SCSI);
}

void dma_esp_read_memory(void) {
    if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
        return;
    }
    if ((dma[CHANNEL_SCSI].limit%DMA_BURST_SIZE) || (dma[CHANNEL_SCSI].next%4)) {
        abort();
    }
    TRY(prb) {
        if (espdma_buf_size>0) {
        }
        while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
            /* Read data from memory to DMA channel FIFO (only if limit < FIFO size) */
            if (espdma_buf_limit<DMA_BURST_SIZE) {
                while (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit && espdma_buf_limit<DMA_BURST_SIZE) {
                    dma_putlong(NEXTMemory_ReadLong(dma[CHANNEL_SCSI].next), espdma_buf, espdma_buf_limit);
                    dma[CHANNEL_SCSI].next+=4;
                    espdma_buf_limit+=4;
                    espdma_buf_size+=4;
                }
            }
            if (espdma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                ESP_DMA_set_status();

                if (floppy_select) {
                    while (espdma_buf_size>0 && flp_buffer.size<flp_buffer.limit) {
                        flp_buffer.data[flp_buffer.size]=espdma_buf[espdma_buf_limit-espdma_buf_size];
                        flp_buffer.size++;
                        espdma_buf_size--;
                    }
                } else {
                    while (espdma_buf_size>0 && esp_counter>0 && SCSIbus.phase==PHASE_DO) {
                        SCSIdisk_Receive_Data(espdma_buf[espdma_buf_limit-espdma_buf_size]);
                        esp_counter--;
                        espdma_buf_size--;
                    }
                }
                if (espdma_buf_size>0) { /* Not complete, stop */
                    break;
                }
                espdma_buf_limit = espdma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
        dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    if ((floppy_select && flp_buffer.size<flp_buffer.limit) || SCSIbus.phase==PHASE_DO) {
        if (espdma_buf_size!=0) {
        }
    }
    dma_interrupt(CHANNEL_SCSI);
}


/* Channel MO */
void dma_mo_write_memory(void) {
    if (!(dma[CHANNEL_DISK].csr&DMA_ENABLE)) {
        return;
    }
    if ((dma[CHANNEL_DISK].limit%DMA_BURST_SIZE) || (dma[CHANNEL_DISK].next%4)) {
        abort();
    }
    TRY(prb) {
        if (modma_buf_size>0) {
        }
        while (dma[CHANNEL_DISK].next<=dma[CHANNEL_DISK].limit) {
            /* Fill DMA channel FIFO (only if limit < FIFO size) */
            if (modma_buf_limit<DMA_BURST_SIZE) {
                while (modma_buf_limit<DMA_BURST_SIZE && ecc_buffer[eccout].size>0) {
                    modma_buf[modma_buf_limit]=ecc_buffer[eccout].data[ecc_buffer[eccout].limit-ecc_buffer[eccout].size];
                    ecc_buffer[eccout].size--;
                    modma_buf_limit++;
                    modma_buf_size++;
                }
            }
            if (modma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit && modma_buf_size>0) {
                    NEXTMemory_WriteLong(dma[CHANNEL_DISK].next, dma_getlong(modma_buf, DMA_BURST_SIZE-modma_buf_size));
                    dma[CHANNEL_DISK].next+=4;
                    modma_buf_size-=4;
                }
                if (modma_buf_size>0) { /* Not complete, stop */
                    break;
                }
                modma_buf_limit = modma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        dma[CHANNEL_DISK].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DISK].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    dma_interrupt(CHANNEL_DISK);
}

void dma_mo_read_memory(void) {
    if (!(dma[CHANNEL_DISK].csr&DMA_ENABLE)) {
        return;
    }
    if ((dma[CHANNEL_DISK].limit%DMA_BURST_SIZE) || (dma[CHANNEL_DISK].next%4)) {
        abort();
    }
    TRY(prb) {
        if (modma_buf_size>0) {
        }
        while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit) {
            /* Read data from memory to DMA channel FIFO (only if limit < FIFO size) */
            if (modma_buf_limit<DMA_BURST_SIZE) {
                while (dma[CHANNEL_DISK].next<dma[CHANNEL_DISK].limit && modma_buf_limit<DMA_BURST_SIZE) {
                    dma_putlong(NEXTMemory_ReadLong(dma[CHANNEL_DISK].next), modma_buf, modma_buf_limit);
                    dma[CHANNEL_DISK].next+=4;
                    modma_buf_limit+=4;
                    modma_buf_size+=4;
                }
            }
            if (modma_buf_limit<DMA_BURST_SIZE) { /* Not complete, stop */
                break;
            } else { /* Empty DMA channel FIFO (only if limit reached FIFO size) */
                while (modma_buf_size>0 && ecc_buffer[eccin].size<ecc_buffer[eccin].limit) {
                    ecc_buffer[eccin].data[ecc_buffer[eccin].size]=modma_buf[modma_buf_limit-modma_buf_size];
                    ecc_buffer[eccin].size++;
                    modma_buf_size--;
                }
                if (modma_buf_size>0) { /* Not complete, stop */
                    break;
                }
                modma_buf_limit = modma_buf_size; /* Should be 0 */
            }
        }
    } CATCH(prb) {
        dma[CHANNEL_DISK].csr &= ~DMA_ENABLE;
        dma[CHANNEL_DISK].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    if (ecc_buffer[eccin].size<ecc_buffer[eccin].limit) {
        if (modma_buf_size!=0) {
        }
    }
    dma_interrupt(CHANNEL_DISK);
}


Uint8* dma_sndout_read_memory(int* len) {
    int i;
    Uint8* result = NULL;
    *len          = 0;
    if (dma[CHANNEL_SOUNDOUT].csr&DMA_ENABLE) {
        if ((dma[CHANNEL_SOUNDOUT].limit&3) || (dma[CHANNEL_SOUNDOUT].next&3)) {
            dma[CHANNEL_SOUNDOUT].next &= ~3;
            dma[CHANNEL_SOUNDOUT].limit &= ~3;
        }
        TRY(prb) {
            *len   = dma[CHANNEL_SOUNDOUT].limit - dma[CHANNEL_SOUNDOUT].next;
            if (*len <= 0) {
                /* EwokOS: guest programmed limit below next; nothing to do */
                *len = 0;
            } else {
                result = malloc(*len * 2);
                if (result == NULL) {
                    /* EwokOS: out of memory; report an underrun instead
                     * of dereferencing the NULL buffer below */
                    *len = 0;
                } else {
                    for(i = 0; dma[CHANNEL_SOUNDOUT].next<dma[CHANNEL_SOUNDOUT].limit; dma[CHANNEL_SOUNDOUT].next++, i++)
                        result[i] = NEXTMemory_ReadByte(dma[CHANNEL_SOUNDOUT].next);
                }
            }
        } CATCH(prb) {
            dma[CHANNEL_SOUNDOUT].csr &= ~DMA_ENABLE;
            dma[CHANNEL_SOUNDOUT].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
    }
    return result;
}

void dma_sndout_intr() {
    if (dma[CHANNEL_SOUNDOUT].csr&DMA_ENABLE) {
        dma_interrupt(CHANNEL_SOUNDOUT);
    }
}

int dma_sndin_write_memory() {
	int value = 0;
    if (dma[CHANNEL_SOUNDIN].csr&DMA_ENABLE) {
		Audio_Input_Lock();


		TRY(prb) {
            while (dma[CHANNEL_SOUNDIN].next<dma[CHANNEL_SOUNDIN].limit) {
                value = Audio_Input_Read();
				if (value < 0) {
					break;
				}
				NEXTMemory_WriteByte(dma[CHANNEL_SOUNDIN].next, value);
				dma[CHANNEL_SOUNDIN].next++;
            }
        } CATCH(prb) {
            dma[CHANNEL_SOUNDIN].csr &= ~DMA_ENABLE;
            dma[CHANNEL_SOUNDIN].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
		Audio_Input_Unlock();

        dma[CHANNEL_SOUNDIN].saved_limit = dma[CHANNEL_SOUNDIN].next;
        dma_interrupt(CHANNEL_SOUNDIN);
		return (dma[CHANNEL_SOUNDIN].next==dma[CHANNEL_SOUNDIN].limit);
    }
	return 1;
}

/* Channel Printer */
void dma_printer_read_memory(void) {
    if (dma[CHANNEL_PRINTER].csr&DMA_ENABLE) {
        if ((dma[CHANNEL_PRINTER].limit%4) || (dma[CHANNEL_PRINTER].next%4)) {
            abort();
        }
        TRY(prb) {
            while (dma[CHANNEL_PRINTER].next<dma[CHANNEL_PRINTER].limit && lp_buffer.size<lp_buffer.limit) {
                lp_buffer.data[lp_buffer.size]=NEXTMemory_ReadByte(dma[CHANNEL_PRINTER].next);
                lp_buffer.size++;
                dma[CHANNEL_PRINTER].next++;
            }
        } CATCH(prb) {
            dma[CHANNEL_PRINTER].csr &= ~DMA_ENABLE;
            dma[CHANNEL_PRINTER].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
        dma_interrupt(CHANNEL_PRINTER);
    }
}


/* Channel Ethernet (this channel does not use DMA buffering) */
#define EN_EOP      0x80000000 /* end of packet */
#define EN_BOP      0x40000000 /* beginning of packet */
#define ENADDR(x)   ((x)&~(EN_EOP|EN_BOP))

Uint32 saved_next_turbo = 0;

static void dma_enet_interrupt(int channel) {
    int interrupt = get_interrupt_type(channel);
    dma[channel].csr |= DMA_COMPLETE;
    if (dma[channel].csr & DMA_SUPDATE) { /* if we are in chaining mode */
        /* Update pointers */
		saved_next_turbo = dma[channel].next;
        dma[channel].next = dma[channel].start;
        dma[channel].limit = dma[channel].stop;
        /* Set bits in CSR */
        dma[channel].csr &= ~DMA_SUPDATE; /* 1st done */
    } else {
        dma[channel].csr &= ~DMA_ENABLE; /* all done */
    }
    set_interrupt(interrupt, SET_INT);
}

void dma_enet_write_memory(bool eop) {
    if (!(dma[CHANNEL_EN_RX].csr&DMA_ENABLE)) {
        return;
    }
    if ((dma[CHANNEL_EN_RX].limit%DMA_BURST_SIZE) || (dma[CHANNEL_EN_RX].next%DMA_BURST_SIZE)) {
        abort();
    }
    TRY(prb) {
        while (dma[CHANNEL_EN_RX].next<dma[CHANNEL_EN_RX].limit && enet_rx_buffer.size>0) {
            NEXTMemory_WriteByte(dma[CHANNEL_EN_RX].next, enet_rx_buffer.data[enet_rx_buffer.limit-enet_rx_buffer.size]);
            enet_rx_buffer.size--;
            dma[CHANNEL_EN_RX].next++;
        }
    } CATCH(prb) {
        dma[CHANNEL_EN_RX].csr &= ~DMA_ENABLE;
        dma[CHANNEL_EN_RX].csr |= (DMA_COMPLETE|DMA_BUSEXC);
    } ENDTRY
    if (enet_rx_buffer.size==0) {
        if (eop) { /* TODO: check if this is correct */
            dma[CHANNEL_EN_RX].next|=EN_BOP;
        }
        dma[CHANNEL_EN_RX].saved_limit = dma[CHANNEL_EN_RX].next;
    }

    dma_enet_interrupt(CHANNEL_EN_RX);
}

bool dma_enet_read_memory(void) {
    if (dma[CHANNEL_EN_TX].csr&DMA_ENABLE) {
        TRY(prb) {
            while (dma[CHANNEL_EN_TX].next<ENADDR(dma[CHANNEL_EN_TX].limit) && enet_tx_buffer.size<enet_tx_buffer.limit) {
                enet_tx_buffer.data[enet_tx_buffer.size]=NEXTMemory_ReadByte(dma[CHANNEL_EN_TX].next);
                enet_tx_buffer.size++;
                dma[CHANNEL_EN_TX].next++;
            }
        } CATCH(prb) {
            dma[CHANNEL_EN_TX].csr &= ~DMA_ENABLE;
            dma[CHANNEL_EN_TX].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
        if (dma[CHANNEL_EN_TX].limit&EN_EOP) {
            dma_enet_interrupt(CHANNEL_EN_TX);
            return true;
        }
        dma_enet_interrupt(CHANNEL_EN_TX);
    }
    return false;
}


/* Memory to Memory */

Uint32 m2m_buffer[DMA_BURST_SIZE];
int m2m_buffer_size;

void M2MDMA_IO_Handler(void) {
    CycInt_AcknowledgeInterrupt();
    if (dma[CHANNEL_R2M].csr&DMA_ENABLE) {
        dma_m2m_write_memory();
        CycInt_AddRelativeInterruptCycles(4, INTERRUPT_M2M_IO);
    }
}

void dma_m2m(void) {
    if ((dma[CHANNEL_M2R].csr&DMA_ENABLE) && (dma[CHANNEL_R2M].csr&DMA_ENABLE)) {
        if (((dma[CHANNEL_R2M].limit-dma[CHANNEL_R2M].next)%DMA_BURST_SIZE) ||
            ((dma[CHANNEL_M2R].limit-dma[CHANNEL_M2R].next)%DMA_BURST_SIZE)) {
            return;
        }
        CycInt_AddRelativeInterruptCycles(4, INTERRUPT_M2M_IO);
    }
}

void dma_m2m_write_memory(void) {
    if (dma[CHANNEL_R2M].next<dma[CHANNEL_R2M].limit) {

        if (dma[CHANNEL_M2R].next<dma[CHANNEL_M2R].limit) {
            /* (Re)fill the buffer, if there is still data to read */
            m2m_buffer_size = 0;

            TRY(prb) {
                while (m2m_buffer_size < DMA_BURST_SIZE) {
                    m2m_buffer[m2m_buffer_size]=NEXTMemory_ReadByte(dma[CHANNEL_M2R].next);
                    m2m_buffer_size++;
                    dma[CHANNEL_M2R].next++;
                }
            } CATCH(prb) {
                dma[CHANNEL_M2R].csr &= ~DMA_ENABLE;
                dma[CHANNEL_M2R].csr |= (DMA_COMPLETE|DMA_BUSEXC);
            } ENDTRY
            dma_interrupt(CHANNEL_M2R);
        } else {
            /* Re-use data in buffer */
            m2m_buffer_size = DMA_BURST_SIZE;
        }
        TRY(prb) {
            /* Write the contents of the buffer to memory */
            while (m2m_buffer_size > 0) {
                NEXTMemory_WriteByte(dma[CHANNEL_R2M].next, m2m_buffer[DMA_BURST_SIZE-m2m_buffer_size]);
                m2m_buffer_size--;
                dma[CHANNEL_R2M].next++;
            }
        } CATCH(prb) {
            dma[CHANNEL_R2M].csr &= ~DMA_ENABLE;
            dma[CHANNEL_R2M].csr |= (DMA_COMPLETE|DMA_BUSEXC);
        } ENDTRY
    }
    dma_interrupt(CHANNEL_R2M);
}


/* Channel DSP */
#define LOG_DMA_DSP_LEVEL	LOG_DEBUG

void dma_dsp_write_memory(Uint8 val) {
	if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE)) {
		return;
	}
	TRY(prb) {
		if (dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit) {
			NEXTMemory_WriteByte(dma[CHANNEL_DSP].next, val);
			dma[CHANNEL_DSP].next++;
		}
	} CATCH(prb) {
		dma[CHANNEL_DSP].csr &= ~DMA_ENABLE;
		dma[CHANNEL_DSP].csr |= (DMA_COMPLETE|DMA_BUSEXC);
	} ENDTRY
	if (dma[CHANNEL_DSP].next==dma[CHANNEL_DSP].limit) {
		DSP_SetIRQB();
		dma_interrupt(CHANNEL_DSP);
	}
}

Uint8 dma_dsp_read_memory(void) {
	Uint8 val = 0;
	if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE)) {
		return val;
	}
	TRY(prb) {
		if (dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit) {
			val = NEXTMemory_ReadByte(dma[CHANNEL_DSP].next);
			dma[CHANNEL_DSP].next++;
		}
	} CATCH(prb) {
		dma[CHANNEL_DSP].csr &= ~DMA_ENABLE;
		dma[CHANNEL_DSP].csr |= (DMA_COMPLETE|DMA_BUSEXC);
	} ENDTRY
	if (dma[CHANNEL_DSP].next==dma[CHANNEL_DSP].limit) {
		DSP_SetIRQB();
		dma_interrupt(CHANNEL_DSP);
	}
	return val;
}

bool dma_dsp_ready(void) {
	if (!(dma[CHANNEL_DSP].csr&DMA_ENABLE) ||
		!(dma[CHANNEL_DSP].next<dma[CHANNEL_DSP].limit)) {
		return false;
	} else {
		return true;
	}
}


/* ---------------------- DMA Scratchpad ---------------------- */

/* This is used to interrupt at vertical screen retrace.
 * TODO: find out how the interrupt is generated in real
 * hardware using the Limit register of the DMA chip.
 * (0xEA * 1024 = visible videomem size)
 */


/* Interrupt Handler (called from Video_InterruptHandler in video.c) */
void dma_video_interrupt(void) {
    if (dma[CHANNEL_VIDEO].limit==0xEA) {
        set_interrupt(INT_VIDEO, SET_INT); /* interrupt is released by writing to CSR */
    } else if (dma[CHANNEL_VIDEO].limit && dma[CHANNEL_VIDEO].limit!=0xEA) {
        abort();
    }
}


/* FIXME: This is just for passing power-on test. Add real SCC channel later. */

void dma_scc_read_memory(void) {
    while (dma[CHANNEL_SCC].next<dma[CHANNEL_SCC].limit) {
        scc_buf[0]=NEXTMemory_ReadByte(dma[CHANNEL_SCC].next);
        dma[CHANNEL_SCC].next++;
    }
    dma_interrupt(CHANNEL_SCC);
}


/* DMA CSR on Turbo systems */

/* CSR read bits */
#define TDMA_BYTECOUNT_MASK	0x00000007
#define TDMA_WRITEPTR_MASK	0x00000018
#define TDMA_READPTR_MASK	0x00000060
#define TDMA_DIRTY_MASK		0x00000180
#define TDMA_BUFSEL			0x00000200

#define TDMA_ENABLE			0x01000000
#define TDMA_SUPDATE		0x02000000
#define TDMA_COMPLETE		0x08000000
#define TDMA_BUSEXC			0x10000000

/* CSR write bits */
#define TDMA_SETENABLE		0x00010000
#define TDMA_SETSUPDATE		0x00020000
#define TDMA_DEV2M			0x00040000
#define TDMA_CLRCOMPLETE	0x00080000
#define TDMA_RESET			0x00100000
#define TDMA_SETCOMPLETE	0x00200000
#define TDMA_FLUSH			0x00400000
#define TDMA_BUFRESET		0x00800000

/* CSR masks */
#define TDMA_CMD_MASK    0x00FB0000

void TDMA_CSR_Read(void) { // 0x02000010, length of register is byte on 68030 based NeXT Computer
	int channel = get_channel(IoAccessCurrentAddress);
	IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, dma[channel].csr<<24);

}

void TDMA_CSR_Write(void) {
	int channel = get_channel(IoAccessCurrentAddress);
	int interrupt = get_interrupt_type(channel);
	Uint32 writecsr = IoMem_ReadLong(IoAccessCurrentAddress & IO_SEG_MASK);

	/* Handle CSR bits */
	dma[channel].direction = (writecsr>>16)&DMA_DEV2M;
	if (writecsr&TDMA_RESET) {
		dma[channel].csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
	}
	if (writecsr&TDMA_BUFRESET) {
		dma_initialize_buffer(channel, 0);
	}
	if (writecsr&TDMA_SETSUPDATE) {
		dma[channel].csr |= DMA_SUPDATE;
	}
	if (writecsr&TDMA_SETENABLE) {
		dma[channel].csr |= DMA_ENABLE;
	}
	if (writecsr&TDMA_CLRCOMPLETE) {
		dma[channel].csr &= ~DMA_COMPLETE;
	}
	set_interrupt(interrupt, RELEASE_INT);
}

void TDMA_Saved_Next_Read(void) { // 0x02004050
	IoMem_WriteLong(IoAccessCurrentAddress & IO_SEG_MASK, saved_next_turbo);
}

/* Flush DMA buffer */
/* FIXME: Implement function for all buffered channels */
void tdma_flush_buffer(int channel) {
	int i;
	if (!(dma[CHANNEL_SCSI].csr&DMA_ENABLE)) {
		return;
	}
	if (dma[CHANNEL_SCSI].direction!=DMA_DEV2M) {
		return;
	}
	TRY(prb) {
		for (i = 0; i < DMA_BURST_SIZE; i+=4) {
			if (dma[CHANNEL_SCSI].next<dma[CHANNEL_SCSI].limit) {
				if (espdma_buf_size) {
					NEXTMemory_WriteLong(dma[CHANNEL_SCSI].next, dma_getlong(espdma_buf, espdma_buf_limit-espdma_buf_size));
					espdma_buf_size-=4;
				}
				dma[CHANNEL_SCSI].next+=4;
			}
		}
	} CATCH(prb) {
		dma[CHANNEL_SCSI].csr &= ~DMA_ENABLE;
		dma[CHANNEL_SCSI].csr |= (DMA_COMPLETE|DMA_BUSEXC);
	} ENDTRY
	dma_interrupt(CHANNEL_SCSI);
}
