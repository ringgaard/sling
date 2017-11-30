#ifndef SLING_HTTP_HTTP_STREAM_H_
#define SLING_HTTP_HTTP_STREAM_H_

#include "sling/http/http-server.h"
#include "sling/stream/stream.h"

namespace sling {

// An InputStream for reading from a HTTP buffer.
class HTTPInputStream : public InputStream {
 public:
  HTTPInputStream(HTTPBuffer *buffer);

  // InputStream interface.
  bool Next(const void **data, int *size) override;
  void BackUp(int count) override;
  bool Skip(int count) override;
  int64 ByteCount() const override;

 private:
  HTTPBuffer *buffer_;
};

// An OutputStream backed by a HTTP buffer.
class HTTPOutputStream : public OutputStream {
 public:
  HTTPOutputStream(HTTPBuffer *buffer, int block_size = 8192);

  // OutputStream interface.
  bool Next(void **data, int *size) override;
  void BackUp(int count) override;
  int64 ByteCount() const override;

 private:
  HTTPBuffer *buffer_;
  int block_size_;
};

}  // namespace sling

#endif  // SLING_HTTP_HTTP_STREAM_H_

