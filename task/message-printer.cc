#include <string>

#include "base/logging.h"
#include "task/task.h"

namespace sling {
namespace task {

// Print incoming messages.
class MessagePrinter : public Processor {
 public:
  void Receive(Channel *channel, Message *message) override {
    LOG(INFO) << "Message on channel " << channel->id()
              << " from " << channel->producer().task()->ToString()
              << " key: " << message->key()
              << " value: " << message->value();
    delete message;
  }
};

REGISTER_TASK_PROCESSOR("printer", MessagePrinter);

}  // namespace task
}  // namespace sling

