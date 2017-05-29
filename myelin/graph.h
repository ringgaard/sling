#ifndef MYELIN_GRAPH_H_
#define MYELIN_GRAPH_H_

#include <string>

#include "base/types.h"
#include "myelin/flow.h"

namespace sling {
namespace myelin {

// Options for Graphviz DOT graph.
struct GraphOptions {
  const char *fontname = "arial";
  bool op_type_as_label = true;
  bool types_in_labels = true;
  bool constants = true;

  const char *op_shape = "box";
  const char *op_style = "rounded,filled";
  const char *op_color = "#a79776";
  const char *op_fillcolor = "#efd8a9";

  const char *input_shape = "ellipse";
  const char *input_style = "filled";
  const char *input_color = "#899e7f";
  const char *input_fillcolor = "#c5e2b6";

  const char *output_shape = "ellipse";
  const char *output_style = "filled";
  const char *output_color = "#828a9a";
  const char *output_fillcolor = "#bbc6dd";

  const char *const_shape = "box";
  const char *const_style = "filled";
  const char *const_color = "#eeeeee";
  const char *const_fillcolor = "#a6a6a6";
};

// Convert flow to DOT graph.
string FlowToDotGraph(const Flow &flow, const GraphOptions &options);

// Write DOT graph file for flow.
void FlowToDotGraphFile(const Flow &flow,
                        const GraphOptions &options,
                        const string &filename);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_GRAPH_H_

