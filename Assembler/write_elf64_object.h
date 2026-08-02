#include <elf.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
    size_t lineno = 0;       // 修复 Bug-A4: 默认初始化防止未初始化读取
    SectionKind sec = SEC_NONE;
    uint32_t offset = 0;
    vector<string> toks;//指令及操作数token化
};

struct Label{
    string name;
    SectionKind sec = SEC_NONE;  // 修复 Bug-A4
    uint32_t addr = 0;            // 修复 Bug-A4
    bool is_global = false;  // .globl 声明的符号为 true
};

// 重定位条目（汇编阶段用符号名，写ELF时转为符号索引）
struct RelocEntry {
    uint32_t offset = 0;    // 修复 Bug-A4
    uint32_t type = 0;      // R_RISCV_* 重定位类型
    string   sym_name;  // 引用的符号名
    int64_t  addend = 0;    // 加数
};

static inline uint64_t align_up(uint64_t v, uint64_t a){ return (v + a - 1) & ~(a - 1); }

void write_elf64_object(
    const string &filename,
    const vector<uint8_t> &textbin,
    const vector<uint8_t> &databin,
    const unordered_map<string, Label> &symtab_map,
    const vector<RelocEntry> &text_relocs,
    const vector<RelocEntry> &data_relocs)
{
    // --- 收集未定义符号（relocs中引用但symtab_map中没有的） ---
    unordered_set<string> undef_names;
    for(auto &r : text_relocs)
        if(symtab_map.find(r.sym_name) == symtab_map.end())
            undef_names.insert(r.sym_name);
    for(auto &r : data_relocs)
        if(symtab_map.find(r.sym_name) == symtab_map.end())
            undef_names.insert(r.sym_name);

    bool has_text_reloc = !text_relocs.empty();
    bool has_data_reloc = !data_relocs.empty();

    // --- SECTION NAMES (shstrtab) ---
    vector<string> shnames = { "", ".text", ".data", ".bss", ".symtab", ".strtab", ".shstrtab" };
    if(has_text_reloc) shnames.push_back(".rela.text");
    if(has_data_reloc) shnames.push_back(".rela.data");

    unordered_map<string,uint32_t> shname_offset;
    uint32_t shs_off_acc = 1;
    for(size_t i=1;i<shnames.size();++i){
        shname_offset[shnames[i]] = shs_off_acc;
        shs_off_acc += (uint32_t)shnames[i].size() + 1;
    }

    // --- SYMBOL NAMES (.strtab) ---
    // ELF规范：local符号必须在global之前
    vector<string> local_syms, global_syms;
    for(auto &p : symtab_map){
        if(p.second.is_global) global_syms.push_back(p.first);
        else local_syms.push_back(p.first);
    }
    for(auto &n : undef_names) global_syms.push_back(n); // 未定义符号都是global
    sort(local_syms.begin(), local_syms.end());
    sort(global_syms.begin(), global_syms.end());

    vector<string> symnames;
    symnames.push_back("");  // null symbol (index 0)
    for(auto &n : local_syms) symnames.push_back(n);
    for(auto &n : global_syms) symnames.push_back(n);
    uint32_t first_global_idx = 1 + local_syms.size(); // sh_info值

    // 符号名 -> 符号表索引
    unordered_map<string,uint32_t> sym_index;
    for(size_t i=0;i<symnames.size();++i)
        sym_index[symnames[i]] = (uint32_t)i;

    unordered_map<string,uint32_t> symname_offset;
    uint32_t symstr_off_acc = 1;
    for(size_t i=1;i<symnames.size();++i){
        symname_offset[symnames[i]] = symstr_off_acc;
        symstr_off_acc += (uint32_t)symnames[i].size() + 1;
    }

    // --- build shstrtab / strtab bytes ---
    vector<uint8_t> shstrtab;
    for(auto &s : shnames){ shstrtab.insert(shstrtab.end(), s.begin(), s.end()); shstrtab.push_back(0); }
    vector<uint8_t> strtab;
    for(auto &s : symnames){ strtab.insert(strtab.end(), s.begin(), s.end()); strtab.push_back(0); }

    // --- build symbol table (Elf64_Sym) ---
    vector<Elf64_Sym> symtab_vec;
    symtab_vec.push_back(Elf64_Sym{}); // null symbol

    for(size_t i=1;i<symnames.size();++i){
        const string &nm = symnames[i];
        Elf64_Sym sym{};
        sym.st_name = symname_offset[nm];
        sym.st_size = 0;

        auto it = symtab_map.find(nm);
        if(it != symtab_map.end()){
            // 已定义符号
            const Label &lab = it->second;
            sym.st_value = lab.addr;
            unsigned char bind = lab.is_global ? STB_GLOBAL : STB_LOCAL;
            unsigned char type = (lab.sec==SEC_TEXT ? STT_FUNC : STT_OBJECT);
            sym.st_info = ELF64_ST_INFO(bind, type);
            if(lab.sec == SEC_TEXT) sym.st_shndx = 1;
            else if(lab.sec == SEC_DATA) sym.st_shndx = 2;
            else sym.st_shndx = SHN_ABS;
        } else {
            // 未定义符号（外部符号）
            sym.st_value = 0;
            sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            sym.st_shndx = SHN_UNDEF;
        }
        symtab_vec.push_back(sym);
    }

    // --- build relocation entries (Elf64_Rela) ---
    auto build_rela_vec = [&](const vector<RelocEntry> &relocs) -> vector<Elf64_Rela> {
        vector<Elf64_Rela> relas;
        for(auto &r : relocs){
            Elf64_Rela rela{};
            rela.r_offset = r.offset;
            uint32_t si = sym_index.count(r.sym_name) ? sym_index[r.sym_name] : 0;
            rela.r_info = ELF64_R_INFO(si, r.type);
            rela.r_addend = r.addend;
            relas.push_back(rela);
        }
        return relas;
    };
    auto text_relas = build_rela_vec(text_relocs);
    auto data_relas = build_rela_vec(data_relocs);

    // --- compute section layout ---
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

    uint64_t cur = sizeof(Elf64_Ehdr);
    uint64_t text_off = align_up(cur, 0x10); cur = text_off + textbin.size();
    uint64_t data_off = align_up(cur, 0x10); cur = data_off + databin.size();
    uint64_t bss_off  = align_up(cur, 0x10);
    uint64_t symtab_off = align_up(cur, 0x8); cur = symtab_off + symtab_vec.size()*sizeof(Elf64_Sym);
    uint64_t strtab_off = align_up(cur, 0x1); cur = strtab_off + strtab.size();
    uint64_t shstrtab_off = align_up(cur, 0x1); cur = shstrtab_off + shstrtab.size();

    uint64_t rela_text_off = 0, rela_data_off = 0;
    if(has_text_reloc){
        rela_text_off = align_up(cur, 0x8);
        cur = rela_text_off + text_relas.size() * sizeof(Elf64_Rela);
    }
    if(has_data_reloc){
        rela_data_off = align_up(cur, 0x8);
        cur = rela_data_off + data_relas.size() * sizeof(Elf64_Rela);
    }

    uint64_t shoff = align_up(cur, 0x8);
    ehdr.e_shoff = shoff;

    // --- section headers ---
    vector<Elf64_Shdr> shdrs(shnames.size());
    shdrs[0] = Elf64_Shdr{};

    // .text (1)
    shdrs[1].sh_name = shname_offset[".text"];
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_offset = text_off;
    shdrs[1].sh_size = textbin.size();
    shdrs[1].sh_addralign = 0x10;

    // .data (2)
    shdrs[2].sh_name = shname_offset[".data"];
    shdrs[2].sh_type = SHT_PROGBITS;
    shdrs[2].sh_flags = SHF_ALLOC | SHF_WRITE;
    shdrs[2].sh_offset = data_off;
    shdrs[2].sh_size = databin.size();
    shdrs[2].sh_addralign = 0x8;

    // .bss (3)
    shdrs[3].sh_name = shname_offset[".bss"];
    shdrs[3].sh_type = SHT_NOBITS;
    shdrs[3].sh_flags = SHF_ALLOC | SHF_WRITE;
    shdrs[3].sh_offset = bss_off;
    shdrs[3].sh_size = 0;
    shdrs[3].sh_addralign = 0x8;

    // .symtab (4)
    shdrs[4].sh_name = shname_offset[".symtab"];
    shdrs[4].sh_type = SHT_SYMTAB;
    shdrs[4].sh_offset = symtab_off;
    shdrs[4].sh_size = symtab_vec.size() * sizeof(Elf64_Sym);
    shdrs[4].sh_link = 5; // -> .strtab
    shdrs[4].sh_info = first_global_idx; // 第一个global符号的索引
    shdrs[4].sh_addralign = 8;
    shdrs[4].sh_entsize = sizeof(Elf64_Sym);

    // .strtab (5)
    shdrs[5].sh_name = shname_offset[".strtab"];
    shdrs[5].sh_type = SHT_STRTAB;
    shdrs[5].sh_offset = strtab_off;
    shdrs[5].sh_size = strtab.size();
    shdrs[5].sh_addralign = 1;

    // .shstrtab (6)
    shdrs[6].sh_name = shname_offset[".shstrtab"];
    shdrs[6].sh_type = SHT_STRTAB;
    shdrs[6].sh_offset = shstrtab_off;
    shdrs[6].sh_size = shstrtab.size();
    shdrs[6].sh_addralign = 1;

    // .rela.text (7) -- sh_link=4(.symtab), sh_info=1(.text)
    if(has_text_reloc){
        shdrs[7].sh_name = shname_offset[".rela.text"];
        shdrs[7].sh_type = SHT_RELA;
        shdrs[7].sh_flags = SHF_INFO_LINK;
        shdrs[7].sh_offset = rela_text_off;
        shdrs[7].sh_size = text_relas.size() * sizeof(Elf64_Rela);
        shdrs[7].sh_link = 4;
        shdrs[7].sh_info = 1;
        shdrs[7].sh_addralign = 8;
        shdrs[7].sh_entsize = sizeof(Elf64_Rela);
    }

    // .rela.data (7或8) -- sh_link=4(.symtab), sh_info=2(.data)
    if(has_data_reloc){
        int idx = has_text_reloc ? 8 : 7;
        shdrs[idx].sh_name = shname_offset[".rela.data"];
        shdrs[idx].sh_type = SHT_RELA;
        shdrs[idx].sh_flags = SHF_INFO_LINK;
        shdrs[idx].sh_offset = rela_data_off;
        shdrs[idx].sh_size = data_relas.size() * sizeof(Elf64_Rela);
        shdrs[idx].sh_link = 4;
        shdrs[idx].sh_info = 2;
        shdrs[idx].sh_addralign = 8;
        shdrs[idx].sh_entsize = sizeof(Elf64_Rela);
    }

    ehdr.e_shnum = (uint16_t)shdrs.size();
    ehdr.e_shstrndx = 6;

    // --- write to file ---
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

    write_zeros_to(text_off);
    if(!textbin.empty()) ofs.write(reinterpret_cast<const char*>(textbin.data()), textbin.size());

    write_zeros_to(data_off);
    if(!databin.empty()) ofs.write(reinterpret_cast<const char*>(databin.data()), databin.size());

    write_zeros_to(symtab_off);
    if(!symtab_vec.empty())
        ofs.write(reinterpret_cast<const char*>(symtab_vec.data()), symtab_vec.size()*sizeof(Elf64_Sym));

    write_zeros_to(strtab_off);
    if(!strtab.empty()) ofs.write(reinterpret_cast<const char*>(strtab.data()), strtab.size());

    write_zeros_to(shstrtab_off);
    if(!shstrtab.empty()) ofs.write(reinterpret_cast<const char*>(shstrtab.data()), shstrtab.size());

    if(has_text_reloc){
        write_zeros_to(rela_text_off);
        ofs.write(reinterpret_cast<const char*>(text_relas.data()), text_relas.size()*sizeof(Elf64_Rela));
    }
    if(has_data_reloc){
        write_zeros_to(rela_data_off);
        ofs.write(reinterpret_cast<const char*>(data_relas.data()), data_relas.size()*sizeof(Elf64_Rela));
    }

    write_zeros_to(shoff);
    ofs.write(reinterpret_cast<const char*>(shdrs.data()), shdrs.size()*sizeof(Elf64_Shdr));

    ofs.close();
}
