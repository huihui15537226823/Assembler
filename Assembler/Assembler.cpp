#include <bits/stdc++.h>
#include "write_elf64_object.h"
using namespace std;

vector<string> tokenize_line(const string &s_raw){
    string s=s_raw;
    vector<string> toks;
    string cur;
    auto push=[&](){
        if(!cur.empty()){
            toks.push_back(cur);
            cur.clear();
        }
    };
    for(size_t i=0;i<s.size();){
        char c=s[i];

        // comments - support '#' ';' '//' as comment starts
        if(c=='#' || c==';' || (c=='/' && i+1<s.size() && s[i+1]=='/')) break;

        if(isspace((unsigned char)c)){
            push();
            i++;
            continue;
        }
        // treat comma as separator (do NOT append "," token)
        if(c==','){
            push();
            i++;
            continue;
        }
        // keep ':' '(' ')' as separate tokens (':' used for labels)
        if(c==':' ){
            push();
            toks.push_back(":");
            i++;
            continue;
        }
        if(c=='(' || c==')'){
            push();
            string t(1,c);
            toks.push_back(t);
            i++;
            continue;
        }
        // other chars part of token
        cur.push_back(c);
        i++;
    }
    push();
    return toks;
}


bool is_directive(const string &tok){
    return !tok.empty() && tok[0]=='.';
}

static unordered_map<string, int> abi_to_x = {
    {"zero",0}, {"ra",1}, {"sp",2}, {"gp",3}, {"tp",4},
    {"t0",5},{"t1",6},{"t2",7},
    {"s0",8},{"fp",8},{"s1",9},
    {"a0",10},{"a1",11},{"a2",12},{"a3",13},{"a4",14},{"a5",15},{"a6",16},{"a7",17},
    {"s2",18},{"s3",19},{"s4",20},{"s5",21},{"s6",22},{"s7",23},{"s8",24},{"s9",25},{"s10",26},{"s11",27},
    {"t3",28},{"t4",29},{"t5",30},{"t6",31}
};


int reg_id(const string &r){
    // ABI name
    auto it = abi_to_x.find(r);
    if(it != abi_to_x.end())
        return it->second;

    // xN form
    if(r.size() >= 2 && r[0] == 'x'){
        try{
            int v=stoi(r.substr(1));
            if(v<0||v>31) throw runtime_error("Register out of range");
            return v;
        }catch(exception &e){
            throw runtime_error("Unknown register: " + r);
        }
    }

    throw runtime_error("Unknown register: " + r);
}

//string to longlong
long long parse_imm(const string &s){
    if(s.empty()) throw runtime_error("empty immediate");
    try{
        if(s.size()>2 && s[0]=='0' && (s[1]=='x' || s[1]=='X')) return stoll(s, nullptr, 16);
        return stoll(s, nullptr, 10);
    }catch(const exception &e){
        throw runtime_error(string("Immediate parse error: '") + s + "'");
    }
}

static inline uint32_t pack_r(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode){
    return (funct7<<25) | (rs2<<20) | (rs1<<15) | (funct3<<12) | (rd<<7) | (opcode&0x7f);
}
static inline uint32_t pack_i(int32_t imm, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode){
    uint32_t uimm = ((uint32_t)imm) & 0xfff;
    return (uimm<<20) | (rs1<<15) | (funct3<<12) | (rd<<7) | (opcode&0x7f);
}
static inline uint32_t pack_s(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode){
    uint32_t imm12 = ((uint32_t)imm) & 0xfff;
    uint32_t imm11_5 = (imm12>>5) & 0x7f;
    uint32_t imm4_0  = imm12 & 0x1f;
    return (imm11_5<<25) | (rs2<<20) | (rs1<<15) | (funct3<<12) | (imm4_0<<7) | (opcode&0x7f);
}
static inline uint32_t pack_b(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode){
    // imm is branch offset in bytes; must fit 13-bit signed with low bit zero
    uint32_t imm13 = ((uint32_t)imm) & 0x1fff;
    uint32_t imm12 = (imm13>>12) & 0x1;           // imm[12]
    uint32_t imm10_5 = (imm13>>5) & 0x3f;         // imm[10:5]
    uint32_t imm4_1 = (imm13>>1) & 0xf;           // imm[4:1]
    uint32_t imm11 = (imm13>>11) & 0x1;           // imm[11]
    return (imm12<<31) | (imm10_5<<25) | (rs2<<20) | (rs1<<15) | (funct3<<12) | (imm4_1<<8) | (imm11<<7) | (opcode&0x7f);
}
static inline uint32_t pack_u(int32_t imm, uint32_t rd, uint32_t opcode){
    uint32_t imm20 = ((uint32_t)imm) & 0xfffff000u;
    return (imm20) | (rd<<7) | (opcode&0x7f);
}
static inline uint32_t pack_j(int32_t imm, uint32_t rd, uint32_t opcode){
    // imm is signed 21-bit (imm[20:1] <<1). Pack as:
    uint32_t imm21 = ((uint32_t)imm) & 0x1fffff;
    uint32_t imm20 = (imm21>>20) & 0x1;
    uint32_t imm10_1 = (imm21>>1) & 0x3ff;
    uint32_t imm11 = (imm21>>11) & 0x1;
    uint32_t imm19_12 = (imm21>>12) & 0xff;
    return (imm20<<31) | (imm10_1<<21) | (imm11<<20) | (imm19_12<<12) | (rd<<7) | (opcode&0x7f);
}

//检查 v（带符号整数）是否能用 bits 位的带符号二补数表示
static inline bool fits_signed(long long v, int bits){
    long long lo = -(1LL<<(bits-1));
    long long hi = (1LL<<(bits-1)) - 1;
    return v>=lo && v<=hi;
}

uint32_t encode_r_type(const string &op, int rd, int rs1, int rs2){
    // R-type: add, sub, sll, slt, sltu, xor, srl, sra, or, and
    //opcode=0x33
    //add rd=rs1+rs2; sub rd=rs1-rs2; sll rd=rs1<<(rs2 & mask)(rs2取低位作为移位量)
    if(op=="add") return pack_r(0x00, rs2, rs1, 0x0, rd, 0x33);
    if(op=="sub") return pack_r(0x20, rs2, rs1, 0x0, rd, 0x33);
    if(op=="sll") return pack_r(0x00, rs2, rs1, 0x1, rd, 0x33);
    //slt rd=(rs1<rs2)? 1:0(有符号比较) sltu rd=(rs1<rs2 unsigned)? 1:0
    if(op=="slt") return pack_r(0x00, rs2, rs1, 0x2, rd, 0x33);
    if(op=="sltu")return pack_r(0x00, rs2, rs1, 0x3, rd, 0x33);
    //xor rd=rs1^rs2(异或) srl rd=rs1>>(rs2 & mask)(逻辑右移高位补0)
    if(op=="xor") return pack_r(0x00, rs2, rs1, 0x4, rd, 0x33);
    if(op=="srl") return pack_r(0x00, rs2, rs1, 0x5, rd, 0x33);
    //sra 算术右移 or rd=rs1|rs2 and rd=rs1&rs2
    if(op=="sra") return pack_r(0x20, rs2, rs1, 0x5, rd, 0x33);
    if(op=="or")  return pack_r(0x00, rs2, rs1, 0x6, rd, 0x33);
    if(op=="and") return pack_r(0x00, rs2, rs1, 0x7, rd, 0x33);
    throw runtime_error("Unknown R-type: "+op);
}

uint32_t encode_i_type(const string &op, int rd, int rs1, long long imm){
    //addi rd=rs1+imm
    if(op=="addi"){
        if(!fits_signed(imm,12)) throw runtime_error("addi immediate out of range");
        return pack_i((int32_t)imm, rs1, 0x0, rd, 0x13);
    }
    //slti rd, rs1, imm 设置 rd 为 1/0，比较 rs1 与立即数（有符号/无符号）
    if(op=="slti"){
        if(!fits_signed(imm,12)) throw runtime_error("slti imm out of range");
        return pack_i((int32_t)imm, rs1, 0x2, rd, 0x13);
    }
    if(op=="sltiu"){
        if(!fits_signed(imm,12)) throw runtime_error("sltiu imm out of range");
        return pack_i((int32_t)imm, rs1, 0x3, rd, 0x13);
    }
    if(op=="xori"){
        if(!fits_signed(imm,12)) throw runtime_error("xori imm out of range");
        return pack_i((int32_t)imm, rs1, 0x4, rd, 0x13);
    }
    if(op=="ori"){
        if(!fits_signed(imm,12)) throw runtime_error("ori imm out of range");
        return pack_i((int32_t)imm, rs1, 0x6, rd, 0x13);
    }
    if(op=="andi"){
        if(!fits_signed(imm,12)) throw runtime_error("andi imm out of range");
        return pack_i((int32_t)imm, rs1, 0x7, rd, 0x13);
    }
    if(op=="slli"){
        // rd = rs1 << shamt
        if(imm < 0 || imm > 63) throw runtime_error("slli shamt out of range");
        uint32_t funct7 = 0x00;
        uint32_t funct3 = 0x1;
        uint32_t opcode = 0x13;
        uint32_t shamt = (uint32_t)imm;
        return (funct7<<25) | (shamt<<20) | (rs1<<15) | (funct3<<12) | (rd<<7) | opcode;
    }
    if(op=="srli"){
        //逻辑右移
        if(imm < 0 || imm > 63) throw runtime_error("srli shamt out of range");
        uint32_t funct7 = 0x00;
        uint32_t funct3 = 0x5;
        uint32_t opcode = 0x13;
        uint32_t shamt = (uint32_t)imm;
        return (funct7<<25) | (shamt<<20) | (rs1<<15) | (funct3<<12) | (rd<<7) | opcode;
    }
    //算术右移(保留符号)
    if(op=="srai"){
        if(imm < 0 || imm > 63) throw runtime_error("srai shamt out of range");
        uint32_t funct7 = 0x20;
        uint32_t funct3 = 0x5;
        uint32_t opcode = 0x13;
        uint32_t shamt = (uint32_t)imm;
        return (funct7<<25) | (shamt<<20) | (rs1<<15) | (funct3<<12) | (rd<<7) | opcode;
    }
    // loads
    //lb 从 rs1 + imm 读取 1 字节，有符号扩展到寄存器宽度 lh(2字节) 有符号拓展
    //lw(4字节) ld(8字节)
    if(op=="lb") { if(!fits_signed(imm,12)) throw runtime_error("lb imm out of range"); return pack_i((int32_t)imm, rs1, 0x0, rd, 0x03); }
    if(op=="lh") { if(!fits_signed(imm,12)) throw runtime_error("lh imm out of range"); return pack_i((int32_t)imm, rs1, 0x1, rd, 0x03); }
    if(op=="lw") { if(!fits_signed(imm,12)) throw runtime_error("lw imm out of range"); return pack_i((int32_t)imm, rs1, 0x2, rd, 0x03); }
    if(op=="ld") { if(!fits_signed(imm,12)) throw runtime_error("ld imm out of range"); return pack_i((int32_t)imm, rs1, 0x3, rd, 0x03); }
    // jalr rd->PC+4(返回地址) 跳转到(rs1+imm) & ~1(把最低位清0)
    if(op=="jalr"){
        if(!fits_signed(imm,12)) throw runtime_error("jalr imm out of range");
        return pack_i((int32_t)imm, rs1, 0x0, rd, 0x67);
    }
    throw runtime_error("Unknown I-type: "+op);
}

uint32_t encode_s_type(const string &op, int rs2, int rs1, long long imm){
    //写入1248字节
    if(op=="sb"){ if(!fits_signed(imm,12)) throw runtime_error("sb imm out of range"); return pack_s((int32_t)imm, rs2, rs1, 0x0, 0x23); }
    if(op=="sh"){ if(!fits_signed(imm,12)) throw runtime_error("sh imm out of range"); return pack_s((int32_t)imm, rs2, rs1, 0x1, 0x23); }
    if(op=="sw"){ if(!fits_signed(imm,12)) throw runtime_error("sw imm out of range"); return pack_s((int32_t)imm, rs2, rs1, 0x2, 0x23); }
    if(op=="sd"){ if(!fits_signed(imm,12)) throw runtime_error("sd imm out of range"); return pack_s((int32_t)imm, rs2, rs1, 0x3, 0x23); }
    throw runtime_error("Unknown S-type: "+op);
}

uint32_t encode_b_type(const string &op, int rs1, int rs2, long long rel){
    if(!fits_signed(rel,13)) throw runtime_error("branch offset out of range");
    if((rel & 0x1) != 0) throw runtime_error("branch offset not aligned");
    //beq rs1 rs2 label =时跳转;bne 不等;blt 有符号小于; bge有符号大于等于;
    if(op=="beq") return pack_b((int32_t)rel, rs2, rs1, 0x0, 0x63);
    if(op=="bne") return pack_b((int32_t)rel, rs2, rs1, 0x1, 0x63);
    if(op=="blt") return pack_b((int32_t)rel, rs2, rs1, 0x4, 0x63);
    if(op=="bge") return pack_b((int32_t)rel, rs2, rs1, 0x5, 0x63);
    if(op=="bltu")return pack_b((int32_t)rel, rs2, rs1, 0x6, 0x63);
    if(op=="bgeu")return pack_b((int32_t)rel, rs2, rs1, 0x7, 0x63);
    throw runtime_error("Unknown B-type: "+op);
}

uint32_t encode_u_type(const string &op, int rd, long long imm){
    //lui rd,imm 将 imm[31:12] 放进 rd 的高 20 位，rd 的低 12 位清 0。
    //等价于 rd = imm & 0xfffff000。常用于构建大常数（与 addi 配合使用）
    if(op=="lui") return pack_u((int32_t)imm, rd, 0x37);
    //rd = PC + imm（imm = high 20 bits << 12）。
    //常用于 position independent addressing（例如生成 PC 相对地址高位）
    if(op=="auipc") return pack_u((int32_t)imm, rd, 0x17);
    throw runtime_error("Unknown U-type: "+op);
}

uint32_t encode_j_type(const string &op, int rd, long long rel){
    if(!fits_signed(rel,21)) throw runtime_error("jal offset out of range");
    if((rel & 0x1) != 0) throw runtime_error("jal offset not aligned");
    //rd = PC + 4（保存返回地址），然后 PC = PC + imm 跳转（imm 是带符号 21-bit，最低位隐含 0）
    if(op=="jal") return pack_j((int32_t)rel, rd, 0x6f);
    throw runtime_error("Unknown J-type: "+op);
}
//解析立即数偏移寻址
pair<long long,int> parse_mem_operand(const vector<string> &toks, int startIdx){
    if(startIdx+3 >= (int)toks.size()) throw runtime_error("bad memory operand");
    string imm_s = toks[startIdx];
    string lpar = toks[startIdx+1];
    string reg_s = toks[startIdx+2];
    string rpar = toks[startIdx+3];
    if(lpar != "(" || rpar != ")") throw runtime_error("bad memory operand parentheses");
    long long imm = parse_imm(imm_s);
    int base = reg_id(reg_s);
    return {imm, base};
}


int main(int argc, char**argv){
    //关闭cpp的iostream与c标准库同步,禁用自动刷新,加速i/o对于大文件处理更快,不影响解析逻辑,性能优化

    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if(argc<2){
        cerr<<"Usage: "<<argv[0]<<" input.s\n";
        return 1;
    }

    string infile = argv[1];
    ifstream ifs(infile);
    if(!ifs){ cerr<<"Cannot open "<<infile<<"\n"; return 2; }

    vector<LineInfo> lines;
    string raw;
    size_t lineno=0;
    while(getline(ifs,raw)){
        lineno++;
        lines.push_back({lineno,raw});
    }

    //Data structures for first pass
    SectionKind cursec=SEC_NONE;
    uint32_t text_off=0;
    uint32_t data_off=0;
    vector<Instr> instrs;
    unordered_map<string,Label> symtab;

    for(auto&L :lines){
        auto toks=tokenize_line(L.raw);
        if(toks.empty()) continue;

        if(toks.size()>=2 && toks[1]==":"){
            string lbl=toks[0];
            uint32_t addr = (cursec==SEC_TEXT? text_off : (cursec==SEC_DATA? data_off : 0));
            symtab[lbl]=Label{lbl,cursec,addr};

            vector<string> rest;
            for(size_t i=2;i<toks.size();++i) rest.push_back(toks[i]);
            if(rest.empty()) continue;
            toks=rest;
        }
        if(is_directive(toks[0])){
            string d = toks[0];
            if(d==".text"){
                cursec = SEC_TEXT;
                continue;
            } else if(d==".data"){
                cursec = SEC_DATA;
                continue;
            } else if(d==".globl" || d==".global"){
                // ignore for first pass (could mark global)
                continue;
            } else if(d==".align"){
                if(toks.size()>=2){
                    long long val = stoll(toks[1]);
                    uint32_t align = (1u<<val);
                    if(cursec==SEC_TEXT) text_off = ( (text_off + align - 1) / align ) * align;
                    if(cursec==SEC_DATA) data_off = ( (data_off + align - 1) / align ) * align;
                }
                continue;
            }else if(d==".word"){
                if(cursec!=SEC_DATA){
                    cerr<<"Warning .word outside .data at line "<<L.lineno<<"\n";
                }
                Instr ins;
                ins.lineno=L.lineno;
                ins.sec=SEC_DATA;
                ins.offset=data_off;
                ins.toks=toks;
                instrs.push_back(ins);
                data_off+=4;
                continue;
            }else{
                continue;
            }
        }
        if(cursec==SEC_NONE){
            cerr<<"Error: instruction outside any section at line "<<L.lineno<<"\n";
            return 3;
        }

        string op=toks[0];
        //mv rd,rs ->addi rd,rs,0
        if(op=="mv"){
            if(toks.size()<3) { 
                cerr<<"mv operand error line "<<L.lineno<<"\n"; return 4; 
            }
            vector<string> t1={"addi",toks[1],toks[2],"0"};
            Instr ins; ins.lineno=L.lineno; ins.sec=cursec; ins.toks=t1;
            if(cursec==SEC_TEXT){ ins.offset = text_off; instrs.push_back(ins); text_off+=4; }
            else { ins.offset = data_off; instrs.push_back(ins); data_off+=4; }
            continue;
        }
        //jr ra ->jalr x0,ra,0
        if(op=="jr"){
            if(toks.size()<2) { cerr<<"jr operand error line "<<L.lineno<<"\n"; return 4; }
            vector<string> t1 = {"jalr", "x0", toks[1], "0"}; // rd x0, rs1, imm
            Instr ins; ins.lineno=L.lineno; ins.sec=cursec; ins.toks=t1;
            if(cursec==SEC_TEXT){ ins.offset=text_off; instrs.push_back(ins); text_off+=4; }
            else { ins.offset=data_off; instrs.push_back(ins); data_off+=4; }
            continue;
        }
        //li rd, imm  -> either addi rd, x0, imm  (if fits) OR lui+addi
        if(op=="li"){
            if(toks.size()<3){ cerr<<"li operand error line "<<L.lineno<<"\n"; return 4; }
            long long imm = parse_imm(toks[2]);
            if(fits_signed(imm,12)){
                vector<string> t1 = {"addi", toks[1], "x0", toks[2]};
                Instr ins; ins.lineno=L.lineno; ins.sec=cursec; ins.toks=t1;
                if(cursec==SEC_TEXT){ ins.offset=text_off; instrs.push_back(ins); text_off+=4; }
                else { ins.offset=data_off; instrs.push_back(ins); data_off+=4; }
                continue;
            } else {
                // expand to LUI + ADDI
                // compute imm_hi = (imm + 0x800) >> 12  (rounding)
                long long imm_hi = (imm + (1LL<<11)) >> 12;
                long long imm_lo = imm - (imm_hi<<12);
                long long lui_imm = imm_hi << 12;
                // LUI rd, imm_hi<<12  (we'll pass full imm)
                vector<string> t_lui = {"lui", toks[1], to_string(lui_imm)};
                vector<string> t_addi = {"addi", toks[1], toks[1], to_string(imm_lo)};
                Instr ins1; ins1.lineno=L.lineno; ins1.sec=cursec; ins1.toks=t_lui;
                Instr ins2; ins2.lineno=L.lineno; ins2.sec=cursec; ins2.toks=t_addi;
                if(cursec==SEC_TEXT){
                    ins1.offset=text_off; instrs.push_back(ins1); text_off+=4;
                    ins2.offset=text_off; instrs.push_back(ins2); text_off+=4;
                } else {
                    ins1.offset=data_off; instrs.push_back(ins1); data_off+=4;
                    ins2.offset=data_off; instrs.push_back(ins2); data_off+=4;
                }
                continue;
            }
        }

        Instr ins;
        ins.lineno=L.lineno;
        ins.sec=cursec;
        ins.toks=toks;

        if(cursec==SEC_TEXT){
            ins.offset=text_off;
            text_off+=4;
        }else if(cursec==SEC_DATA){
            ins.offset=data_off;
            data_off+=4;
        }
        instrs.push_back(move(ins));
    }    

    //first pass done
    // Print first pass summary (optional)
    cout<<"=== First pass results ===\n";

    vector<uint8_t> textout(max<uint32_t>(1,text_off),0);
    vector<uint8_t> dataout(max<uint32_t>(1,data_off),0);

    auto get_label_addr=[&](const string &Lname)->uint32_t{
        auto it=symtab.find(Lname);
        if(it==symtab.end()){
            throw runtime_error("Undefined label: "+Lname);
        }
        return it->second.addr;
    };

    for(auto &ins:instrs){
        if(ins.sec==SEC_DATA){
            if(ins.toks.size()>0 && ins.toks[0]==".word"){
                if(ins.toks.size()<2) throw runtime_error("bad .word");
                long long v = parse_imm(ins.toks[1]);
                uint32_t w = (uint32_t)v;
                if(ins.offset + 4 > dataout.size()) throw runtime_error("data overflow");
                memcpy(&dataout[ins.offset], &w, 4);
                continue;
            } else if(ins.toks.size()==0) continue;
            else { /* ignore other data directives for now */ continue; }
        }
        if(ins.sec==SEC_TEXT){
            if(ins.toks.empty()) continue;

            string op=ins.toks[0];
            uint32_t encoded=0;

            try{
                // R-type: op rd rs1 rs2
            if(op=="add"||op=="sub"||op=="sll"||op=="slt"||op=="sltu"||op=="xor"||op=="srl"||op=="sra"||op=="or"||op=="and"){
                if(ins.toks.size()<4) throw runtime_error("operand count");
                int rd = reg_id(ins.toks[1]);
                int rs1 = reg_id(ins.toks[2]);
                int rs2 = reg_id(ins.toks[3]);
                encoded = encode_r_type(op, rd, rs1, rs2);
            }
            // I-type arithmetic: addi, etc. form: addi rd rs1 imm
            else if(op=="addi"||op=="slti"||op=="sltiu"||op=="xori"||op=="ori"||op=="andi"||op=="slli"||op=="srli"||op=="srai"){
                if(ins.toks.size()<4) throw runtime_error("operand count");
                int rd = reg_id(ins.toks[1]);
                int rs1 = reg_id(ins.toks[2]);
                long long imm = parse_imm(ins.toks[3]);
                encoded = encode_i_type(op, rd, rs1, imm);
            }
            // loads: ld rd, imm(rs1)
            else if(op=="ld"||op=="lw"||op=="lh"||op=="lb"){
                if(ins.toks.size()<4) throw runtime_error("bad load tokens");
                // format: op rd imm ( rs1 )
                int rd = reg_id(ins.toks[1]);
                auto mem = parse_mem_operand(ins.toks, 2);
                long long imm = mem.first; int rs1 = mem.second;
                encoded = encode_i_type(op, rd, rs1, imm);
            }
            // stores: sd rs2, imm(rs1)
            else if(op=="sd"||op=="sw"||op=="sh"||op=="sb"){
                if(ins.toks.size()<4) throw runtime_error("bad store tokens");
                int rs2 = reg_id(ins.toks[1]);
                auto mem = parse_mem_operand(ins.toks, 2);
                long long imm = mem.first; int rs1 = mem.second;
                encoded = encode_s_type(op, rs2, rs1, imm);
            }
            // jalr: jalr rd rs1 imm  (our pseudo expansion uses x0, ra, 0 for jr)
            else if(op=="jalr"){
                if(ins.toks.size()<4) throw runtime_error("bad jalr");
                int rd = reg_id(ins.toks[1]);
                int rs1 = reg_id(ins.toks[2]);
                long long imm = parse_imm(ins.toks[3]);
                encoded = encode_i_type("jalr", rd, rs1, imm);
            }
            // jal: jal rd label   -> compute rel = label_addr - ins.offset
            else if(op=="jal"){
                if(ins.toks.size()<3) throw runtime_error("bad jal");
                int rd = reg_id(ins.toks[1]);
                string targ = ins.toks[2];
                uint32_t tgt = get_label_addr(targ);
                long long rel = (long long)tgt - (long long)ins.offset;
                encoded = encode_j_type("jal", rd, rel);
            }
            // branches: beq rs1 rs2 label  -> encoded as branch with rel = label - ins.offset
            else if(op=="beq"||op=="bne"||op=="blt"||op=="bge"||op=="bltu"||op=="bgeu"){
                if(ins.toks.size()<4) throw runtime_error("bad branch");
                int rs1 = reg_id(ins.toks[1]);
                int rs2 = reg_id(ins.toks[2]);
                string targ = ins.toks[3];
                uint32_t tgt = get_label_addr(targ);
                long long rel = (long long)tgt - (long long)ins.offset;
                encoded = encode_b_type(op, rs1, rs2, rel);
            }
            // U-type: lui/auipc
            else if(op=="lui"||op=="auipc"){
                if(ins.toks.size()<3) throw runtime_error("bad u-type");
                int rd = reg_id(ins.toks[1]);
                long long imm = parse_imm(ins.toks[2]);
                encoded = encode_u_type(op, rd, imm);
            }
            // simple pseudo-ops were expanded in pass1, so no "li" or "mv" at this point
            else {
                throw runtime_error("Unknown opcode: "+op);
            }
            }
            catch(exception &e){
                cerr<<"Error at line "<<ins.lineno<<": "<<e.what()<<"\n";
                return 5;
            }

            if(ins.offset + 4 > textout.size()){ cerr<<"text buffer overflow\n"; return 6; }
            // write little-endian
            uint32_t w = encoded;
            textout[ins.offset+0] = w & 0xff;
            textout[ins.offset+1] = (w>>8) & 0xff;
            textout[ins.offset+2] = (w>>16) & 0xff;
            textout[ins.offset+3] = (w>>24) & 0xff;
        }
    }

cout<<"=== SECOND PASS DONE ===\n";
cout<<"Generated text.bin and data.bin\n";

write_elf64_object("output.o", textout, dataout, symtab);

cout<<"Wrote ELF64 relocatable object: output.o\n";
    return 0;

}