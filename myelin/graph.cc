#include "myelin/graph.h"

#include "base/types.h"
#include "file/file.h"
#include "string/printf.h"

namespace sling {
namespace myelin {

static void AppendOpId(string *str, const Flow::Operation *op) {
  str->push_back('"');
  str->append(op->name);
  str->push_back('"');
}

static void AppendVarId(string *str, const Flow::Variable *var) {
  str->push_back('"');
  str->append("v:");
  str->append(var->name);
  str->push_back('"');
}

string FlowToDotGraph(const Flow &flow, const GraphOptions &options) {
  string str;

  // Output DOT graph header.
  str.append("digraph flow {\n");
  StringAppendF(&str, "node [fontname=\"%s\"]\n", options.fontname);

  // Output DOT graph nodes for ops.
  for (Flow::Operation *op : flow.ops()) {
    AppendOpId(&str, op);
    str.append(" [");

    str.append("label=\"");
    if (options.op_type_as_label) {
      str.append(op->type);
    } else {
      str.append(op->name);
    }
    if (options.types_in_labels && op->outdegree() >= 1) {
      str.append("\\n");
      str.append(op->outputs[0]->TypeString());
    }
    str.append("\" ");

    StringAppendF(&str, "shape=%s ", options.op_shape);
    StringAppendF(&str, "style=\"%s\" ", options.op_style);
    StringAppendF(&str, "color=\"%s\" ", options.op_color);
    StringAppendF(&str, "fillcolor=\"%s\" ", options.op_fillcolor);
    str.append("];\n");
  }

  // Output DOT graph edges between ops.
  for (Flow::Operation *op : flow.ops()) {
    for (Flow::Variable *input : op->inputs) {
      if (input->producer != nullptr) {
        AppendOpId(&str, input->producer);
        str.append(" -> ");
        AppendOpId(&str, op);
        str.append(";\n");
      }
    }
  }

  // Output DOT graph nodes and edges for inputs, outputs, and constants.
  for (Flow::Variable *var : flow.vars()) {
    if (!options.include_constants && var->data != nullptr) continue;
    if (var->in || var->out) {
      AppendVarId(&str, var);
      str.append(" [");
      str.append("label=\"");
      size_t slash = var->name.rfind('/');
      if (slash != string::npos) {
        str.append(var->name.substr(slash + 1));
      } else {
        str.append(var->name);
      }
      if (options.types_in_labels) {
        str.append("\\n");
        str.append(var->TypeString());
      }
      if (options.max_value_size > 0 && var->data != nullptr) {
        int elements = var->elements();
        if (elements > 0 && elements <= options.max_value_size) {
          str.append("\\n");
          str.append(var->DataString());
        }
      }
      str.append("\" ");
      if (var->data != nullptr) {
        StringAppendF(&str, "shape=%s ", options.const_shape);
        StringAppendF(&str, "style=\"%s\" ", options.const_style);
        StringAppendF(&str, "color=\"%s\" ", options.const_color);
        StringAppendF(&str, "fillcolor=\"%s\" ", options.const_fillcolor);
      } else if (var->in) {
        StringAppendF(&str, "shape=%s ", options.input_shape);
        StringAppendF(&str, "style=\"%s\" ", options.input_style);
        StringAppendF(&str, "color=\"%s\" ", options.input_color);
        StringAppendF(&str, "fillcolor=\"%s\" ", options.input_fillcolor);
      } else {
        StringAppendF(&str, "shape=%s ", options.output_shape);
        StringAppendF(&str, "style=\"%s\" ", options.output_style);
        StringAppendF(&str, "fillcolor=\"%s\" ", options.output_fillcolor);
      }
      str.append("];\n");
    }
    if (var->in) {
      for (Flow::Operation *consumer : var->consumers) {
        AppendVarId(&str, var);
        str.append(" -> ");
        AppendOpId(&str, consumer);
        str.append(";\n");
      }
    }
    if (var->out && var->producer != nullptr) {
        AppendOpId(&str, var->producer);
        str.append(" -> ");
        AppendVarId(&str, var);
        str.append(";\n");
    }
  }

  // Output DOT graph footer.
  str.append("}\n");

  return str;
}

void FlowToDotGraphFile(const Flow &flow,
                        const GraphOptions &options,
                        const string &filename) {
  string dot = FlowToDotGraph(flow, options);
  CHECK_OK(File::WriteContents(filename, dot));
}

}  // namespace myelin
}  // namespace sling

