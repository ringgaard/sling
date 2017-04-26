#ifndef TOOLS_ELF_WRITER_
#define TOOLS_ELF_WRITER_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>

#include <string>
#include <vector>

// ELF object file writer.
class Elf {
 public:
  // Section in ELF file.   
  struct Section {
    Section(int idx) {
      memset(&hdr, 0, sizeof(Elf64_Shdr));
      index = idx;
      data = nullptr;
    }
    Elf64_Shdr hdr;
    int index;
    int symidx = 0;
    const void *data;
  };

  // Symbol in ELF file.   
  struct Symbol {
    Symbol(int idx) {
      memset(&sym, 0, sizeof(Elf64_Sym));
      index = idx;
    }
    Elf64_Sym sym;
    int index;
  };

  // Buffer for generating section.   
  struct Buffer {
    Buffer(Elf *elf,
           const char *name,
           const char *relaname,
           Elf64_Word type,
           Elf64_Word flags) : elf(elf) {
      progbits = elf->AddSection(name, type);
      progbits->hdr.sh_flags = flags;
      if (relaname) {
        rela = elf->AddSection(relaname, SHT_RELA);
        rela->hdr.sh_link = elf->symtab()->index;
        rela->hdr.sh_info = progbits->index;
        rela->hdr.sh_entsize = sizeof(Elf64_Rela);
        rela->hdr.sh_addralign = 8;
      } else {
        rela = nullptr;
      }
    }

    void Add(const void *data, int size) {
      content.append(reinterpret_cast<const char *>(data), size);
    }

    void Add8(uint8_t data) {
      Add(&data, sizeof(uint8_t));
    }

    void Add32(uint32_t data) {
      Add(&data, sizeof(uint32_t));
    }

    void Add64(uint64_t data) {
      Add(&data, sizeof(uint64_t));
    }

    void AddPtr(Buffer *buffer, int offset) {
      AddReloc(buffer, R_X86_64_64, offset);
      Add64(0);
    }
    
    void AddPtr32(Buffer *buffer, int offset) {
      AddReloc(buffer, R_X86_64_32, offset);
      Add32(0);
    }

    void AddReloc(Section *section, int type, int addend = 0) {
      Elf64_Rela rel;
      rel.r_offset = offset();
      rel.r_info = ELF64_R_INFO(section->symidx, type); 
      rel.r_addend = addend; 
      relocs.append(reinterpret_cast<const char *>(&rel), sizeof(rel));
    }

    void AddReloc(Symbol *symbol, int type, int addend = 0) {
      Elf64_Rela rel;
      rel.r_offset = offset();
      rel.r_info = ELF64_R_INFO(symbol->index, type); 
      rel.r_addend = addend; 
      relocs.append(reinterpret_cast<const char *>(&rel), sizeof(rel));
    }

    void AddReloc(Buffer *buffer, int type, int addend = 0) {
      AddReloc(buffer->progbits, type, addend);
    }

    void Update() {
      progbits->data = content.data();
      progbits->hdr.sh_size = content.size();
      if (rela) {
        rela->data = relocs.data();
        rela->hdr.sh_size = relocs.size();
      }
    }
    
    int offset() const { return content.size(); }

    Elf *elf;
    Section *progbits;
    Section *rela;
    std::string content;
    std::string relocs;
  };

  Elf();
  ~Elf();

  // Add section to ELF file.
  Section *AddSection(const char *name, Elf64_Word type);
  
  // Add symbol to ELF file.
  Symbol *AddSymbol(const char *name);
  Symbol *AddSymbol(const char *name, Section *section,
                    int bind, int type, int size = 0);

  // Update symbol and section tables.
  void Update();

  // Write ELF object file.
  void Write(const char *filename);

  // Return symbol table.
  Section *symtab() { return symtab_; }

 private:
  // ELF file header.
  Elf64_Ehdr ehdr_;

  // Symbol table.
  std::vector<Symbol *> symbols_;

  // Symbol names.
  std::string symbol_names_; 

  // Symbol table section.
  Section *symtab_;

  // Symbol section contents.
  std::vector<Elf64_Sym> symbol_data_;
  
  // Sections.
  std::vector<Section *> sections_;

  // Section names.
  std::string section_names_; 
};

#endif  // TOOLS_ELF_WRITER_
