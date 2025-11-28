#include <bits/stdc++.h>

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

        if(c=='#'|| c==';' || (c=='/' && i+1<s.size() && s[i+1]=='/')) break;

        if(isspace((unsigned char)c)){
            push();
            i++;
            continue;
        }
        if(c==','){
            push();
            i++;
            continue;
        }
        if(c==':'){
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
        return stoi(r.substr(1));
    }

    throw runtime_error("Unknown register: " + r);
}

//string to longlong
long long parse_imm(const string &s){
    long long v=0;
    if(s.size()>2 && s[0]=='0' && s[1]=='x')
        v = stoll(s, nullptr, 16);
    else
        v = stoll(s);
    return v;
}



uint32_t encode_riscv_r(const string &op, int rd, int rs1, int rs2){
    // only a demo: implement ADD = funct7(0) rs2 rs1 funct3(0) rd opcode(0x33)
    if(op=="add"){
        uint32_t funct7=0, funct3=0, opcode=0x33;
        return (funct7<<25) | (rs2<<20) | (rs1<<15)
               | (funct3<<12) | (rd<<7) | opcode;
    }
    throw runtime_error("Unknown R-type: "+op);
}

uint32_t encode_riscv_i(const string &op, int rd, int rs1, long long imm){
    if(op=="addi"){
        uint32_t opcode=0x13, funct3=0;
        uint32_t imm12 = imm & 0xfff;
        return (imm12<<20) | (rs1<<15)
               | (funct3<<12) | (rd<<7) | opcode;
    }
    throw runtime_error("Unknown I-type: "+op);
}

uint32_t encode_riscv_u(const string &op, int rd, long long imm){
    if(op=="lui"){
        uint32_t opcode=0x37;
        return ((imm & 0xfffff000) | (rd<<7) | opcode);
    }
    throw runtime_error("Unknown U-type: "+op);
}

uint32_t encode_riscv_j(const string &op, int rd, long long rel){
    if(op=="jal"){
        uint32_t opcode=0x6f;
        // jal encoding is tricky; here we do simplified version:
        long long off = rel;
        uint32_t imm20 =
            ((off & 0x80000)<<12) |
            ((off & 0x7fe)<<20) |
            ((off & 0x800)<<9) |
            (off & 0xff000);
        return imm20 | (rd<<7) | opcode;
    }
    throw runtime_error("Unknown J-type: "+op);
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

    vector<uint8_t> textout(text_off);
    vector<uint8_t> dataout(data_off);

    auto get_label_addr=[&](const string &Lname)->uint32_t{
        auto it=symtab.find(Lname);
        if(it==symtab.end()){
            throw runtime_error("Undefined label: "+Lname);
        }
        return it->second.addr;
    };

    for(auto &ins:instrs){
        if(ins.sec==SEC_DATA){
            if(ins.toks[0]==".word"){
                long long val=parse_imm(ins.toks[1]);
                uint32_t w=(uint32_t)val;
                memcpy(&dataout[ins.offset],&w,4);
                continue;
            }
        }
        if(ins.sec==SEC_TEXT){
            if(ins.toks.empty()) continue;

            string op=ins.toks[0];
            uint32_t encoded=0;

            try{
                if(op=="add"){
                    int rd = reg_id(ins.toks[1]);
                    int rs1= reg_id(ins.toks[2]);
                    int rs2= reg_id(ins.toks[3]);
                    encoded = encode_riscv_r(op,rd,rs1,rs2);
                }
                else if(op=="addi"){
                    int rd = reg_id(ins.toks[1]);
                    int rs1= reg_id(ins.toks[2]);
                    long long imm = parse_imm(ins.toks[3]);
                    encoded = encode_riscv_i(op,rd,rs1,imm);
                }
                else if(op=="lui"){
                    int rd = reg_id(ins.toks[1]);
                    long long imm = parse_imm(ins.toks[2]);
                    encoded = encode_riscv_u(op,rd,imm);
                }
                else if(op=="jal"){
                    int rd = reg_id(ins.toks[1]);
                    string target = ins.toks[2];

                    uint32_t tgt = get_label_addr(target);
                    long long rel = (long long)tgt - (long long)ins.offset;
                    encoded = encode_riscv_j(op,rd,rel);
                }
                else{
                    throw runtime_error("Unknown opcode: "+op);
                }
            }
            catch(exception &e){
                cerr<<"Error at line "<<ins.lineno<<": "<<e.what()<<"\n";
                return 5;
            }

            memcpy(&textout[ins.offset],&encoded,4);
        }
    }

    {
        ofstream ofs("text.bin", ios::binary);
        ofs.write((char*)textout.data(), textout.size());
    }
    {
        ofstream ofs("data.bin", ios::binary);
        ofs.write((char*)dataout.data(), dataout.size());
    }

    cout<<"=== SECOND PASS DONE ===\n";
    cout<<"Generated text.bin and data.bin\n";

    return 0;

}