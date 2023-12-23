#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include "helpers.h"

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1;  // error
  }

  fd_set_nb(connfd);
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

static void event_loop(int fd) {
  std::vector<Conn *> fd2conn;

  fd_set_nb(fd);

  std::vector<struct pollfd> poll_args;
  while (true) {
    poll_args.clear();

    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }

      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      poll_args.push_back(pfd);
    }

    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (rv < 0) {
      die("poll");
    }

    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents){
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);

        if (conn->state == STATE_END) {
          fd2conn[conn->fd] = NULL;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }

    if (poll_args[0].revents) {
      (void)accept_new_conn(fd2conn, fd);
    }
  }
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT_SERVER);
  addr.sin_addr.s_addr = INADDR_ANY;
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("bind()");
  }

  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  printf("The server has been started on port %d\n", PORT_SERVER);

  event_loop(fd);

  return 0;
}