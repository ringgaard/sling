#ifndef TASK_ACCUMULATOR_H_
#define TASK_ACCUMULATOR_H_

#include <string>
#include <vector>

#include "base/types.h"
#include "task/message.h"
#include "task/reducer.h"
#include "task/task.h"
#include "string/text.h"
#include "util/mutex.h"

namespace sling {
namespace task {

// Accumulator for collecting counts for keys.
class Accumulator {
 public:
  // Initialize accumulator.
  void Init(Channel *output, int num_buckets = 1 << 20);

  // Add counts for key.
  void Increment(Text key, int64 count = 1);

  // Flush remaining counts to output.
  void Flush();

 private:
  // Hash buckets for accumulating counts.
  struct Bucket {
    string key;
    int64 count = 0;
  };
  std::vector<Bucket> buckets_;

  // Output channel for accumulated counts.
  Channel *output_ = nullptr;

  // Mutex for serializing access to accumulator.
  Mutex mu_;
};

// Reducer that outputs the sum of all the values for a key.
class SumReducer : public Reducer {
 public:
  // Sum all the counts for the key and call the output method with the sum.
  void Reduce(const ReduceInput &input) override;

  // Called with aggregate count for key. The default implementation just
  // outputs the key and the sum to the output.
  virtual void Aggregate(int shard, const Slice &key, uint64 sum);
};

}  // namespace task
}  // namespace sling

#endif  // TASK_ACCUMULATOR_H_

