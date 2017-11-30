#include <string>
#include <iostream>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/myelin/flow.h"

DEFINE_string(input, "local/tagger-rnn.flow", "Input flow");
DEFINE_string(output, "local/tagger.flow", "Output flow");

using namespace sling;
using namespace sling::myelin;

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Load and patch flow file.
  Flow flow;
  CHECK(flow.Load(FLAGS_input));

  // Zero out the last embedding vector (used for oov).
  Flow::Variable *embedding = flow.Var("tagger/fixed_embedding_matrix_0");
  CHECK(embedding != nullptr);
  float *emb_data =
      const_cast<float *>(reinterpret_cast<const float *>(embedding->data));
  for (int i = 0; i < embedding->dim(1); i++) {
    emb_data[embedding->elements() - 1 - i] = 0.0;
  }

  // Create dictionary blob.
  Flow::Function *lexicon = flow.Func("lexicon");
  CHECK(lexicon != nullptr && lexicon->ops.size() == 1);
  const string &vocab = lexicon->ops[0]->GetAttr("dict");
  Flow::Blob *dictionary = flow.AddBlob("dictionary", "lexicon");
  dictionary->attrs.Set("oov", embedding->dim(0) - 1);
  dictionary->attrs.Set("delimiter", '\n');
  char *data = flow.AllocateMemory(vocab.size());
  dictionary->data = data;
  dictionary->size = vocab.size();
  memcpy(data, vocab.data(), vocab.size());

  // Create tag map blob.
  string tagdata;
  CHECK(File::ReadContents("local/tag-map", &tagdata));
  Flow::Blob *tags = flow.AddBlob("tags", "lexicon");
  tags->attrs.Set("delimiter", '\n');
  data = flow.AllocateMemory(tagdata.size());
  tags->data = data;
  tags->size = tagdata.size();
  memcpy(data, tagdata.data(), tagdata.size());

  // Delete old lexicon "function".
  flow.DeleteOperation(lexicon->ops[0]);
  flow.DeleteFunction(lexicon);

  std::cout << flow.ToString();

  // Save converted flow to output.
  flow.Save(FLAGS_output);

  return 0;
}

