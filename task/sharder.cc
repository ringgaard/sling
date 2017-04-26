#include <vector>

#include "base/types.h"
#include "task/task.h"
#include "util/fingerprint.h"

namespace sling {
namespace task {

// Shard input messages according to key fingerprint.
class SharderTask : public Processor {
 public:
  void Start(Task *task) override {
    // Get output shard channels.
    shards_ = task->GetSinks("output");
  }

  void Receive(Channel *channel, Message *message) override {
    // Compute key fingerprint.
    uint64 fp = Fingerprint(message->key().data(), message->key().size());
    int shard = fp % shards_.size();

    // Output message on output shard channel.
    shards_[shard]->Send(message);
  }

 private:
  // Output shard channels.
  std::vector<Channel *> shards_;
};

REGISTER_TASK_PROCESSOR("sharder", SharderTask);

}  // namespace task
}  // namespace sling

