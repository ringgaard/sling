#include "task/task.h"

namespace sling {
namespace task {

// Silently discard all incoming messages.
class NullSink : public Processor {
 public:
  void Receive(Channel *channel, Message *message) override {
    delete message;
  }
};

REGISTER_TASK_PROCESSOR("null", NullSink);

}  // namespace task
}  // namespace sling

