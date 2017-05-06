#include <elf.h>
#include <string.h>
#include <stdio.h>

#include "tools/elf-writer.h"

Elf::Elf() {
  // Initialize header.
  memset(&ehdr_, 0, sizeof(ehdr_));
  ehdr_.e_ident[EI_MAG0] = ELFMAG0;
  ehdr_.e_ident[EI_MAG1] = ELFMAG1;
  ehdr_.e_ident[EI_MAG2] = ELFMAG2;
  ehdr_.e_ident[EI_MAG3] = ELFMAG3;
  ehdr_.e_ident[EI_CLASS] = ELFCLASS64;
  ehdr_.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr_.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr_.e_ident[EI_OSABI] = ELFOSABI_SYSV;
  ehdr_.e_type = ET_REL;
  ehdr_.e_machine = EM_X86_64;
  ehdr_.e_version = EV_CURRENT;
  ehdr_.e_ehsize = sizeof(ehdr_);
  ehdr_.e_shoff = sizeof(ehdr_);
  ehdr_.e_shentsize = sizeof(Elf64_Shdr);

  // Add null symbol as first symbol.
  AddSymbol("");

  // Add null section as first section.
  AddSection("", SHT_NULL);

  // Add section for symbol table.
  symtab_ = AddSection(".symtab", SHT_SYMTAB);
}

Elf::~Elf() {
  for (auto *section : sections_) delete section;
  for (auto *symbol : symbols_) delete symbol;
}

Elf::Section *Elf::AddSection(const char *name, Elf64_Word type) {
  // Allocate new section.
  Section *section = new Section(sections_.size());
  sections_.push_back(section);

  // Set section name.
  int namelen = strlen(name);
  section->hdr.sh_name = section_names_.size();
  section_names_.append(name, namelen + 1);

  // Set section type.
  section->hdr.sh_type = type;
  if (type != SHT_NULL) section->hdr.sh_addralign = 1;

  // Add symbol for section.
  if (type == SHT_PROGBITS) {
    section->symidx = AddSymbol("", section, STB_LOCAL, STT_SECTION)->index;
  }

  return section;
}

Elf::Symbol *Elf::AddSymbol(const char *name) {
  // Allocate new symbol.
  Symbol *symbol = new Symbol(symbols_.size());
  symbols_.push_back(symbol);

  // Set symbol name.
  int namelen = strlen(name);
  if (namelen == 0 && !symbol_names_.empty()) {
    symbol->sym.st_name = 0;
  } else {
    symbol->sym.st_name = symbol_names_.size();
    symbol_names_.append(name, namelen + 1);
  }

  return symbol;
}

Elf::Symbol *Elf::AddSymbol(const char *name, Section *section,
                            int bind, int type, int size) {
  Symbol *symbol = AddSymbol(name);
  symbol->sym.st_info = ELF64_ST_INFO(bind, type);
  if (section != nullptr) {
    symbol->sym.st_shndx = section->index;
  }
  symbol->sym.st_size = size;
  return symbol;
}

void Elf::Update() {
  // Build symbol table section.
  for (int i = 0; i < symbols_.size(); ++i) {
    // Copy symbol.
    symbol_data_.push_back(symbols_[i]->sym);

    // Set first non-local symbol. Local symbols must come before non-local
    // symbols in the symbol table.
    if (ELF64_ST_BIND(symbols_[i]->sym.st_info) == STB_LOCAL) {
      symtab_->hdr.sh_info = i + 1;
    }
  }
  symtab_->data = symbol_data_.data();
  symtab_->hdr.sh_size = symbol_data_.size() * sizeof(Elf64_Sym);
  symtab_->hdr.sh_entsize = sizeof(Elf64_Sym);
  symtab_->hdr.sh_addralign = 8;

  // Build symbol name string table.
  Section *strtab = AddSection(".strtab", SHT_STRTAB);
  strtab->hdr.sh_size = symbol_names_.size();
  strtab->data = symbol_names_.data();
  symtab_->hdr.sh_link = strtab->index;

  // Build section name string table.
  Section *shstrtab = AddSection(".shstrtab", SHT_STRTAB);
  shstrtab->hdr.sh_size = section_names_.size();
  shstrtab->data = section_names_.data();

  // Set number of sections in header.
  ehdr_.e_shstrndx = shstrtab->index;
  ehdr_.e_shnum = sections_.size();
}

void Elf::Write(const char *filename) {
  // Open output file.
  FILE *f = fopen(filename, "w");
  if (!f) {
    perror(filename);
    abort();
  }

  // Write ELF header.
  fwrite(&ehdr_, 1, sizeof(Elf64_Ehdr), f);

  // Write section headers.
  int offset = sizeof(Elf64_Ehdr) + ehdr_.e_shnum * sizeof(Elf64_Shdr);
  for (Section *section : sections_) {
    if (section->data != nullptr) {
      section->hdr.sh_offset = offset;
      offset += section->hdr.sh_size;
    }
    fwrite(&section->hdr, 1, sizeof(Elf64_Shdr), f);
  }

  // Write section data.
  for (Section *section : sections_) {
    if (section->data != nullptr) {
      fwrite(section->data, 1, section->hdr.sh_size, f);
    }
  }

  // Close output file.
  fclose(f);
}

