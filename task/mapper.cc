#include "task/mapper.h"

#include "base/logging.h"

namespace sling {
namespace task {

void Mapper::Start(Task *task) {
  // Get output channel.
  output_ = task->GetSink("output");
  if (output_ == nullptr) {
    LOG(ERROR) << "No output channel";
    return;
  }
}

void Mapper::Receive(Channel *channel, Message *message) {
  // Call Map() method on each input message.
  MapInput input(message->key(), message->value());
  Map(input);

  // Delete input message.
  delete message;
}

void Mapper::Done(Task *task) {
  // Close output channel.
  if (output_ != nullptr) output_->Close();
}

void Mapper::Output(Slice key, Slice value) {
  // Ignore if there is no output.
  if (output_ == nullptr) return;

  // Create new message and send it on the output channel.
  Message *message = new Message(key, value);
  output_->Send(message);
}

}  // namespace task
}  // namespace sling

