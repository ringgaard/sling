//
// SHA-1 digest function
//

#ifndef SHA1_SHA1_H_
#define SHA1_SHA1_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SHA1_LENGTH 20
#define SHA1_BASE64_LENGTH 30

typedef unsigned char shabuf_t[SHA1_LENGTH];

typedef struct {
  unsigned long total[2];     // number of bytes processed
  unsigned long state[5];     // intermediate digest state
  unsigned char buffer[64];   // data block being processed
  unsigned char ipad[64];     // HMAC: inner padding
  unsigned char opad[64];     // HMAC: outer padding
} sha1_context;

void sha1_start(sha1_context *ctx);
void sha1_update(sha1_context *ctx, const unsigned char *input, int ilen);
void sha1_strupdate(sha1_context *ctx, const char *input, int ilen);
void sha1_finish(sha1_context *ctx, shabuf_t sha);
int sha1_finish_base64(sha1_context *ctx, char *base64);

#ifdef __cplusplus
}
#endif

#endif  // SHA1_SHA1_H_

