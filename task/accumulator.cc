#include "task/accumulator.h"

#include "base/logging.h"
#include "string/numbers.h"
#include "task/reducer.h"
#include "util/fingerprint.h"
#include "util/mutex.h"

namespace sling {
namespace task {

void Accumulator::Init(Channel *output, int num_buckets) {
  output_ = output;
  buckets_.clear();
  buckets_.resize(num_buckets);
}

void Accumulator::Increment(Text key, int64 count) {
  uint64 b = Fingerprint(key.data(), key.size()) % buckets_.size();
  MutexLock lock(&mu_);
  Bucket &bucket = buckets_[b];
  if (key != bucket.key) {
    if (bucket.count != 0) {
      output_->Send(new Message(bucket.key, SimpleItoa(bucket.count)));
      bucket.count = 0;
    }
    bucket.key.assign(key.data(), key.size());
  }
  bucket.count += count;
}

void Accumulator::Flush() {
  MutexLock lock(&mu_);
  for (Bucket &bucket : buckets_) {
    if (bucket.count != 0) {
      output_->Send(new Message(bucket.key, SimpleItoa(bucket.count)));
      bucket.count = 0;
    }
    bucket.key.clear();
  }
}

void SumReducer::Reduce(const ReduceInput &input) {
  int64 sum = 0;
  for (Message *m : input.messages()) {
    int64 count;
    const Slice &value = m->value();
    CHECK(safe_strto64_base(value.data(), value.size(), &count, 10));
    sum += count;
  }
  Aggregate(input.shard(), input.key(), sum);
}

void SumReducer::Aggregate(int shard, const Slice &key, uint64 sum) {
  Output(shard, new Message(key, SimpleItoa(sum)));
}

REGISTER_TASK_PROCESSOR("sum-reducer", SumReducer);

}  // namespace task
}  // namespace sling

