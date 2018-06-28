#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/flow.h"
#include "sling/myelin/kernel/tensorflow.h"
#include "sling/util/embeddings.h"
#include "sling/util/top.h"

DEFINE_string(fact_embeddings,
              "local/data/e/fact/fact-embeddings.vec",
              "Fact embeddings");
DEFINE_string(category_embeddings,
              "local/data/e/fact/category-embeddings.vec",
              "Category embeddings");
DEFINE_string(kb, "local/data/e/wiki/kb.sling", "Knowledge base");
DEFINE_string(similarity_flow, "", "Flow file for similarity model");
DEFINE_int32(topk, 15, "Number of similar fact to list");
DEFINE_string(source, "c", "source embeddings (c=category, f=facts)");
DEFINE_string(target, "f", "target embeddings (c=category, f=facts)");

using namespace sling;
using namespace sling::myelin;

Library library;
Network net;
std::vector<string> source_lexicon;
std::vector<string> target_lexicon;

Flow::Variable *LoadWordEmbeddings(Flow *flow,
                                   const string &name,
                                   std::vector<string> *lexicon,
                                   const string &filename) {
  EmbeddingReader reader(filename);
  reader.set_normalize(true);
  int dims = reader.dim();
  int rows = reader.num_words();
  Flow::Variable *matrix = flow->AddVariable(name, DT_FLOAT, {rows, dims});
  size_t rowsize = dims * sizeof(float);
  matrix->size = rowsize * rows;
  matrix->data = flow->AllocateMemory(matrix->size);

  char *data = matrix->data;
  for (int i = 0; i < rows; ++i) {
    CHECK(reader.Next());
    lexicon->push_back(reader.word());
    memcpy(data, reader.embedding().data(), rowsize);
    data += rowsize;
  }

  return matrix;
};

void BuildModel(const string &source_embeddings,
                const string &target_embeddings) {
  Flow flow;
  FlowBuilder tf(&flow, "sim");

  LOG(INFO) << "Loading source embeddings from " << source_embeddings;
  auto *input_embeddings = LoadWordEmbeddings(&flow, "input_embeddings",
                                              &source_lexicon,
                                              source_embeddings);

  LOG(INFO) << "Loading target embeddings from " << target_embeddings;
  auto *output_embeddings = LoadWordEmbeddings(&flow, "target_embeddings",
                                               &target_lexicon,
                                               target_embeddings);

  auto *input = tf.Placeholder("input", DT_INT32, {1, 1});
  auto *hidden = tf.Gather(input_embeddings, input);
  tf.Name(tf.MatMul(hidden, tf.Transpose(output_embeddings)), "similarity");

  if (!FLAGS_similarity_flow.empty()) flow.Save(FLAGS_similarity_flow);

  LOG(INFO) << "Compile model";
  flow.Analyze(library);
  net.Compile(flow, library);
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Load knowledge base.
  Store kb;
  Handle name = kb.Lookup("name");
  if (!FLAGS_kb.empty()) {
    LOG(INFO) << "Loading knowledge base from " << FLAGS_kb;
    LoadStore(FLAGS_kb, &kb);
  }

  // Build model.
  RegisterTensorflowLibrary(&library);
  if (FLAGS_source == "c" && FLAGS_target == "f") {
    BuildModel(FLAGS_category_embeddings, FLAGS_fact_embeddings);
  } else if (FLAGS_source == "f" && FLAGS_target == "c") {
    BuildModel(FLAGS_fact_embeddings, FLAGS_category_embeddings);
  } else if (FLAGS_source == "c" && FLAGS_target == "c") {
    BuildModel(FLAGS_category_embeddings, FLAGS_category_embeddings);
  } else if (FLAGS_source == "f" && FLAGS_target == "f") {
    BuildModel(FLAGS_fact_embeddings, FLAGS_fact_embeddings);
  } else {
    LOG(FATAL) << "Unknown source/target combination";
  }

  // Make source and target mappings.
  Handles sources(&kb);
  std::unordered_map<string, int> source_map;
  for (int i = 0; i < source_lexicon.size(); ++i) {
    source_map[source_lexicon[i]] = i;
    Object obj = FromText(&kb, source_lexicon[i]);
    sources.push_back(obj.handle());
  }
  Handles targets(&kb);
  for (int i = 0; i < target_lexicon.size(); ++i) {
    Object obj = FromText(&kb, target_lexicon[i]);
    targets.push_back(obj.handle());
  }
  if (FLAGS_topk > target_lexicon.size()) FLAGS_topk = target_lexicon.size();

  // Initialize similarity computation.
  Cell *sim = net.GetCell("sim");
  Instance data(sim);
  int *input = data.Get<int>(sim->GetParameter("sim/input"));
  float *similarity = data.Get<float>(sim->GetParameter("sim/similarity"));

  for (;;) {
    // Get source id.
    string srcid;
    std::cout << (FLAGS_source == "c" ? "category" : "fact") << ": ";
    std::getline(std::cin, srcid);
    if (srcid == "q") break;

    // Look up source index.
    auto f = source_map.find(srcid);
    if (f == source_map.end()) {
      std::cout << "Unknown source id\n";
      continue;
    }
    if (FLAGS_source == "c") {
      Frame source(&kb, sources[f->second]);
      std::cout << "source: " << source.GetText(name) << "\n";
    }

    // Compute similarity scores.
    *input = f->second;
    data.Compute();

    // Find top-k targets.
    Top<std::pair<float, int>> top(FLAGS_topk);
    for (int i = 0; i < target_lexicon.size(); ++i) {
      top.push(std::make_pair(similarity[i], i));
    }
    top.sort();

    // Output top-k targets.
    for (int i = 0; i < FLAGS_topk; ++i) {
      std::cout << i << ": " << top[i].first << " "
                << target_lexicon[top[i].second];
      if (FLAGS_target == "c") {
        Frame target(&kb, targets[top[i].second]);
        std::cout << " " << target.GetText(name);
      }
      if (FLAGS_target == "f") {
        Array target(&kb, targets[top[i].second]);
        for (int j = 0; j < target.length(); ++j) {
          if (j != 0) std::cout << ":";
          Frame entity(&kb, target.get(j));
          std::cout << " " << entity.GetText(name);
        }
      }
      std::cout << "\n";
    }
  }

  return 0;
}

