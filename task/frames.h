#ifndef TASK_FRAMES_H_
#define TASK_FRAMES_H_

#include "frame/object.h"
#include "task/message.h"
#include "task/task.h"

namespace sling {
namespace task {

// Task processor for receiving and sending frames.
class FrameProcessor : public Processor {
 public:
  ~FrameProcessor() { delete commons_; }

  // Task processor implementation.
  void Start(Task *task) override;
  void Receive(Channel *channel, Message *message) override;
  void Done(Task *task);

  // Called to initialize frame processor.
  virtual void Startup(Task *task);

  // Called for each frame received on input.
  virtual void Process(Slice key, const Frame &frame);

  // Called when all frames have been received.
  virtual void Flush(Task *task);

  // Output object to output.
  void Output(Text key, const Object &value);

  // Output frame to output using frame id as key.
  void Output(const Frame &frame);

  // Output shallow encoding of frame to output.
  void OutputShallow(Text key, const Object &value);
  void OutputShallow(const Frame &frame);

  // Return output channel.
  Channel *output() const { return output_; }

 protected:
  // Commons store for messages.
  Store *commons_ = nullptr;

  // Name bindings.
  Names names_;

  // Output channel (optional).
  Channel *output_;

  // Statistics.
  Counter *frame_memory_;
  Counter *frame_handles_;
  Counter *frame_symbols_;
  Counter *frame_gcs_;
  Counter *frame_gctime_;
};

// Create message from object.
Message *CreateMessage(Text key, const Object &Object, bool shallow = false);

// Create message with encoded frame using frame id as key.
Message *CreateMessage(const Frame &frame, bool shallow = false);

// Decode message as frame.
Frame DecodeMessage(Store *store, Message *message);

// Load repository into store from input file.
void LoadStore(Store *store, Resource *file);

}  // namespace task
}  // namespace sling

#endif  // TASK_FRAMES_H_

