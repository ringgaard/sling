#include "task/reducer.h"

namespace sling {
namespace task {

Reducer::~Reducer() {
  for (auto *s : shards_) delete s;
}

void Reducer::Start(Task *task) {
  int num_shards = task->GetSources("input").size();
  shards_.reserve(num_shards);
  for (int i = 0; i < num_shards; ++i) {
    shards_.push_back(new Shard());
  }
  outputs_ = task->GetSinks("output");
}

void Reducer::Receive(Channel *channel, Message *message) {
  int shard = channel->consumer().shard().part();
  Shard *s = shards_[shard];

  MutexLock lock(&s->mu);
  if (s->messages.empty()) {
   s->key = message->key();
  } else if (message->key() != s->key) {
    ReduceShard(shard);
    s->key = message->key();
  }
  s->messages.push_back(message);
}

void Reducer::ReduceShard(int shard) {
  Shard *s = shards_[shard];
  if (s->messages.empty()) return;

  ReduceInput input(shard, s->key, s->messages);
  Reduce(input);
  s->clear();
}

void Reducer::Done(Task *task) {
  for (int shard = 0; shard < shards_.size(); ++shard) {
    ReduceShard(shard);
    delete shards_[shard];
  }
  shards_.clear();
}

void Reducer::Output(int shard, Message *message) {
  outputs_[shard % outputs_.size()]->Send(message);
}

}  // namespace task
}  // namespace sling

