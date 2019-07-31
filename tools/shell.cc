#include <iostream>
#include <string>
#include <vector>

#include "sling/base/clock.h"
#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/strtoint.h"
#include "sling/base/types.h"
#include "sling/file/posix.h"
#include "sling/frame/json.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/frame/wire.h"
#include "sling/schema/feature-structure.h"
#include "sling/schema/schemata.h"
#include "sling/string/printf.h"
#include "sling/string/strcat.h"
#include "sling/string/strip.h"

using namespace sling;

DEFINE_bool(rebind, false, "allow symbol rebinding");

// Output stream for printing on standard output.
class StdoutStream : public FileOutputStream {
 public:
  StdoutStream() : FileOutputStream(NewStdoutFile()) {}
};

// Print objects in text format on standard output.
class StdoutPrinter {
 public:
  // Initializes printing objects to stdout.
  explicit StdoutPrinter(const Store *store)
    : output_(&stream_),
      printer_(store, &output_) {}

  // Prints object on output.
  void Print(const Object &object) { printer_.Print(object); }

  // Prints handle value relative to a store.
  void Print(Handle handle) { printer_.Print(handle); }

  // Prints all frames in the store.
  void PrintAll() { printer_.PrintAll(); }

  // Returns underlying printer.
  Printer *printer() { return &printer_; }

  // Returns underlying output.
  Output *output() { return &output_; }

 private:
  StdoutStream stream_;
  Output output_;
  Printer printer_;
};

// SLING command line shell.
class Shell {
 public:
  Shell() {
    // Create store.
    options_.symbol_rebinding = FLAGS_rebind;
    store_ = new Store(&options_);
  }

  ~Shell() {
    Clear();
  }

  // Run shell.
  void Run(int argc, char *argv[]) {
    string cmdline;
    for (int i = 1; i < argc; ++i) {
      if (i > 1) cmdline.push_back(' ');
      cmdline.append(argv[i]);
    }
    for (;;) {
      StripWhiteSpace(&cmdline);
      if (cmdline == "quit" || cmdline == "q") break;
      if (!cmdline.empty()) Do(cmdline);

      std::cout << "> ";
      std::getline(std::cin, cmdline);
    }
  }

  // Execute command line.
  void Do(const string &cmdline) {
    string cmd;
    string args;
    int space = cmdline.find(' ');
    if (space != -1) {
      cmd = cmdline.substr(0, space);
      args = cmdline.substr(space + 1);
    } else {
      cmd = cmdline;
    }
    Execute(cmd, args);
  }

  // Execute command.
  void Execute(const string &cmd, const string &args) {
    if (cmd == "load") {
      LoadCommand(args);
    } else if (cmd == "save") {
      SaveCommand(args);
    } else if (cmd == "read") {
      ReadCommand(args);
    } else if (cmd == "write") {
      WriteCommand(args);
    } else if (cmd == "print") {
      PrintCommand(args);
    } else if (cmd == "set") {
      SetCommand(args);
    } else if (cmd == "dump") {
      DumpCommand(args);
    } else if (cmd == "encode") {
      EncodeCommand(args);
    } else if (cmd == "symbols") {
      SymbolsCommand(args);
    } else if (cmd == "handle") {
      HandleCommand(args);
    } else if (cmd == "unbound") {
      UnboundCommand(args);
    } else if (cmd == "stats") {
      StatsCommand(args);
    } else if (cmd == "gc") {
      GCCommand(args);
    } else if (cmd == "coalesce") {
      CoalesceCommand(args);
    } else if (cmd == "freeze") {
      FreezeCommand(args);
    } else if (cmd == "reset") {
      ResetCommand(args);
    } else if (cmd == "time") {
      TimeCommand(args);
    } else if (cmd == "notime") {
      NoTimeCommand(args);
    } else if (cmd == "unify") {
      UnifyCommand(args);
    } else if (cmd == "compile") {
      CompileCommand(args);
    } else if (cmd == "construct") {
      ConstructCommand(args);
    } else if (cmd == "rolemap") {
      RoleMapCommand(args);
    } else if (cmd == "trace") {
      TraceCommand(args);
    } else if (cmd == "indent") {
      IndentCommand(args);
    } else if (cmd == "shallow") {
      ShallowCommand(args);
    } else if (cmd == "deep") {
      DeepCommand(args);
    } else if (cmd == "local") {
      LocalCommand(args);
    } else if (cmd == "global") {
      GlobalCommand(args);
    } else if (cmd == "byref") {
      ByRefCommand(args);
    } else if (cmd == "json") {
      JsonCommand(args);
    } else if (cmd == "rolestat") {
      RoleStatCommand(args);
    } else if (cmd == "unresolved") {
      UnresolvedCommand(args);
    } else if (cmd == "inspect") {
      InspectCommand(args);
    } else {
      string cmdline = StrCat(cmd, " ", args);
      if (cmdline[0] == '{') {
        PrintCommand(cmdline);
      } else {
        std::cout << "Unknown command\n";
      }
    }
  }

  // Clears the store(s) in the shell.
  void Clear() {
    delete compiler_;
    compiler_ = nullptr;
    delete schemata_;
    schemata_ = nullptr;
    const Store *globals = store_->globals();
    delete store_;
    delete globals;
    store_ = nullptr;
  }

  // Loads encoded file into store.
  void LoadCommand(const string &args) {
    File *file = OpenFile(args, "r");
    if (file == nullptr) return;
    Timing timing(this);
    store_->LockGC();
    FileDecoder decoder(store_, file);
    Object obj = decoder.DecodeAll();
    store_->UnlockGC();
    if (trace_ > 0) {
      std::cout << ToText(obj, indent_) << "\n";
    }
  }

  // Saves all named objects in store to file.
  void SaveCommand(const string &args) {
    File *file = OpenFile(args, "w");
    if (file == nullptr) return;
    Timing timing(this);
    FileEncoder encoder(store_, file);
    encoder.EncodeAll();
    CHECK(encoder.Close());
  }

  // Reads file(s) in text format into store.
  void ReadCommand(const string &args) {
    // Find matching files.
    std::vector<string> filenames;
    string pattern = args;
    StripWhiteSpace(&pattern);
    File::Match(pattern, &filenames);
    if (filenames.empty()) {
      std::cout << "File not found: " << args << "\n";
      return;
    }

    Timing timing(this);
    for (const string &filename : filenames) {
      FileReader reader(store_, filename);
      if (json_mode_) reader.reader()->set_json(true);
      while (!reader.done()) {
        reader.Read();
        if (reader.error()) {
          std::cout << reader.reader()->GetErrorMessage(filename) << "\n";
          break;
        }
      }
    }
  }

  // Writes all named objects in store to text file.
  void WriteCommand(const string &args) {
    Timing timing(this);
    FilePrinter printer(store_, args);
    printer.PrintAll();
    CHECK(printer.Close());
  }

  // Parses and prints object.
  void PrintCommand(const string &args) {
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;
    if (json_mode_) {
      string output;
      StringOutputStream stream(&output);
      Output out(&stream);
      JSONWriter writer(store_, &out);
      writer.set_shallow(shallow_);
      writer.set_global(global_);
      writer.set_byref(byref_);
      writer.set_indent(indent_);
      for (Handle arg : arguments) {
        writer.Write(arg);
        out.WriteChar('\n');
      }
      std::cout << output;
    } else {
      StringPrinter printer(store_);
      printer.printer()->set_shallow(shallow_);
      printer.printer()->set_global(global_);
      printer.printer()->set_byref(byref_);
      printer.printer()->set_indent(indent_);
      for (Handle arg : arguments) {
        printer.Print(arg);
        printer.output()->WriteChar('\n');
      }
      std::cout << printer.text();
    }
  }

  // Sets role for frame.
  void SetCommand(const string &args) {
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;
    if (arguments.size() != 3) {
      std::cout << "Syntax error: 'set <frame> <role> <value>' expected\n";
      return;
    }
    store_->Set(arguments[0], arguments[1], arguments[2]);
  }

  // Prints handle for object.
  void HandleCommand(const string &args) {
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;
    for (Handle arg : arguments) {
      std::cout << StringPrintf("%08X %d", arg.raw(), arg.raw()) << "\n";
    }
  }

  // Prints object information.
  void InspectCommand(const string &args) {
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;
    for (Handle arg : arguments) {
      string info = StringPrintf("Handle: %08X (%d)", arg.raw(), arg.raw());
      StrAppend(&info, "\n");
      if (arg.IsRef() && !arg.IsNil()) {
        const Datum *object = store_->Deref(arg);
        StrAppend(&info, StringPrintf("Addr: %p", object), "\n");
        StrAppend(&info, StringPrintf("Info: %08X", object->info), "\n");
        StrAppend(&info, StringPrintf("Self: %08X", object->self.raw()), "\n");
      }
      std::cout << info;
    }
  }

  // Dumps encoded input.
  void Dump(Input *input) {
    int index = 0;
    while (!input->done()) {
      uint64 tag;
      string str;
      Handle h;
      if (!input->ReadVarint64(&tag)) {
        std::cout << "Error reading tag\n";
        return;
      }
      uint64 arg = tag >> 3;

      // Decode different tag types.
      switch (tag & 7) {
        case WIRE_REF:
          std::cout << "REF     " << arg << "\n";
          break;

        case WIRE_FRAME:
          std::cout << "FRAME   " << arg << " (" << index << ")\n";
          index++;
          break;

        case WIRE_STRING:
          if (!input->ReadString(arg, &str)) {
            std::cout << "Error reading string, length " << arg << "\n";
            return;
          }
          std::cout << "STRING  " << str << " (" << index << ")\n";
          index++;
          break;

        case WIRE_SYMBOL:
          if (!input->ReadString(arg, &str)) {
            std::cout << "Error reading symbol, length " << arg << "\n";
            return;
          }
          std::cout << "SYMBOL  " << str << " (" << index << ")\n";
          index++;
          break;

        case WIRE_LINK:
          if (!input->ReadString(arg, &str)) {
            std::cout << "Error reading link, length " << arg << "\n";
            return;
          }
          std::cout << "LINK    " << str << " (" << index << ")\n";
          index++;
          break;

        case WIRE_INTEGER:
          h = Handle::Integer(arg);
          std::cout << "INTEGER " << h.AsInt() << "\n";
          break;

        case WIRE_FLOAT:
          h = Handle::FromFloatBits(arg);
          if (h.IsIndex()) {
            std::cout << "FLOAT   @" << h.AsIndex() << "\n";
          } else {
            std::cout << "FLOAT   " << h.AsFloat() << "\n";
          }
          break;

        case WIRE_SPECIAL:
          switch (arg) {
            case WIRE_NIL: std::cout << "SPECIAL nil\n"; break;
            case WIRE_ID: std::cout << "SPECIAL id\n"; break;
            case WIRE_ISA: std::cout << "SPECIAL isa\n"; break;
            case WIRE_IS: std::cout << "SPECIAL is\n"; break;

            case WIRE_ARRAY: {
              uint32 size;
              if (!input->ReadVarint32(&size)) {
                std::cout << "Error reading array size\n";
                return;
              }
              std::cout << "ARRAY   " << size << " (" << index << ")\n";
              index++;
              break;
            }

            case WIRE_INDEX: {
              uint32 index;
              if (!input->ReadVarint32(&index)) {
                std::cout << "Error reading index value\n";
                return;
              }
              std::cout << "INDEX   " << index << "\n";
              break;
            }

            case WIRE_RESOLVE: {
              uint32 slots;
              uint32 replace;
              if (!input->ReadVarint32(&slots) ||
                  !input->ReadVarint32(&replace)) {
                std::cout << "Error reading index value\n";
                return;
              }
              std::cout << "RESOLVE " << slots << ", " << replace << "\n";
              break;
            }

            default:
              std::cout << "Invalid special tag: " << arg << "\n";
              return;
          }
      }
    }
  }

  // Dumps encoded file.
  void DumpCommand(const string &args) {
    File *file = OpenFile(args, "r");
    if (file == nullptr) return;
    Timing timing(this);
    FileInputStream stream(file);
    Input input(&stream);
    Dump(&input);
  }

  // Encodes objects and dumps their encoding.
  void EncodeCommand(const string &args) {
    // Parse arguments.
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;

    // Encode objects.
    StringEncoder encoder(store_);
    for (Handle h : arguments) encoder.Encode(h);

    // Output object encoding.
    const string &encoded  = encoder.buffer();
    std::cout << encoded.size() << " bytes\n";
    ArrayInputStream stream(encoded.data(), encoded.size());
    Input input(&stream);
    Dump(&input);
  }

  // Prints all bound symbols.
  void SymbolsCommand(const string &args) {
    StdoutPrinter printer(store_);
    printer.printer()->set_shallow(shallow_);
    printer.printer()->set_global(global_);
    printer.printer()->set_byref(byref_);
    printer.printer()->set_indent(indent_);
    printer.PrintAll();
  }

  // Prints all unbound symbols.
  void UnboundCommand(const string &args) {
    MapDatum *symbols = store_->GetMap(store_->symbols());
    for (Handle *bucket = symbols->begin(); bucket < symbols->end(); ++bucket) {
      Handle h = *bucket;
      while (!h.IsNil()) {
        const SymbolDatum *symbol = store_->GetSymbol(h);
        if (symbol->unbound()) {
          String name(store_, symbol->name);
          if (name.valid()) std::cout << name.value() << "\n";
        }
        h = symbol->next;
      }
    }
  }

  // Outputs memory statistics for store.
  void StatsCommand(const string &args) {
    Timing timing(this);
    MemoryUsage usage;
    if (args == "global" && store_->globals() != nullptr) {
      store_->globals()->GetMemoryUsage(&usage);
    } else {
      store_->GetMemoryUsage(&usage);
    }

    std::cout << "Heap used ........ : " << usage.used_heap_bytes() << "\n";
    std::cout << "Heap unused ...... : " << usage.unused_heap_bytes << "\n";
    std::cout << "Heap total ....... : " << usage.total_heap_size << "\n";
    std::cout << "Number of heaps .. : " << usage.num_heaps << "\n";

    std::cout << "Handles used ..... : " << usage.used_handles() << "\n";
    std::cout << "Handles unused ... : " << usage.num_unused_handles << "\n";
    std::cout << "Handles free ..... : " << usage.num_free_handles << "\n";
    std::cout << "Handles dead ..... : " << usage.num_dead_handles << "\n";
    std::cout << "Handles total .... : " << usage.num_handles << "\n";

    std::cout << "Bound symbols .... : " << usage.num_bound_symbols << "\n";
    std::cout << "Proxy symbols .... : " << usage.num_proxy_symbols << "\n";
    std::cout << "Unbound symbols .. : " << usage.num_unbound_symbols << "\n";
    std::cout << "Total symbols .... : " << usage.num_symbols() << "\n";
    std::cout << "Symbol buckets ... : " << usage.num_symbol_buckets << "\n";
  }

  // Performs garbage collection.
  void GCCommand(const string &args) {
    Timing timing(this);
    store_->GC();
  }

  // Coalesces strings in store.
  void CoalesceCommand(const string &args) {
    Timing timing(this);
    store_->CoalesceStrings();
  }

  // Freezes store and creates a local store.
  void FreezeCommand(const string &args) {
    // Check if store is already frozen.
    if (store_->globals() != nullptr) {
      std::cout << "Global store is already frozen\n";
      return;
    }

    // Freeze global store and create a local store.
    Timing timing(this);
    store_->Freeze();
    store_ = new Store(store_);
    global_ = false;
  }

  // Resets global and local stores.
  void ResetCommand(const string &args) {
    Timing timing(this);
    Clear();
    store_ = new Store(&options_);
    global_ = true;
  }

  // Enables command timing.
  void TimeCommand(const string &args) {
    timing_ = true;
  }

  // Disables command timing.
  void NoTimeCommand(const string &args) {
    timing_ = false;
  }

  // Sets trace level.
  void TraceCommand(const string &args) {
    trace_ = atoi32(args);
  }

  // Sets indentation level.
  void IndentCommand(const string &args) {
    indent_ = atoi32(args);
  }

  // Sets shallow printing.
  void ShallowCommand(const string &args) {
    shallow_ = true;
  }

  // Sets deep printing.
  void DeepCommand(const string &args) {
    shallow_ = false;
  }

  // Sets local printing.
  void LocalCommand(const string &args) {
    global_ = false;
  }

  // Sets global printing.
  void GlobalCommand(const string &args) {
    global_ = true;
  }

  // Sets by-reference printing.
  void ByRefCommand(const string &args) {
    byref_ = true;
  }

  // Sets JSON mode for reading SLING text input.
  void JsonCommand(const string &args) {
    json_mode_ = true;
  }

  // Prints statistics for all frame roles.
  void RoleStatCommand(const string &args) {
    bool only_proxies = (args == "proxy");
    HandleMap<int> rolecount;
    std::vector<std::pair<int, Handle>> histogram;
    Store::Iterator it(store_);
    const Datum *object;
    while ((object = it.next()) != nullptr) {
      if (object->IsFrame()) {
        const FrameDatum *frame = object->AsFrame();
        int slots = frame->slots();
        if (histogram.size() < slots + 1) {
          histogram.resize(slots + 1);
        }
        histogram[slots].first++;
        histogram[slots].second = frame->self;
        for (const Slot *s = frame->begin(); s < frame->end(); ++s) {
          if (!only_proxies || store_->IsProxy(s->value)) {
            rolecount[s->name]++;
          }
        }
      }
    }

    std::cout << rolecount.size() << " roles\n";
    for (auto r : rolecount) {
      std::cout << HandleName(r.first) << ": " << r.second << "\n";
    }

    for (int i = 0; i < histogram.size(); ++i) {
      if (histogram[i].first != 0) {
        std::cout << i << " slots: "
                  << histogram[i].first << " frames "
                  << HandleName(histogram[i].second) << "\n";
      }
    }
  }

  // Prints all unresolved symbols (i.e. proxies).
  void UnresolvedCommand(const string &args) {
    Store::Iterator it(store_);
    const Datum *object;
    while ((object = it.next()) != nullptr) {
      if (object->IsProxy()) {
        std::cout << HandleName(object->self) << "\n";
      }
    }
  }

  // Unifies feature structures.
  void UnifyCommand(const string &args) {
    // Parse arguments.
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;

    // Unify all the arguments.
    Timing timing(this);
    FeatureStructure fs(store_);
    int result = -1;
    for (Handle arg : arguments) {
      // Argument must be a frame.
      Object object(store_, arg);
      if (!object.IsFrame()) {
        std::cout << "Argument is not a frame: " << ToText(object) << "\n";
        return;
      }

      // Add object to feature structure.
      int node = fs.AddFrame(object.handle());

      // Unify next object with existing feature structure.
      if (result == -1) {
        result = node;
      } else {
        result = fs.Unify(result, node);
        if (result == -1) {
          std::cout << "Unification with " << ToText(object) << " failed\n";
          if (trace_ > 0) {
            Frame tmpl(store_, fs.Template());
            std::cout << "Partial:\n";
            OutputTemplate(tmpl);
          }
          return;
        }
      }
    }

    // Output result of unification.
    if (result != -1) {
      // Output DAG template if tracing is enabled.
      if (trace_ > 0) {
        Frame tmpl(store_, fs.Template());
        OutputTemplate(tmpl);
        std::cout << "Result is in node " << result << "\n";

        // Compact DAG.
        std::cout << "Compacted DAG:\n";
        result = fs.Compact(result);
        Frame compacted(store_, fs.Template());
        OutputTemplate(compacted);
      }

      // Output unified feature structure as frame.
      Frame unified(store_, fs.Construct(result));
      std::cout << ToText(unified, indent_) << "\n";
    }
  }

  // Compiles schema.
  void CompileCommand(const string &args) {
    // Create schema compiler.
    if (compiler_ == nullptr) compiler_ = new SchemaCompiler(store_);

    // Parse arguments.
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;

    Timing timing(this);
    for (Handle schema : arguments) {
      // Compile schema.
      Handle tmpl = compiler_->Compile(schema);
      if (tmpl.IsNil()) {
        std::cout << "Schema compilation of " << args << " failed\n";
        return;
      }

      // Output DAG if tracing is enabled.
      if (trace_ > 0) {
        Frame dag(store_, tmpl);
        if (trace_ > 1) std::cout << ToText(dag, indent_) << "\n";
        OutputTemplate(dag);
      }
    }
  }

  // Constructs or fetches role map for schema.
  void RoleMapCommand(const string &args) {
    // Create schema compiler.
    if (compiler_ == nullptr) compiler_ = new SchemaCompiler(store_);

    // Parse arguments.
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;

    Timing timing(this);
    for (Handle schema : arguments) {
      // Get rolemap.
      Handle rolemap = compiler_->GetRoleMap(schema);
      std::cout << ToText(store_, rolemap, indent_) << "\n";
    }
  }

  // Constructs frame.
  void ConstructCommand(const string &args) {
    // Create schemata.
    if (schemata_ == nullptr) schemata_ = new Schemata(store_);

    // Parse arguments.
    Handles arguments(store_);
    if (!Eval(args, &arguments)) return;
    if (arguments.size() != 2) {
      std::cout << "Error: construction requires two arguments\n";
      return;
    }
    Object schema(store_, arguments[0]);
    Object input(store_, arguments[1]);
    if (!schema.IsFrame() || !input.IsFrame()) {
      std::cout << "Error: arguments are not frames\n";
      return;
    }

    // Construct frame from schema and input frame.
    Timing timing(this);
    Object result(store_,
                  schemata_->Construct(schema.handle(), input.handle()));
    timing.end();

    // Output result.
    std::cout << ToText(result, indent_) << "\n";
  }

  // Parses and evaluates text in SLING format.
  bool Eval(const string &text, Handles *result) {
    StringReader reader(store_, text);
    if (json_mode_) reader.reader()->set_json(true);
    while (!reader.done()) {
      Object object = reader.Read();
      if (reader.error()) {
        std::cout << reader.reader()->GetErrorMessage("input") << "\n";
        return false;
      } else {
        result->push_back(object.handle());
      }
    }
    return true;
  }

  // Outputs feature structure template.
  void OutputTemplate(const Frame &frame) {
    int i = 0;
    for (auto s : frame) {
      Object name(frame.store(), s.name);
      Object value(frame.store(), s.value);
      if (name.valid() && name.IsFrame() && name.AsFrame().IsPublic()) {
        name = name.AsFrame().id();
      }
      if (value.valid() && value.IsFrame() && value.AsFrame().IsPublic()) {
        value = value.AsFrame().id();
      }
      std::cout << StringPrintf("%04d", i) << " " << ToText(name) << ": "
                << ToText(value) << "\n";
      i++;
    }
  }

  // Opens file.
  static File *OpenFile(Text filename, const char *mode) {
    string fn = filename.ToString();
    StripWhiteSpace(&fn);
    File *file = File::Open(fn, mode);
    if (file == nullptr) {
      std::cout << "Unable to open file: " << fn << "\n";
      return nullptr;
    } else {
      return file;
    }
  }

  // Gets the name for a handle.
  string HandleName(Handle handle) {
    return store_->DebugString(handle);
  }

 private:
  // Utility class for timing commands.
  class Timing {
   public:
    explicit Timing(Shell *shell) : active_(shell->timing_) {
      if (active_) timer_.start();
    }

    ~Timing() {
      if (active_) {
        timer_.stop();
        if (timer_.ms() < 2) {
          std::cout << "time: " << timer_.us() << " us\n";
        } else {
          std::cout << "time: " << timer_.ms() << " ms\n";
        }
      }
    }

    void end() { if (active_) timer_.stop(); }

   private:
    bool active_;
    Clock timer_;
  };

  // Object store.
  Store::Options options_;
  Store *store_;

  // Schemata for frame construction.
  Schemata *schemata_ = nullptr;

  // Schema compiler.
  SchemaCompiler *compiler_ = nullptr;

  // Output timing of command.
  bool timing_ = false;

  // Tracing level.
  int trace_ = 0;

  // Indentation for SLING output.
  int indent_ = 0;

  // JSON mode for reading and printing SLING text.
  bool json_mode_ = false;

  // Print mode.
  bool shallow_ = true;
  bool global_ = true;
  bool byref_ = true;
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Run shell interpreter.
  Shell shell;
  shell.Run(argc, argv);

  return 0;
}

