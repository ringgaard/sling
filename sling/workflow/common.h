#ifndef SLING_WORFKLOW_COMMON_H_
#define SLING_WORFKLOW_COMMON_H_

#include <string>
#include <vector>

#include "sling/base/types.h"
#include "sling/string/text.h"
#include "sling/string/strcat.h"
#include "sling/task/container.h"
#include "sling/task/task.h"

namespace sling {

// Lists of resources, tasks, and channels.
typedef std::vector<task::Resource *> Resources;
typedef std::vector<task::Task *> Tasks;
typedef std::vector<task::Channel *> Channels;

// Corpus locations.
struct Corpora {
  // Versions.
  static string wikidata_version() {
    //return "20160718";
    //return "20160926";
    return "20161031";
  }
  static string wikipedia_version() {
    //return "20160701";
    //return "20160920";
    return "20161101";
  }

  // Shared data directory.
  static string root() { return "/var/data"; }

  // Repository file.
  static string google3(Text filename) {
    return StrCat(root(), "/google3/", filename);
  }

  // Corpus directory.
  static string corpus() { return root() + "/corpora"; }

  // Workflow directory.
  static string workflow() { return root() + "/e"; }
  static string workflow(Text name) { return StrCat(root(), "/e/", name); }

  // Wikidata.
  static string wikidata() { return corpus() + "/wikidata"; }
  static string wikidata_dump() {
    return StrCat(wikidata(), "/wikidata-", wikidata_version(),
                  "-all.json.bz2");
  }

  // Wikipedia.
  static string wikipedia() { return corpus() + "/wikipedia"; }
  static string wikipedia_dump(Text language) {
     return StrCat(wikipedia(), "/", language, "wiki-",
            wikipedia_version(), "-pages-articles.xml.bz2");
  }

  // Common crawl.
  static int common_crawl_volumes() { return 3; }

  static string common_crawl_file_list(int volume) {
    return StrCat("/archive/", volume, "/commoncrawl/files.txt");
  }

  static Resources CommonCrawlFiles(task::Container *wf, int volume);

  static std::vector<Resources> CommonCrawl(task::Container *wf);
};

// Resource factory for creating resources in a task container.
struct ResourceFactory {
  ResourceFactory(task::Container *wf) : container(wf) {}

  task::Resource *File(Text filename, Text format = "text");
  Resources Files(Text filename, Text format = "text");
  Resources Files(Text basename, int shards, Text format = "text");

  task::Container *container;
};

// Read files.
struct Reader {
  Reader(task::Container *wf, Text name, const Resources &files);

  void Connect(task::Container *wf, task::Task *task, Text input = "input");
  void Connect(task::Container *wf, const Tasks &tasks, Text input = "input");

  int shards() const { return readers.size(); }

  static string TaskName(const task::Format &format);

  Resources inputs;
  Tasks readers;
};

// Write files.
struct Writer {
  Writer(task::Container *wf, Text name, const Resources &files);

  void Connect(task::Container *wf, task::Task *task, Text output = "output");
  void Connect(task::Container *wf, const Tasks &tasks, Text output = "output");

  int shards() const { return writers.size(); }

  static string TaskName(const task::Format &format);

  Resources outputs;
  Tasks writers;
};

// Sharded writer.
struct ShardedWriter {
  ShardedWriter(task::Container *wf, Text name, const Resources &files);

  void Connect(task::Container *wf, task::Task *task, Text output = "output");
  int shards() const { return writer.shards(); }

  task::Format format;
  task::Task *sharder;
  Writer writer;
};

// Map task with record input.
struct Map {
  Map(task::Container *wf, Text name, Text type, const Resources &files);

  Reader reader;
  task::Task *mapper;
};

// Shard and sort input data.
struct Shuffle {
  Shuffle(task::Container *wf, Text name, Text format, int shards);

  void Connect(task::Container *wf, task::Task *task,
               Text format, Text output = "output");

  task::Task *sharder;
  Tasks sorters;
};

// Reduce and output to file.
struct Reduce {
  Reduce(task::Container *wf, Text name, Text type, const Resources &files);

  void Connect(task::Container *wf, const Shuffle &shuffle, Text format);

  task::Task *reducer;
  Writer writer;
};

// Map input records, shuffle, sort, reduce, and output to files.
struct MapReduce {
  MapReduce(task::Container *wf,
            Text name,
            const Resources &inputs,
            const Resources &outputs,
            Text mapper_type,
            Text reducer_type,
            Text shuffle_format);

  Map map;
  Shuffle shuffle;
  Reduce reduce;
};

// Build frame store from input frames.
struct FrameStoreBuilder {
  FrameStoreBuilder(task::Container *wf,
                    Text name,
                    task::Resource *output);

  void Connect(task::Container *wf, task::Task *task, Text output);

  task::Task *builder;
};

// Web corpus reader.
struct WebCorpus {
  WebCorpus(task::Container *wf, int num_workers = 5);

  void SetFileLimit(int limit);
  void SetBufferSize(int buffer_size);

  void Connect(task::Container *wf, task::Task *task, Text input = "input");

  std::vector<Resources> volumes;
  Tasks readers;
  task::Task *workers;
};

}  // namespace sling

#endif  // SLING_WORFKLOW_COMMON_H_

