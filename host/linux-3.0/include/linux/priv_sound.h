#ifndef _PRIV_SOUND_H_
#define _PRIV_SOUND_H_

void privsound_buf_to_hw(void *buf, ssize_t frame_size, unsigned int size);
void privsound_encrypt_input(void *buf, ssize_t size);
void set_DMA_buffer_address(char *dmabuf, long length);

#endif
