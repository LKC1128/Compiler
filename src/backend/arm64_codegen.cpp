// ARM64 port by OpenAI Codex. Reuses the project's LIR, layout and global
// initializer design; instruction selection is independently implemented.
// ABI reference: https://github.com/ARM-software/abi-aa/tree/main/aapcs64
#include "backend/arm64_codegen.hpp"
#include "backend/arm64_abi.hpp"
#include "ir/bit_utils.hpp"
#include "ir/symbol_names.hpp"
#include <stdexcept>
#include <cstdint>
namespace sysy {
namespace {
unsigned width(const std::string& r) {return r[0]=='w'||r[0]=='s'?32:64;}
unsigned bytes(const std::string& r) {return width(r)/8;}
}
std::string ARM64CodeGen::reg(Type* t,int n) const {
    return std::string(t->is_float()?(t->size_in_bytes()==8?"d":"s"):(t->is_pointer()?"x":"w"))+std::to_string(n);
}
std::string ARM64CodeGen::allocated(const std::string& name,Type* type) const {
    return reg(type,std::stoi(name.substr(1)));
}
std::string ARM64CodeGen::label(const BasicBlock* b) const {return ".L"+function_->name()+"_"+b->name();}
void ARM64CodeGen::immediate(const std::string& r,int64_t value) {
    uint64_t bits=uint64_t(value);if(width(r)==32)bits&=UINT32_MAX;
    out_<<"  movz "<<r<<", #"<<(bits&65535)<<"\n";
    for(unsigned shift=16;shift<width(r);shift+=16) if((bits>>shift)&65535)
        out_<<"  movk "<<r<<", #"<<((bits>>shift)&65535)<<", lsl #"<<shift<<"\n";
}
void ARM64CodeGen::address(const std::string& dst,const std::string& base,int64_t offset) {
    if(!offset) {if(dst!=base)out_<<"  mov "<<dst<<", "<<base<<"\n";return;}
    if(offset>0 && offset<=4095)out_<<"  add "<<dst<<", "<<base<<", #"<<offset<<"\n";
    else if(offset<0 && offset>=-4095)out_<<"  sub "<<dst<<", "<<base<<", #"<<-offset<<"\n";
    else {immediate("x17",offset);out_<<"  add "<<dst<<", "<<base<<", x17\n";}
}
void ARM64CodeGen::memory(const std::string& op,const std::string& r,const std::string& base,int64_t offset) {
    unsigned size=op=="ldrb"||op=="strb"?1:bytes(r);
    if(offset>=0 && offset%size==0 && uint64_t(offset)/size<=4095)
        out_<<"  "<<op<<" "<<r<<", ["<<base<<(offset?", #"+std::to_string(offset):"")<<"]\n";
    else {address("x16",base,offset);out_<<"  "<<op<<" "<<r<<", [x16]\n";}
}
void ARM64CodeGen::get(const Value* v,const std::string& r) {
    if(auto* i=dynamic_cast<const ConstantInt*>(v)) {immediate(r,i->value());return;}
    if(auto* f=dynamic_cast<const ConstantFloat*>(v)) {
        immediate("w17",bit_copy<uint32_t>(f->value()));out_<<"  fmov "<<r<<", w17\n";return;
    }
    if(dynamic_cast<const ConstantZero*>(v)||dynamic_cast<const UndefValue*>(v)||dynamic_cast<const PoisonValue*>(v)) {
        if(v->type()->is_float())out_<<"  fmov "<<r<<", "<<(width(r)==64?"xzr":"wzr")<<"\n";
        else immediate(r,0);return;
    }
    if(auto* g=dynamic_cast<const GlobalVariable*>(v)) {
        out_<<"  adrp "<<r<<", "<<global_symbol(*g)<<"\n  add "<<r<<", "<<r<<", :lo12:"<<global_symbol(*g)<<"\n";return;
    }
    const auto& locations=function_->machine_layout().values;
    auto found=locations.find(v);if(found==locations.end())throw std::runtime_error("ARM64 value has no allocated location");
    const auto& location=found->second;
    if(location.object) {address(r,"x29",int64_t(location.offset)+16);return;}
    if(!location.reg.empty()) {
        auto source=allocated(location.reg,v->type());if(source!=r)out_<<"  "<<(v->type()->is_float()?"fmov ":"mov ")<<r<<", "<<source<<"\n";
    } else memory("ldr",r,"x29",int64_t(location.offset)+16);
}
void ARM64CodeGen::put(const std::string& r,const Value* v) {
    const auto& location=function_->machine_layout().values.at(v);
    if(!location.reg.empty()) {
        auto destination=allocated(location.reg,v->type());if(destination!=r)out_<<"  "<<(v->type()->is_float()?"fmov ":"mov ")<<destination<<", "<<r<<"\n";
    } else memory("str",r,"x29",int64_t(location.offset)+16);
}
void ARM64CodeGen::initializer(const Constant* c,Type* type) {
    if(auto* i=dynamic_cast<const ConstantInt*>(c))out_<<(type->size_in_bytes()==1?"  .byte ":"  .word ")<<i->value()<<"\n";
    else if(auto* f=dynamic_cast<const ConstantFloat*>(c))out_<<"  .word "<<bit_copy<uint32_t>(f->value())<<"\n";
    else if(auto* a=dynamic_cast<const ConstantArray*>(c))for(auto* value:a->elements())initializer(value,value->type());
    else out_<<"  .zero "<<type->size_in_bytes()<<"\n";
}
void ARM64CodeGen::globals(const Module& m) {
    for(auto& g:m.globals()) {
        auto* type=static_cast<PointerType*>(g->type())->pointee();
        bool zero=!g->initializer()||dynamic_cast<ConstantZero*>(g->initializer());
        out_<<(g->is_constant()?"  .section .rodata\n":zero?"  .bss\n":"  .data\n");
        auto name=global_symbol(*g);out_<<"  .balign 16\n  .globl "<<name<<"\n  .type "<<name<<", %object\n  .size "<<name<<", "<<type->size_in_bytes()<<"\n"<<name<<":\n";
        initializer(g->initializer(),type);
    }
}
void ARM64CodeGen::function(const Function& fn) {
    function_=&fn;
    if(fn.level()!=IRLevel::LIR || !(fn.state_flags()&SF_REGALLOC))throw std::runtime_error("ARM64 requires allocated LIR");
    auto name=function_symbol(fn);out_<<"  .text\n  .balign 4\n  .globl "<<name<<"\n  .type "<<name<<", %function\n"<<name<<":\n";
    // Layout offsets are relative to incoming SP; x29 points to the standard
    // two-word frame record at incoming SP-16. All SP changes stay aligned.
    out_<<"  mov x9, sp\n  mov x10, x29\n";
    immediate("x16",fn.machine_layout().frame_size);out_<<"  sub sp, sp, x16\n  sub x9, x9, #16\n  str x10, [x9]\n  str x30, [x9, #8]\n  mov x29, x9\n";
    for(const auto& saved:fn.machine_layout().saved_registers)
        memory("str",saved.first[0]=='v'?"d"+saved.first.substr(1):saved.first,"x29",int64_t(saved.second)+16);
    ArgAllocator incoming;
    for(auto& owner:fn.args()) {
        auto* arg=owner.get();auto location=incoming.next(arg->type());
        if(location.kind==ArgLocation::Stack) {auto scratch=reg(arg->type(),arg->type()->is_float()?16:9);memory("ldr",scratch,"x29",int64_t(location.index)+16);put(scratch,arg);}
        else put(reg(arg->type(),location.index),arg);
    }
    for(auto& block:fn.blocks()) {out_<<label(block.get())<<":\n";for(auto& i:block->instructions())instruction(*i);}
    out_<<".L"<<fn.name()<<"_epilogue:\n";
    for(const auto& saved:fn.machine_layout().saved_registers)
        memory("ldr",saved.first[0]=='v'?"d"+saved.first.substr(1):saved.first,"x29",int64_t(saved.second)+16);
    out_<<"  mov sp, x29\n  ldp x29, x30, [sp], #16\n  ret\n  .size "<<name<<", .-"<<name<<"\n";
}
void ARM64CodeGen::generate(const Module& m) {
    out_<<"  .arch armv8-a\n";globals(m);for(auto& fn:m.functions())if(!fn->is_declaration())function(*fn);
    out_<<"  .section .note.GNU-stack,\"\",%progbits\n";
}
void ARM64CodeGen::instruction(const Instruction& i) {
    switch(i.opcode()) {
    case Opcode::Alloca:return;
    case Opcode::Load: {
        auto& load=static_cast<const LoadInst&>(i);get(load.pointer(),"x9");auto result=reg(i.type(),i.type()->is_float()?16:10);
        memory(i.type()->is_integer()&&i.type()->size_in_bytes()==1?"ldrb":"ldr",result,"x9",0);put(result,&i);return;
    }
    case Opcode::Store: {
        auto& store=static_cast<const StoreInst&>(i);get(store.pointer(),"x9");auto value=reg(store.value()->type(),store.value()->type()->is_float()?16:10);get(store.value(),value);
        memory(store.value()->type()->is_integer()&&store.value()->type()->size_in_bytes()==1?"strb":"str",value,"x9",0);return;
    }
    case Opcode::Add:case Opcode::Sub:case Opcode::Mul:case Opcode::SDiv:case Opcode::SRem:
    case Opcode::And:case Opcode::Or:case Opcode::Xor:case Opcode::Shl:case Opcode::LShr:case Opcode::AShr: {
        get(i.get_operand(0),"w9");get(i.get_operand(1),"w10");std::string op;
        switch(i.opcode()) {
        case Opcode::Add:op="add";break;case Opcode::Sub:op="sub";break;case Opcode::Mul:op="mul";break;
        case Opcode::SDiv:op="sdiv";break;case Opcode::And:op="and";break;case Opcode::Or:op="orr";break;
        case Opcode::Xor:op="eor";break;case Opcode::Shl:op="lslv";break;case Opcode::LShr:op="lsrv";break;case Opcode::AShr:op="asrv";break;
        default:break;
        }
        if(i.opcode()==Opcode::SRem)out_<<"  sdiv w11, w9, w10\n  msub w9, w11, w10, w9\n";
        else out_<<"  "<<op<<" w9, w9, w10\n";
        put("w9",&i);return;
    }
    case Opcode::FAdd:case Opcode::FSub:case Opcode::FMul:case Opcode::FDiv:case Opcode::FNeg: {
        auto a=reg(i.type(),16),b=reg(i.type(),17);get(i.get_operand(0),a);
        if(i.opcode()==Opcode::FNeg)out_<<"  fneg "<<a<<", "<<a<<"\n";
        else {get(i.get_operand(1),b);const char* op=i.opcode()==Opcode::FAdd?"fadd":i.opcode()==Opcode::FSub?"fsub":i.opcode()==Opcode::FMul?"fmul":"fdiv";out_<<"  "<<op<<" "<<a<<", "<<a<<", "<<b<<"\n";}
        put(a,&i);return;
    }
    case Opcode::ICmp: {
        auto& cmp=static_cast<const ICmpInst&>(i);auto a=reg(cmp.lhs()->type(),9),b=reg(cmp.rhs()->type(),10);get(cmp.lhs(),a);get(cmp.rhs(),b);
        if(cmp.lhs()->type()->is_integer()) {
            auto bits=static_cast<IntegerType*>(cmp.lhs()->type())->bits();
            if(bits<32) for(const auto& r:{a,b}) {
                bool sign=cmp.predicate()==ICmpPred::SLT||cmp.predicate()==ICmpPred::SLE||cmp.predicate()==ICmpPred::SGT||cmp.predicate()==ICmpPred::SGE;
                if(sign)out_<<"  sbfx "<<r<<", "<<r<<", #0, #"<<bits<<"\n";
                else out_<<"  and "<<r<<", "<<r<<", #"<<((1u<<bits)-1)<<"\n";
            }
        }
        const char* cond="eq";
        switch(cmp.predicate()) {
        case ICmpPred::EQ:cond="eq";break;case ICmpPred::NE:cond="ne";break;
        case ICmpPred::SLT:cond="lt";break;case ICmpPred::SLE:cond="le";break;case ICmpPred::SGT:cond="gt";break;case ICmpPred::SGE:cond="ge";break;
        case ICmpPred::ULT:cond="lo";break;case ICmpPred::ULE:cond="ls";break;case ICmpPred::UGT:cond="hi";break;case ICmpPred::UGE:cond="hs";break;
        }
        out_<<"  cmp "<<a<<", "<<b<<"\n  cset w9, "<<cond<<"\n";put("w9",&i);return;
    }
    case Opcode::FCmp: {
        auto& cmp=static_cast<const FCmpInst&>(i);auto a=reg(cmp.lhs()->type(),16),b=reg(cmp.rhs()->type(),17);get(cmp.lhs(),a);get(cmp.rhs(),b);
        const char* cond="eq";switch(cmp.predicate()) {
        case FCmpPred::OEQ:cond="eq";break;case FCmpPred::ONE:case FCmpPred::UNE:cond="ne";break;
        case FCmpPred::OLT:cond="mi";break;case FCmpPred::OLE:cond="ls";break;case FCmpPred::OGT:cond="gt";break;case FCmpPred::OGE:cond="ge";break;
        }
        out_<<"  fcmp "<<a<<", "<<b<<"\n  cset w9, "<<cond<<"\n";
        if(cmp.predicate()==FCmpPred::ONE)out_<<"  cset w10, vc\n  and w9, w9, w10\n";
        put("w9",&i);return;
    }
    case Opcode::SIToFP: {
        get(i.get_operand(0),"w9");auto result=reg(i.type(),16);out_<<"  scvtf "<<result<<", w9\n";put(result,&i);return;
    }
    case Opcode::FPToSI: {
        auto input=reg(i.get_operand(0)->type(),16);get(i.get_operand(0),input);out_<<"  fcvtzs w9, "<<input<<"\n";put("w9",&i);return;
    }
    case Opcode::FPExt: {
        get(i.get_operand(0),"s16");out_<<"  fcvt d16, s16\n";put("d16",&i);return;
    }
    case Opcode::BitCast: {
        auto* operand=i.get_operand(0);auto input=reg(operand->type(),operand->type()->is_float()?16:9);get(operand,input);
        auto output=reg(i.type(),i.type()->is_float()?16:9);
        if(operand->type()->is_float()!=i.type()->is_float())out_<<"  fmov "<<output<<", "<<input<<"\n";
        else if(input!=output)out_<<"  mov "<<output<<", "<<input<<"\n";
        put(output,&i);return;
    }
    case Opcode::ZExt:case Opcode::SExt:case Opcode::Trunc: {
        get(i.get_operand(0),"w9");unsigned bits=static_cast<IntegerType*>((i.opcode()==Opcode::Trunc?i.type():i.get_operand(0)->type()))->bits();
        if(bits<32) {
            if(i.opcode()==Opcode::SExt)out_<<"  sbfx w9, w9, #0, #"<<bits<<"\n";
            else out_<<"  and w9, w9, #"<<((1u<<bits)-1)<<"\n";
        }
        put("w9",&i);return;
    }
    case Opcode::GetElementPtr: {
        auto& gep=static_cast<const GetElementPtrInst&>(i);get(gep.base_pointer(),"x9");Type* type=gep.source_element_type();
        for(size_t k=0;k<gep.num_indices();++k) {
            if(k && type->is_array())type=static_cast<ArrayType*>(type)->element_type();
            get(gep.index(k),"w10");out_<<"  sxtw x10, w10\n";immediate("x11",int64_t(type->size_in_bytes()));out_<<"  madd x9, x10, x11, x9\n";
        }
        put("x9",&i);return;
    }
    case Opcode::Branch:case Opcode::CondBranch: {
        auto& branch=static_cast<const BranchInst&>(i);
        if(branch.is_conditional()) {
            get(branch.condition(),"w9");auto near=".Larm_branch_"+std::to_string(branch_id_++);
            out_<<"  cbnz w9, "<<near<<"\n  b "<<label(branch.false_target())<<"\n"<<near<<":\n  b "<<label(branch.true_target())<<"\n";
        } else out_<<"  b "<<label(branch.target())<<"\n";return;
    }
    case Opcode::Return: {
        auto& ret=static_cast<const ReturnInst&>(i);if(ret.has_return_value())get(ret.return_value(),reg(ret.return_value()->type(),0));
        out_<<"  b .L"<<function_->name()<<"_epilogue\n";return;
    }
    case Opcode::Call: {
        auto& call=static_cast<const CallInst&>(i);ArgAllocator arguments;
        for(size_t k=0;k<call.num_args();++k) {
            auto* value=call.arg(k);auto location=arguments.next(value->type());
            if(location.kind==ArgLocation::Stack) {
                auto scratch=reg(value->type(),value->type()->is_float()?16:9);get(value,scratch);memory("str",scratch,"sp",location.index);
            } else get(value,reg(value->type(),location.index));
        }
        out_<<"  bl "<<function_symbol(*call.callee())<<"\n";
        if(!call.type()->is_void())put(reg(call.type(),0),&i);return;
    }
    case Opcode::Phi:throw std::runtime_error("ARM64 codegen received a Phi after LIR lowering");
    }
    throw std::runtime_error("Unsupported ARM64 instruction");
}
}
