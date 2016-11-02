#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int yes = 1;
  int ret;
  int len;
  char *buf;

  struct sockaddr_storage their_addr;
  socklen_t addr_size;
  struct addrinfo hints;
  struct addrinfo *res;

  int sockfd, new_fd;

  if (argc != 2) {
      printf("usage: local_tcp_client server_port\n");
      return 1;
  }

  // presuming received chunks are under 1KB in size
  buf = (char *)malloc(1000);
  if (buf == NULL) {
      perror("malloc");
      return 1;
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if ((ret = getaddrinfo("127.0.0.1", argv[1], &hints, &res)) != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
      return 1;
  }

  if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1) {
      fprintf(stderr, "socket creation failed\n");
      return 1;
  }

  if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
      fprintf(stderr, "connect failed\n");
      return 1;
  }

  len = read(sockfd, buf, 1000);
  if (len == -1) {
      fprintf(stderr, "read finished/failed\n");
      return 1;
  }

  printf("read: %s\n", buf);
  return 0;
}
