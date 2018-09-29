#ifndef __NETWORK_H__
#define __NETWORK_H__


ret_code_t network_init();
ret_code_t network_unint();
ret_code_t network_listen(uint16_t *port, SOCKET *fd);
ret_code_t network_accept(SOCKET sfd, struct in_addr* addr, SOCKET *cfd);
ret_code_t network_read(SOCKET fd, char *buf, int32_t size);
ret_code_t network_write(SOCKET fd, void *buf, uint32_t size);

#endif