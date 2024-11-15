// Copyright 2023 Ringgaard Research ApS
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "epsonds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SCANNER_NAME        "scanner.ringgaard.com"
#define SCANNER_PORT        1865

#define A4_WIDTH            210
#define A4_HEIGHT           290

#define A5_WIDTH            145
#define A5_HEIGHT           210

#define MARGIN_TOP          1
#define MARGIN_BOTTOM       1
#define MARGIN_LEFT         1
#define MARGIN_RIGHT        1

int scan_dpi = 300;
int scan_color = COLOR_MONO;
int orientation = PORTRAIT;
int paper_size = A4;
int duplex = 0;
int jpeg_quality = 80;      // 00: high compression, 99: high quality

#define ACK 0x06
#define NAK 0x15

#define PAR_OK   1
#define PAR_FAIL 2

// Scanner status.
struct status {
  int par;       // scanner parameter status
  int pst;       // page start
  int pen;       // page end
  int width;     // page width
  int height;    // page hieght
  int side;      // page side (1=front, 2=back)
  int lft;       // pages left report
  int left;      // pages left
  int cancel;    // scanning cancelled
  int error;     // scanner error
  int paperjam;  // paper jam
  int empty;     // document feeder empty
  int notready;  // scannner not ready
};

static int sock = -1;
static int netwait = 0;

static int xbe32toh(void *ptr) {
  return ntohl(*(unsigned int *) ptr);
}

static void xhtobe32(void *ptr, int n) {
  *(unsigned int *) ptr = htonl(n);
}

int scanner_connect() {
  struct hostent *host;
  struct sockaddr_in addr;
  struct timeval tv;

  // Lookup scanner address.
  host = gethostbyname(SCANNER_NAME);
  if (host == NULL) {
    fprintf(stderr, "Unknwon hostname: %s\n", SCANNER_NAME);
    return -1;
  }

  // Create socket.
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return -1;
  }

  // Connect.
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SCANNER_PORT);
  memcpy(&addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);
  if (connect(sock,(struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("connect");
    return -1;
  }

  // Set timeout.
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv,  sizeof(tv));

  return 0;
}

int scanner_disconnect() {
  if (sock != -1) close(sock);
  sock = -1;
  return 0;
}

int scanner_connected() {
  return sock != -1;
}

int scanner_read(unsigned char *buf, int len) {
  int rc;
  int size;
  int left;
  unsigned char header[12];

  // Receive header.
  if (netwait) {
    while (1) {
      rc = recv(sock, header, 12, 0);
      if (rc < 0) {
        if (errno == EAGAIN) continue;
        perror("recv header");
        return -1;
      }
      if (rc == 0) return 0;
      break;
    }
  } else {
    rc = recv(sock, header, 12, 0);
    if (rc < 0) {
      perror("recv header");
      return -1;
    }
  }
  //printf("recv %d bytes header\n", rc);
  if (rc != 12) {
    fprintf(stderr, "short header (%d bytes)\n", rc);
    return -1;
  }

#if 0
  printf("recv hdr:");
  for (int i = 0; i < 12; ++i) printf(" %02X", header[i]);
  printf("\n");
#endif

  // Check header.
  if (header[0] != 'I' || header[1] != 'S') {
    fprintf(stderr, "unexpected header: %02X %02x\n", header[0], header[1]);
    return -1;
  }

  // Get payload size.
  size = xbe32toh(header + 6);
  //printf("recv %d bytes payload\n", size);
  if (size > len) {
    fprintf(stderr, "buffer too small (%d), %d bytes needed\n", len, size);
    return -1;
  }

  // Receive payload.
  left = size;
  while (left > 0) {
    //printf("%d bytes payload left\n", left);
    rc = recv(sock, buf, left, 0);
    //printf("%d bytes payload received\n", rc);
    if (rc <= 0) {
      perror("recv payload");
      return -1;
    }
    left -= rc;
    buf += rc;
  }

  //printf("recv done: %d bytes\n", size);
  return size;
}

int scanner_write(unsigned int cmd, const void *request,
                  int reqlen, int rsplen) {
  unsigned char packet[12 + 8];
  int len;
  int rc;

  memset(packet, 0, 12 + 8);
  packet[0] = 'I';
  packet[1] = 'S';
  packet[2] = cmd >> 8; // packet type
  packet[3] = cmd;      // data type
  packet[5] = 0x0C;     // data offset
  xhtobe32(packet + 6, reqlen);

  // 0x20 passthru
  // 0x21 job control
  if ((cmd >> 8) == 0x20) {
    xhtobe32(packet + 6, reqlen + 8);    // data size (data header + payload)
    xhtobe32(packet + 12, reqlen);       // payload size
    xhtobe32(packet + 12 + 4, rsplen);   // expected answer size
  }

  // Send header (and data header).
  len = ((cmd >> 8) == 0x20 && (reqlen || rsplen)) ? 12 + 8 : 12;
  //printf("send %d bytes header\n", len);
  rc = send(sock, packet, len, 0);
  if (rc <= 0) {
    perror("send");
    return -1;
  }

  // Send payload.
  if (reqlen) {
    //printf("send %d bytes playload\n", reqlen);
    rc = send(sock, request, reqlen, 0);
    if (rc <= 0) {
      perror("send");
      return -1;
    }
  }

  return 0;
}

int scanner_transact(unsigned int cmd,
                     const void *txbuf, int txlen,
                     void *rxbuf, int rxlen) {
  int rc;

  //printf("transact: cmd %04X tx %d bytes, rx %d bytes\n", cmd, txlen, rxlen);
  rc = scanner_write(cmd, txbuf, txlen, rxlen);
  if (rc < 0) return -1;
  //printf("transact: read reply, %d bytes\n", rxlen);
  return scanner_read(reinterpret_cast<unsigned char *>(rxbuf), rxlen);
}

int scanner_control(unsigned int cmd, const char *buf, int len) {
  int rc;
  unsigned char result;

  rc = scanner_transact(cmd, buf, len, &result, 1);
  if (rc < 0) return -1;

  if (len == 0) return 0;
  if (result == ACK) return 0;
  if (result == NAK) return -1;

  fprintf(stderr, "unexpcted control result: 0x%02x\n", result);
  return -1;
}

int scanner_request(void *data, int len) {
  return scanner_transact(0x2000, NULL, 0, data, len);
}

int parse_number(char **ptr) {
  int len = 0;
  char *p = *ptr;
  switch (*p++) {
    case 'd': len = 3; break;
    case 'i': len = 7; break;
    default: return -1;
  }

  int n = 0;
  for (int i = 0; i < len; ++i) {
    int c = *p++;
    if (c == 0) return -1;
    n = n * 10 + (c - '0');
  }
  *ptr = p;
  return n;
}

int parse_status(char *str, struct status *st) {
  char param[4];
  char *p = str;
  memset(st, 0, sizeof(struct status));
  while (*p) {
    while (*p && *p != '#') p++;
    if (!*p) break;
    p++;
    for (int i = 0; i < 3; ++i) {
      if (!*p) return -1;
      param[i] = *p++;
    }
    param[3] = 0;

    if (strcmp(param, "---") == 0) {
      break;
    } if (strcmp(param, "par") == 0) {
      if (strncmp(p, "OK  ", 4) == 0) st->par = PAR_OK;
      if (strncmp(p, "FAIL", 4) == 0) st->par = PAR_FAIL;
    } else if (strcmp(param, "pst") == 0) {
      st->pst = 1;
      st->width = parse_number(&p);
      parse_number(&p);
      st->height = parse_number(&p);
    } else if (strcmp(param, "pen") == 0) {
      st->pen = 1;
    } else if (strcmp(param, "typ") == 0) {
      if (*p == 'A') st->side = 1;
      if (*p == 'B') st->side = 2;
    } else if (strcmp(param, "lft") == 0) {
      st->lft = 1;
      st->left = parse_number(&p);
    } else if (strcmp(param, "atn") == 0) {
      if (strncmp(p, "CAN ", 4) == 0) st->cancel = 1;
    } else if (strcmp(param, "err") == 0) {
      st->error = 1;
      if (strncmp(p, "ADF PJ", 6) == 0) st->paperjam = 1;
    } else if (strcmp(param, "nrd") == 0) {
      st->error = 1;
      if (strncmp(p, "RSVD", 4) == 0) st->notready = 1;
    } else {
      printf("unknown status parameter '%s' in %s\n", param, str);
    }
  }

  return 0;
}

int scanner_cmd(const char *cmd, const char *payload, int len,
                struct status *st) {
  char header[13];  // add extra byte for sprintf() nul termination
  char rsp[65];
  int rc;

  // Build header.
  memset(rsp, 0, 64);
  memset(header, 0, sizeof(header));
  sprintf(header, "%4.4sx%07x", cmd, len);
  //printf("command: %s\n", header);

  // Send request block, request immediate response if there's no payload.
  //printf("cmd: request block\n");
  rc = scanner_transact(0x2000, header, 12, rsp, len > 0 ? 0 : 64);
  if (rc < 0) return -1;

  // Send parameter block, request response.
  if (len) {
    //printf("cmd: param block\n");
    rc = scanner_transact(0x2000, payload, len, rsp, 64);
    if (rc < 0) return -1;
  }

  // Check response.
  rsp[rc] = 0;
  //printf("cmd: check resp: %s\n", rsp);
  if (rc < 4) {
    fprintf(stderr, "short reply to command %s: %d bytes (%02X)\n",
            cmd, rc, rsp[0]);
    return -1;
  }
  if (memcmp(rsp, cmd, 4) != 0) {
    fprintf(stderr, "unexpected reply to command %s: %c%c%c%c\n",
            cmd, rsp[0], rsp[1], rsp[2], rsp[3]);
    return -1;
  }

  // Get response length.
  if (rsp[4] != 'x') return -1;
  if (sscanf(rsp + 5, "%x#", &rc) != 1) return -1;

  // Parse status reply.
  if (st) {
    if (parse_status(rsp + 12, st) < 0) return -1;
  }

  return rc;
}

int scanner_handshake() {
  int rc;
  unsigned char buf[5];

  rc = scanner_read(buf, 5);
  if (rc != 5) return -1;

  return 0;
}

int scanner_lock() {
  const char *buf = "\x01\xa0\x04\x00\x00\x01\x2c";

  if (scanner_control(0x2100, buf, 7) < 0) return -1;
  if (scanner_control(0x2000, "\x1CX", 2) < 0) return -1;
  return 0;
}

int scanner_unlock() {
  scanner_cmd("FIN ", NULL, 0, NULL);
  scanner_control(0x2101, NULL, 0);
  return 0;
}

int scanner_query(const char *cmd) {
  int rc;
  unsigned char *buffer;
  int len;

  rc = scanner_cmd(cmd, NULL, 0, NULL);
  if (rc < 0) return -1;

  len = rc;
  if (len > 0) {
    buffer = reinterpret_cast<unsigned char *>(malloc(len));
    rc = scanner_request(buffer, len);
    if (rc < 0) {
      free(buffer);
      return -1;
    }

    printf("query %s: ", cmd);
    for (int i = 0; i < len; ++i) {
      if (buffer[i] == '#') printf("\n");
      printf("%c", buffer[i] < ' ' ? '.' : buffer[i]);
    }
    printf("\n");

    free(buffer);
  }

  return 0;
}

int scanner_status(struct status *st) {
  int rc;
  unsigned char *buffer;
  int len;

  rc = scanner_cmd("STAT", NULL, 0, NULL);
  if (rc < 0) return -1;

  memset(st, 0, sizeof(struct status));
  len = rc;
  if (len > 0) {
    buffer = reinterpret_cast<unsigned char *>(malloc(len + 1));
    rc = scanner_request(buffer, len);
    if (rc < 0) {
      free(buffer);
      return -1;
    }
    buffer[len] = 0;

    printf("status %s\n", buffer);
    for (int i = 0; i < len; ++i) {
      if (buffer[i] == '#') {
        if (memcmp(buffer + i, "#ERRADF PE", 10) == 0) {
          st->empty = 1;
        } else {
          st->error = 1;
        }
      }
    }

    free(buffer);
  }

  return 0;
}

int scanner_info() {
  return scanner_query("INFO");
}

int scanner_capa() {
  return scanner_query("CAPA");
}

int scanner_resa() {
  return scanner_query("RESA");
}

int mm_to_pixels(int mm) {
  return mm * scan_dpi * 10 / 254;
}

int scanner_para() {
  char parameters[256];
  int rc;
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;

  if (paper_size == A4) {
    width = mm_to_pixels(A4_WIDTH - MARGIN_LEFT - MARGIN_RIGHT);
    height = mm_to_pixels(A4_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM);
    left = mm_to_pixels(MARGIN_LEFT);
    top = mm_to_pixels(MARGIN_TOP);
  } else if (paper_size == A5) {
    width = mm_to_pixels(A5_WIDTH - MARGIN_LEFT - MARGIN_RIGHT);
    height = mm_to_pixels(A5_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM);
    left = mm_to_pixels((A4_WIDTH - A5_WIDTH) / 2 + MARGIN_LEFT);
    top = mm_to_pixels(MARGIN_TOP);
  }

  // Set up scanning parameters.
  sprintf(parameters,
          "#ADF%s"
          "#COL%s"
          "#FMTJPG "
          "#JPGd%03d"
          "#RSMd%03d"
          "#RSSd%03d"
          "#ACQi%07di%07di%07di%07d",
          duplex ? "DPLX" : "",
          scan_color == COLOR_RGB ? "C024" : "M008",
          jpeg_quality,
          scan_dpi,
          scan_dpi,
          left,
          top,
          width,
          height);

  //printf("scanner parameters: %s\n", parameters);

  // Configure scanner.
  struct status st;
  rc = scanner_cmd("PARA", parameters, strlen(parameters), &st);
  if (rc != 0) return -1;
  if (st.par != PAR_OK) {
    fprintf(stderr, "error setting scanner parameters: %s\n", parameters);
    return -1;
  }

  return 0;
}

int scanner_trtd() {
  return scanner_cmd("TRDT", NULL, 0, NULL);
}

int scanner_img(struct status *st) {
  return scanner_cmd("IMG ", NULL, 0, st);
}

int scanner_can() {
  return scanner_cmd("CAN ", NULL, 0, NULL);
}

int scanner_srdy() {
  return scanner_cmd("SRDY", "#ON ", 4, NULL);
}

int scan_document(const char *page_file_mask, int serial) {
  struct status st;
  int rc;
  int len;
  int page_num = 0;
  int buffer_size = 65536;
  char *buffer = reinterpret_cast<char *>(malloc(buffer_size));
  FILE *f = NULL;

  // Check feeder.
  int ready = 0;
  for (int retry = 0; retry < 15; ++retry) {
    struct status st;
    if (scanner_status(&st) < 0) return -1;
    if (!st.empty) {
      if (retry > 0) sleep(2);
      ready = 1;
      break;
    }
    sleep(1);
  }
  if (!ready) {
    printf("no document in feeder\n");
    return -1;
  }

  // Initialize data transfer.
  if (scanner_trtd() < 0) {
    fprintf(stderr, "error initializing data transfer\n");
    return -1;
  }

  // Request image data.
  while (1) {
    rc = scanner_img(&st);
    if (rc < 0) {
      fprintf(stderr, "error requesting image data\n");
      goto error;
    }


    if (st.pst) {
      // Start of page.
      page_num++;
      char fn[256];
      sprintf(fn, page_file_mask, serial, page_num);
      printf("save page %d to %s\n", page_num, fn);
      f = fopen(fn, "w");
      if (!f) {
        perror(fn);
        goto error;
      }
    }

    if (st.pen) {
      if (f) {
        printf("page %d done\n", page_num);
        fclose(f);
        f = NULL;
      } else {
        fprintf(stderr, "page end with no page start\n");
      }
    }

    if (st.lft && st.left == 0) {
      // Scanning complete.
      break;
    }

    len = rc;
    if (len > 0) {
      if (f) {
        if (len > buffer_size) {
          buffer = reinterpret_cast<char *>(realloc(buffer, len));
          buffer_size = len;
        }
        rc = scanner_request(buffer , len);
        if (rc < 0) goto error;
        if (fwrite(buffer, 1, len, f) != len) goto error;
      } else {
        fprintf(stderr, "received image data before page start\n");
      }
    }

    if (st.error) {
      printf("scanning error\n");
      if (st.paperjam) printf("paper jam\n");
    }

    if (st.cancel) {
      printf("scanning cancelled\n");
      break;
    }
  }

  free(buffer);
  if (f) fclose(f);
  return page_num;

error:
  free(buffer);
  if (f) fclose(f);
  return -1;
}
