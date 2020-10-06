#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/compiler.h"
#include "sling/util/embeddings.h"
#include "sling/util/top.h"

DEFINE_string(embeddings,
              "data/e/wiki/en/word-embeddings.vec",
              "Word embeddings");
DEFINE_int32(topk, 15, "Number of similar words to list");

using namespace sling;
using namespace sling::myelin;

Network net;
std::vector<string> lexicon;
std::unordered_map<string, int> wordmap;

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

void BuildModel(const string &embeddings_file) {
  Flow flow;
  FlowBuilder tf(&flow, "sim");

  LOG(INFO) << "Loading embeddings from " << embeddings_file;
  auto *embeddings = LoadWordEmbeddings(&flow, "embeddings",
                                        &lexicon, embeddings_file);

  auto *input = tf.Placeholder("input", DT_INT32, {1, 1});
  auto *hidden = tf.Gather(embeddings, input);
  tf.Name(tf.MatMul(hidden, tf.Transpose(embeddings)), "similarity");

  LOG(INFO) << "Compile model";
  Compiler compiler;
  compiler.Compile(&flow, &net);
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  BuildModel(FLAGS_embeddings);
  if (FLAGS_topk > lexicon.size()) FLAGS_topk = lexicon.size();
  for (int i = 0; i < lexicon.size(); ++i) {
    wordmap[lexicon[i]] = i;
  }

  // Initialize similarity computation.
  Cell *sim = net.GetCell("sim");
  Instance data(sim);
  int *input = data.Get<int>(sim->GetParameter("sim/input"));
  float *similarity = data.Get<float>(sim->GetParameter("sim/similarity"));

  for (;;) {
    // Get word.
    string word;
    std::cout << "word: ";
    std::getline(std::cin, word);
    if (word == "q") break;

    // Look up word index.
    auto f = wordmap.find(word);
    if (f == wordmap.end()) {
      std::cout << "Unknown word\n";
      continue;
    }

    // Compute similarity scores.
    *input = f->second;
    data.Compute();

    // Find top-k similar words.
    Top<std::pair<float, int>> top(FLAGS_topk);
    for (int i = 0; i < lexicon.size(); ++i) {
      top.push(std::make_pair(similarity[i], i));
    }
    top.sort();

    // Output top-k most similar words.
    for (int i = 0; i < FLAGS_topk; ++i) {
      std::cout << i << ": " << top[i].first << " "
                << lexicon[top[i].second] << "\n";
    }
  }

  return 0;
}

