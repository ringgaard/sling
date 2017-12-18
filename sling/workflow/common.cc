#include "sling/workflow/common.h"

#include <string>
#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/stream/file-input.h"
#include "sling/string/text.h"
#include "sling/string/strcat.h"
#include "sling/string/strip.h"
#include "sling/task/job.h"
#include "sling/task/task.h"

namespace sling {

Resources Corpora::CommonCrawlFiles(task::Job *job, int volume) {
  Resources resources;
  task::Format format("warc", "data");
  FileInput filelist(common_crawl_file_list(volume));
  string fn;
  while (filelist.ReadLine(&fn)) {
    StripWhiteSpace(&fn);
    if (fn.empty() || fn[0] == '#') continue;
    resources.push_back(job->CreateResource(fn, format));
  }
  return resources;
}

std::vector<Resources> Corpora::CommonCrawl(task::Job *job) {
  std::vector<Resources> web;
  for (int volume = 1; volume <= common_crawl_volumes(); ++volume) {
    web.push_back(CommonCrawlFiles(job, volume));
  }
  return web;
}

task::Resource *ResourceFactory::File(Text filename, Text format) {
  return job->CreateResource(filename.str(), task::Format(format.str()));
}

Resources ResourceFactory::Files(Text filename, Text format) {
  return job->CreateResources(filename.str(), task::Format(format.str()));
}

Resources ResourceFactory::Files(Text basename, int shards, Text format) {
  return job->CreateShardedResources(basename.str(),
                                     shards,
                                     task::Format(format.str()));
}

Reader::Reader(task::Job *job, Text name, const Resources &files)
    : inputs(files) {
  for (int i = 0; i < files.size(); ++i) {
    task::Shard shard(i, files.size());
    task::Task *reader =
        job->CreateTask(TaskName(files[i]->format()),
                        StrCat(name, "-reader"), shard);
    job->BindInput(reader, files[i], "input");
    readers.push_back(reader);
  }
}

string Reader::TaskName(const task::Format &format) {
  if (format.file() == "records") return "record-file-reader";
  if (format.file() == "sstable") return "sstable-reader";
  if (format.file() == "textmap") return "text-map-reader";
  if (format.file() == "store") return "frame-store-reader";
  if (format.file() == "text" && format.value() == "frame") {
    return "frame-store-reader";
  }
  return "text-file-reader";
}

void Reader::Connect(task::Job *job, task::Task *task, Text input) {
  for (int i = 0; i < shards(); ++i) {
    task::Shard shard(i, shards());
    job->Connect(
      task::Port(readers[i], "output", shard),
      task::Port(task, input.str(), shard),
      inputs[i]->format().AsMessage());
  }
}

void Reader::Connect(task::Job *job, const Tasks &tasks, Text input) {
  CHECK_EQ(tasks.size(), shards());
  for (int i = 0; i < shards(); ++i) {
    task::Shard shard(i, shards());
    job->Connect(
      task::Port(readers[i], "output", shard),
      task::Port(tasks[i], input.str(), shard),
      inputs[i]->format().AsMessage());
  }
}

Writer::Writer(task::Job *job, Text name, const Resources &files)
    : outputs(files) {
  for (int i = 0; i < files.size(); ++i) {
    task::Shard shard(i, files.size());
    task::Task *writer =
        job->CreateTask(TaskName(files[i]->format()),
                        StrCat(name, "-writer"), shard);
    job->BindOutput(writer, files[i], "output");
    writers.push_back(writer);
  }
}

void Writer::Connect(task::Job *job, task::Task *task, Text output) {
  for (int i = 0; i < shards(); ++i) {
    task::Shard shard(i, shards());
    job->Connect(
      task::Port(task, output.str(), shard),
      task::Port(writers[i], "input", shard),
      outputs[i]->format().AsMessage());
  }
}

void Writer::Connect(task::Job *job, const Tasks &tasks, Text output) {
  CHECK_EQ(tasks.size(), shards());
  for (int i = 0; i < shards(); ++i) {
    task::Shard shard(i, shards());
    job->Connect(
      task::Port(tasks[i], output.str(), shard),
      task::Port(writers[i], "input", shard),
      outputs[i]->format().AsMessage());
  }
}

string Writer::TaskName(const task::Format &format) {
  if (format.file() == "records") return "record-file-writer";
  if (format.file() == "sstable") return "sstable-writer";
  if (format.file() == "textmap") return "text-map-writer";
  if (format.file() == "store") return "frame-store-builder";
  return "text-file-writer";
}

ShardedWriter::ShardedWriter(task::Job *job,
                             Text name,
                             const Resources &files)
    : format(files.front()->format().AsMessage()),
      sharder(job->CreateTask("sharder", StrCat(name, "-sharder"))),
      writer(job, name, files) {
  writer.Connect(job, sharder);
}

void ShardedWriter::Connect(task::Job *job,
                            task::Task *task,
                            Text output) {
  job->Connect(task::Port(task, output.str()),
              task::Port(sharder, "input"),
              format);
}

Map::Map(task::Job *job, Text name, Text type, const Resources &files)
    : reader(job, name, files) {
  mapper = job->CreateTask(type.str(), StrCat(name, "-mapper"));
  reader.Connect(job, mapper);
}

Shuffle::Shuffle(task::Job *job, Text name, Text format, int shards) {
  sharder = job->CreateTask("sharder", StrCat(name, "-sharder"));
  sorters = job->CreateTasks("sorter", StrCat(name, "-sorter"), shards);
  for (int i = 0; i < shards; ++i) {
    task::Shard shard(i, shards);
    job->Connect(
      task::Port(sharder, "output", shard),
      task::Port(sorters[i], "input", shard),
      format.str());
  }
}

void Shuffle::Connect(task::Job *job, task::Task *task,
                      Text format, Text output) {
    job->Connect(
      task::Port(task, output.str()),
      task::Port(sharder, "input"),
      format.str());
}

Reduce::Reduce(task::Job *job,
               Text name,
               Text type,
               const Resources &files)
    : reducer(type.empty() ?
                  nullptr :
                  job->CreateTask(type.str(), StrCat(name, "-reducer"))),
      writer(job, name, files) {
  if (!type.empty()) writer.Connect(job, reducer);
}

void Reduce::Connect(task::Job *job, const Shuffle &shuffle, Text format) {
  for (int i = 0; i < shuffle.sorters.size(); ++i) {
    task::Shard shard(i, shuffle.sorters.size());
    job->Connect(
        task::Port(shuffle.sorters[i], "output", shard),
        task::Port(reducer, "input", shard),
        task::Format("message", format.str()));
  }
}

MapReduce::MapReduce(task::Job *job,
                     Text name,
                     const Resources &inputs,
                     const Resources &outputs,
                     Text mapper_type,
                     Text reducer_type,
                     Text shuffle_format)
    : map(job, name, mapper_type, inputs),
      shuffle(job, name, shuffle_format, outputs.size()),
      reduce(job, name, reducer_type, outputs) {
  job->Connect(map.mapper, shuffle.sharder, shuffle_format.str());
  for (int i = 0; i < outputs.size(); ++i) {
    task::Shard shard(i, outputs.size());
    job->Connect(
        task::Port(shuffle.sorters[i], "output", shard),
        reduce.reducer != nullptr ?
            task::Port(reduce.reducer, "input", shard) :
            task::Port(reduce.writer.writers[i], "input", shard),
        task::Format("message", shuffle_format.str()));
  }
}

FrameStoreBuilder::FrameStoreBuilder(task::Job *job,
                                     Text name,
                                     task::Resource *output) {
  builder = job->CreateTask("frame-store-builder", StrCat(name, "-builder"));
  job->BindOutput(builder, output, "store");
}

void FrameStoreBuilder::Connect(task::Job *job,
                                task::Task *task,
                                Text output) {
  job->Connect(
      task::Port(task, output.str()),
      task::Port(builder, "input"),
      task::Format("message", "frame"));
}

WebCorpus::WebCorpus(task::Job *job, int num_workers)
    : volumes(Corpora::CommonCrawl(job)) {
  for (int vol = 0; vol < volumes.size(); ++vol) {
    task::Shard shard(vol, volumes.size());
    task::Task *reader = job->CreateTask("warc-reader", "web-reader", shard);
    reader->AddParameter("warc_type", "response");
    readers.push_back(reader);
    for (task::Resource *resource : volumes[vol]) {
      job->BindInput(reader, resource, "input");
    }
  }

  workers = job->CreateTask("workers", "web-workers");
  workers->AddParameter("worker_threads", num_workers);
  for (task::Task *reader : readers) {
    job->Connect(reader, workers, "header:data");
  }
}

void WebCorpus::SetFileLimit(int limit) {
  int limit_per_volume = limit / readers.size();
  for (task::Task *reader : readers) {
    reader->AddParameter("max_warc_files", limit_per_volume);
  }
}

void WebCorpus::SetBufferSize(int buffer_size) {
  for (task::Task *reader : readers) {
    reader->AddParameter("buffer_size", buffer_size);
  }
}

void WebCorpus::Connect(task::Job *job, task::Task *task, Text input) {
  job->Connect(
      task::Port(workers, "output"),
      task::Port(task, input.str()),
      task::Format("message/header:data"));
}

}  // namespace sling

