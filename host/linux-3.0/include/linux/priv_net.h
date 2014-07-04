#ifndef _PRIV_NET_H_
#define _PRIV_NET_H_


void* privnet_find_mac(unsigned char *mac);
void privnet_encrypt(unsigned char *data, int len, void* idptr);
void privnet_decrypt(unsigned char *data, int len, void* idptr);

#endif
