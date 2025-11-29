#include <elf.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cstdint>

using namespace std;

//记录每条指令或符号所属的section,便于计算offset生成最终节表
enum SectionKind{
    SEC_NONE=0,
    SEC_TEXT=1,
    SEC_DATA=2
};
//记录源.s文件的行号和原始文本
struct LineInfo{
    size_t lineno;
    string raw;
};

struct Instr{
    size_t lineno;
    SectionKind sec;
    uint32_t offset;
    vector<string> toks;//指令及操作数token化
};

struct Label{
    string name;
    SectionKind sec;
    uint32_t addr;
};

static inline uint64_t align_up(uint64_t v, uint64_t a){ return (v + a - 1) & ~(a - 1); }

void write_elf64_object(
    const string &filename,
    const vector<uint8_t> &textbin,
    const vector<uint8_t> &databin,
    const unordered_map<string, Label> &symtab_map)
{
    // --- SECTION NAMES (shstrtab) ---
    vector<string> shnames = { "", ".text", ".data", ".bss", ".symtab", ".strtab", ".shstrtab" };
    unordered_map<string,uint32_t> shname_offset;
    uint32_t shs_off_acc = 1; // first byte reserved for empty string
    for(size_t i=1;i<shnames.size();++i){
        shname_offset[shnames[i]] = shs_off_acc;
        shs_off_acc += (uint32_t)shnames[i].size() + 1;
    }

    // --- SYMBOL NAME STRTAB (.strtab for sym names) ---
    // deterministic order: collect, sort
    vector<string> symnames;
    symnames.push_back(""); // first empty
    for(auto &p : symtab_map) symnames.push_back(p.first);
    sort(symnames.begin()+1, symnames.end()); // keep index 0 empty, sort rest deterministically

    unordered_map<string,uint32_t> symname_offset;
    uint32_t symstr_off_acc = 1;
    for(size_t i=1;i<symnames.size();++i){
        symname_offset[symnames[i]] = symstr_off_acc;
        symstr_off_acc += (uint32_t)symnames[i].size() + 1;
    }

    // --- construct shstrtab bytes and strtab bytes ---
    vector<uint8_t> shstrtab;
    for(auto &s : shnames){ shstrtab.insert(shstrtab.end(), s.begin(), s.end()); shstrtab.push_back(0); }

    vector<uint8_t> strtab; // symbol string table (.strtab)
    for(auto &s : symnames){ strtab.insert(strtab.end(), s.begin(), s.end()); strtab.push_back(0); }

    // --- build symbol table entries (Elf64_Sym) ---
    vector<Elf64_Sym> symtab_vec;
    symtab_vec.push_back(Elf64_Sym{}); // null symbol

    // iterate symnames to keep deterministic order (skip index 0)
    for(size_t i=1;i<symnames.size();++i){
        auto it = symtab_map.find(symnames[i]);
        if(it == symtab_map.end()){
            // shouldn't happen
            continue;
        }
        const Label &lab = it->second;
        Elf64_Sym sym{};
        sym.st_name = symname_offset[symnames[i]];
        sym.st_value = lab.addr;
        sym.st_size = 0;
        // binding: for now make globals; if you want local handling, extend Label with bind info
        unsigned char bind = STB_GLOBAL;
        unsigned char type = (lab.sec==SEC_TEXT ? STT_FUNC : STT_OBJECT);
        sym.st_info = ELF64_ST_INFO(bind, type);
        // section index: match our shdr indices below (.text index 1, .data index 2)
        if(lab.sec == SEC_TEXT) sym.st_shndx = 1;
        else if(lab.sec == SEC_DATA) sym.st_shndx = 2;
        else sym.st_shndx = SHN_ABS;
        symtab_vec.push_back(sym);
    }

    // --- compute section layout offsets ---
    // start right after ELF header
    Elf64_Ehdr ehdr{};
    ehdr.e_ident[EI_MAG0] = 0x7f;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_RISCV;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    // e_shnum and e_shstrndx set later after shdrs constructed

    uint64_t cur = sizeof(Elf64_Ehdr);
    uint64_t text_off = align_up(cur, 0x10); cur = text_off + textbin.size();
    uint64_t data_off = align_up(cur, 0x10); cur = data_off + databin.size();
    uint64_t bss_off  = align_up(cur, 0x8);  /* bss size = 0 for now */ cur = bss_off + 0;
    uint64_t symtab_off = align_up(cur, 0x8); cur = symtab_off + symtab_vec.size()*sizeof(Elf64_Sym);
    uint64_t strtab_off = align_up(cur, 0x1); cur = strtab_off + strtab.size();
    uint64_t shstrtab_off = align_up(cur, 0x1); cur = shstrtab_off + shstrtab.size();
    uint64_t shoff = align_up(cur, 0x8);

    ehdr.e_shoff = shoff;

    // --- section headers ---
    vector<Elf64_Shdr> shdrs(shnames.size());
    // [0] null
    shdrs[0] = Elf64_Shdr{};

    // .text (index 1)
    shdrs[1].sh_name = shname_offset[".text"];
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_addr = 0;
    shdrs[1].sh_offset = text_off;
    shdrs[1].sh_size = textbin.size();
    shdrs[1].sh_addralign = 0x10;

    // .data (index 2)
    shdrs[2].sh_name = shname_offset[".data"];
    shdrs[2].sh_type = SHT_PROGBITS;
    shdrs[2].sh_flags = SHF_ALLOC | SHF_WRITE;
    shdrs[2].sh_offset = data_off;
    shdrs[2].sh_size = databin.size();
    shdrs[2].sh_addralign = 0x8;

    // .bss (index 3)
    shdrs[3].sh_name = shname_offset[".bss"];
    shdrs[3].sh_type = SHT_NOBITS;
    shdrs[3].sh_flags = SHF_ALLOC | SHF_WRITE;
    shdrs[3].sh_offset = bss_off;
    shdrs[3].sh_size = 0; // keep zero for now
    shdrs[3].sh_addralign = 0x8;

    // .symtab (index 4)
    shdrs[4].sh_name = shname_offset[".symtab"];
    shdrs[4].sh_type = SHT_SYMTAB;
    shdrs[4].sh_flags = 0;
    shdrs[4].sh_offset = symtab_off;
    shdrs[4].sh_size = symtab_vec.size() * sizeof(Elf64_Sym);
    shdrs[4].sh_link = 5; // link to .strtab (index 5)
    // sh_info = one greater than the symbol table index of the last local symbol
    // we only have the NULL local symbol at index 0 -> sh_info = 1
    shdrs[4].sh_info = 1;
    shdrs[4].sh_addralign = 8;
    shdrs[4].sh_entsize = sizeof(Elf64_Sym);

    // .strtab (index 5)
    shdrs[5].sh_name = shname_offset[".strtab"];
    shdrs[5].sh_type = SHT_STRTAB;
    shdrs[5].sh_offset = strtab_off;
    shdrs[5].sh_size = strtab.size();
    shdrs[5].sh_addralign = 1;

    // .shstrtab (index 6)
    shdrs[6].sh_name = shname_offset[".shstrtab"];
    shdrs[6].sh_type = SHT_STRTAB;
    shdrs[6].sh_offset = shstrtab_off;
    shdrs[6].sh_size = shstrtab.size();
    shdrs[6].sh_addralign = 1;

    // final header fields
    ehdr.e_shnum = (uint16_t)shdrs.size();
    // index of .shstrtab in our shdrs vector (we placed it at index 6)
    ehdr.e_shstrndx = 6;

    // --- write to file (use tellp to get current position and pad correctly) ---
    ofstream ofs(filename, ios::binary);
    if(!ofs) throw runtime_error("Cannot open output file");

    ofs.write(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));

    auto write_zeros_to = [&](uint64_t target){
        uint64_t curpos = (uint64_t)ofs.tellp();
        if(target > curpos){
            uint64_t n = target - curpos;
            const size_t chunk = 4096;
            vector<char> zeros(min<uint64_t>(n, chunk), 0);
            while(n){
                size_t w = (size_t)min<uint64_t>(n, zeros.size());
                ofs.write(zeros.data(), w);
                n -= w;
            }
        }
    };

    // .text
    write_zeros_to(text_off);
    if(!textbin.empty()) ofs.write(reinterpret_cast<const char*>(textbin.data()), textbin.size());

    // .data
    write_zeros_to(data_off);
    if(!databin.empty()) ofs.write(reinterpret_cast<const char*>(databin.data()), databin.size());

    // .bss : NOBITS -> no bytes written; but ensure file offset moves to symtab_off when writing symtab
    write_zeros_to(symtab_off);

    // .symtab
    if(!symtab_vec.empty()){
        ofs.write(reinterpret_cast<const char*>(symtab_vec.data()), symtab_vec.size()*sizeof(Elf64_Sym));
    }

    // .strtab
    write_zeros_to(strtab_off);
    if(!strtab.empty()) ofs.write(reinterpret_cast<const char*>(strtab.data()), strtab.size());

    // .shstrtab
    write_zeros_to(shstrtab_off);
    if(!shstrtab.empty()) ofs.write(reinterpret_cast<const char*>(shstrtab.data()), shstrtab.size());

    // finally section headers
    write_zeros_to(shoff);
    ofs.write(reinterpret_cast<const char*>(shdrs.data()), shdrs.size()*sizeof(Elf64_Shdr));

    ofs.close();
}
