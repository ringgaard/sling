// Tool for generating ELF object file with embedded data files. The generated
// object file can be linked into a binary with a registration function that
// is called with each of the embedded files.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "util/elf-writer.h"

using sling::Elf;

// Package files as embedded data in an ELF object file.
class EmbeddedData {
 public:
  EmbeddedData(const char *regfunc) : regfunc_(regfunc) {}

  // Add file as embedded data.
  void AddFile(const char *filename) {
    // Open file.
    int f = open(filename, O_RDONLY);
    if (f == -1) {
      perror(filename);
      exit(EXIT_FAILURE);
    }

    // Get file meta data.
    struct stat st;
    if (fstat(f, &st) == -1) {
      perror(filename);
      exit(EXIT_FAILURE);
    }

    // Add file content to data section.
    int file_content_offset = content_.offset();
    char buffer[512];
    int bytes;
    while ((bytes = read(f, buffer, sizeof(buffer))) > 0) {
      content_.Add(buffer, bytes);
    }
    content_.Add8(0);

    // Add file name to string section.
    int filename_offset = strdata_.offset();
    strdata_.Add(filename, strlen(filename) + 1);

    // Add file metadata to data section.
    data_.AddPtr(&strdata_, filename_offset);
    data_.Add64(st.st_size);
    data_.AddPtr(&content_, file_content_offset);
    data_.Add64(st.st_mtime);

    // Close file.
    close(f);
    num_files_++;
  }

  // Update embedded data object file.
  void Update() {
    // Add symbol for file table.
    elf_.AddSymbol("table", data_.progbits, STB_LOCAL,
                   STT_OBJECT, 4 * 8 * num_files_);

    // Add symbol for init function.
    elf_.AddSymbol("init", startup_.progbits, STB_LOCAL, STT_FUNC, 15);

    // Generate init function which calls an external registration function with
    // the file table and file count as arguments, i.e.:
    //
    //   extern void register_embedded_files(EmbeddedFile *files, int count);
    //   static void init() __attribute__((constructor));
    //   static void init() {
    //     register_embedded_files(files, num_files);
    //   }
    Elf::Symbol *regfunc = elf_.AddSymbol(regfunc_, nullptr,
                                          STB_GLOBAL, STT_NOTYPE);

    // Emit: mov esi, #files
    startup_.Add8(0xbe);
    startup_.Add32(num_files_);

    // Emit: mov edi, table
    startup_.Add8(0xbf);
    startup_.AddPtr32(&data_, 0);

    // Emit: jmpf register_embedded_files
    startup_.Add8(0xe9);
    startup_.AddReloc(regfunc, R_X86_64_PC32, -4);
    startup_.Add32(0);

    // Add init function to init array section.
    init_.AddPtr(&startup_, 0);

    // Update sections.
    data_.Update();
    strdata_.Update();
    content_.Update();
    startup_.Update();
    init_.Update();

    // Update ELF object file.
    elf_.Update();
  }

  // Write object file with embedded data.
  void Write(const char *filename) {
    elf_.Write(filename);
  }

 private:
  // ELF file writer.
  Elf elf_;

  // Data section for file metadata.
  Elf::Buffer data_{&elf_, ".data", ".rela.data",
                    SHT_PROGBITS, SHF_ALLOC | SHF_WRITE};

  // String section file names.
  Elf::Buffer strdata_{&elf_, ".rodata.str", nullptr, SHT_PROGBITS, SHF_ALLOC};

  // Data section file contents.
  Elf::Buffer content_{&elf_, ".rodata.file", nullptr, SHT_PROGBITS, SHF_ALLOC};

  // Init function section.
  Elf::Buffer startup_{&elf_, ".text.startup", ".rela.text.startup",
                       SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR};

  // Init function array.
  Elf::Buffer init_{&elf_, ".init_array", ".rela.init_array",
                    SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE};

  // Name of external registration function.
  const char *regfunc_;

  // Number of files added.
  int num_files_ = 0;
};

int main(int argc, char *argv[]) {
  // Get command line arguments.
  const char *output_file = "embeddata.o";
  const char *registration_function = "register_embedded_files";
  bool verbose = false;
  int opt;
  while ((opt = getopt(argc, argv, "ho:r:v")) != -1) {
    switch (opt) {
      case 'o':
        output_file = optarg;
        break;

      case 'r':
        registration_function = optarg;
        break;

      case 'v':
        verbose = true;
        break;

      case 'h':
      default:
        fprintf(stderr,
                "Usage: %s [-v] [-o outfile] [-r regfunc] files...\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  // Generate ELF object file with embedded data.
  EmbeddedData data(registration_function);
  for (int i = optind; i < argc; i++) {
    if (verbose) printf("Add %s\n", argv[i]);
    data.AddFile(argv[i]);
  }
  data.Update();
  if (verbose) printf("Writing embedded data to %s\n", output_file);
  data.Write(output_file);

  return 0;
}

