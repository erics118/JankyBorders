#pragma once
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// client for the yabai scripting addition, which runs inside Dock.app
// framing and opcodes match src/osax/common.h in erics118/yabai
#define YABAI_SA_SOCKET_PATH_FMT "/tmp/yabai-sa_%s.socket"
#define YABAI_SA_OPCODE_HANDSHAKE 0x01
#define YABAI_SA_OPCODE_WINDOW_ORDERING_GROUP 0x14
#define YABAI_SA_ATTRIB_ORDERING_GROUP 0x80

static inline int yabai_sa_connect(void) {
  struct passwd* pw = getpwuid(getuid());
  if (!pw) return -1;

  struct sockaddr_un address = { .sun_family = AF_UNIX };
  snprintf(address.sun_path,
           sizeof(address.sun_path),
           YABAI_SA_SOCKET_PATH_FMT,
           pw->pw_name             );

  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) return -1;

  struct timeval timeout = { .tv_sec = 0, .tv_usec = 200000 };
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (connect(sockfd, (struct sockaddr*)&address, sizeof(address)) == -1) {
    close(sockfd);
    return -1;
  }
  return sockfd;
}

// a message is an int16 length of what follows, the opcode, then the arguments
static inline bool yabai_sa_send(char opcode, void* args, int16_t args_length, char* reply, int reply_size) {
  char bytes[64];
  int16_t length = 1 + args_length;
  if (sizeof(length) + length > sizeof(bytes)) return false;

  memcpy(bytes, &length, sizeof(length));
  bytes[sizeof(length)] = opcode;
  if (args_length) memcpy(bytes + sizeof(length) + 1, args, args_length);

  int sockfd = yabai_sa_connect();
  if (sockfd == -1) return false;

  bool sent = send(sockfd, bytes, sizeof(length) + length, 0) != -1;
  int received = recv(sockfd, reply, reply_size, 0);
  close(sockfd);
  return sent && (!reply_size || received > 0);
}

static inline bool yabai_sa_supports_ordering_group(void) {
  // the reply is the version string, a null byte, then a uint32 attribute mask
  char reply[128] = {};
  if (!yabai_sa_send(YABAI_SA_OPCODE_HANDSHAKE, NULL, 0, reply, sizeof(reply) - 1)) {
    return false;
  }

  size_t version_length = strnlen(reply, sizeof(reply) - 1);
  if (version_length + 1 + sizeof(uint32_t) >= sizeof(reply)) return false;

  uint32_t attrib;
  memcpy(&attrib, reply + version_length + 1, sizeof(attrib));
  return attrib & YABAI_SA_ATTRIB_ORDERING_GROUP;
}

// order 1 keeps child directly above parent whenever parent is reordered
// order 0 takes child out of its group
static inline bool yabai_sa_ordering_group(uint32_t parent_wid, uint32_t child_wid, int order) {
  char args[2 * sizeof(uint32_t) + sizeof(int)];
  memcpy(args, &parent_wid, sizeof(uint32_t));
  memcpy(args + sizeof(uint32_t), &child_wid, sizeof(uint32_t));
  memcpy(args + 2 * sizeof(uint32_t), &order, sizeof(int));

  char reply;
  return yabai_sa_send(YABAI_SA_OPCODE_WINDOW_ORDERING_GROUP, args, sizeof(args), &reply, 0);
}
