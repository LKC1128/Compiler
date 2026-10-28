#include "ir/ir_printer.hpp"
#include <iomanip>
#include <unordered_set>
#include "ir/bit_utils.hpp"
#include "ir/symbol_names.hpp"

namespace sysy {

void IRPrinter::build_value_names(const Function& function) {
    value_names_.clear();
    unnamed_counter_ = 0;
    std::unordered_set<std::string> used;
    auto assign = [&](const Value* value) {
        std::string base = value->has_name() ? value->name() : "v";
        std::string name = base;
        while (!used.insert(name).second)
            name = base + "." + std::to_string(unnamed_counter_++);
        value_names_[value] = "%" + name;
    };
    for (const auto& argument : function.args()) assign(argument.get());
    for (const auto& block : function.blocks()) {
        assign(block.get());
        for (const auto& inst : block->instructions())
            if (!inst->type()->is_void()) assign(inst.get());
    }
}

std::string IRPrinter::format_constant(const Constant* c) {
    if (auto* ci = dynamic_cast<const ConstantInt*>(c)) {
        if (ci->type()->is_integer() && static_cast<IntegerType*>(ci->type())->bits() == 1) {
            return ci->value() ? "true" : "false";
        }
        return std::to_string(ci->value());
    }
    if (auto* cf = dynamic_cast<const ConstantFloat*>(c)) {
        std::ostringstream ss;
        // LLVM's float hex spelling uses the exact value extended to double.
        ss << "0x" << std::hex << std::uppercase << std::setw(16)
           << std::setfill('0') << bit_copy<uint64_t>(static_cast<double>(cf->value()));
        return ss.str();
    }
    if (dynamic_cast<const ConstantZero*>(c)) {
        return "zeroinitializer";
    }
    if (dynamic_cast<const PoisonValue*>(c)) return "poison";
    if (dynamic_cast<const UndefValue*>(c)) {
        return "undef";
    }
    if (auto* ca = dynamic_cast<const ConstantArray*>(c)) {
        std::string res = "[";
        for (size_t i = 0; i < ca->elements().size(); ++i) {
            if (i > 0) res += ", ";
            res += ca->elements()[i]->type()->to_string() + " " + format_constant(ca->elements()[i]);
        }
        res += "]";
        return res;
    }
    return "0";
}

std::string IRPrinter::get_value_name(const Value* val) {
    if (!val) return "null";
    if (val->kind() == Value::ValueKind::GlobalVariable) {
        return "@" + global_symbol(*static_cast<const GlobalVariable*>(val));
    }
    if (val->kind() == Value::ValueKind::Function) {
        return "@" + function_symbol(*static_cast<const Function*>(val));
    }
    if (auto* c = dynamic_cast<const Constant*>(val)) {
        return format_constant(c);
    }
    auto it = value_names_.find(val);
    if (it != value_names_.end()) {
        return it->second;
    }
    if (val->has_name()) {
        return "%" + val->name();
    }
    return "%?";
}

void IRPrinter::print(const Module& module) {
    // Print globals
    for (const auto& gv : module.globals()) {
        os_ << "@" << global_symbol(*gv) << " = ";
        if (gv->is_constant()) {
            os_ << "constant ";
        } else {
            os_ << "global ";
        }
        Type* pointee = static_cast<PointerType*>(gv->type())->pointee();
        os_ << pointee->to_string() << " ";
        if (gv->initializer()) {
            os_ << format_constant(gv->initializer());
        } else {
            os_ << "zeroinitializer";
        }
        os_ << "\n";
    }

    if (!module.globals().empty()) {
        os_ << "\n";
    }

    // Print function declarations and definitions
    for (const auto& fn : module.functions()) {
        if (fn->is_declaration()) {
            os_ << "declare " << fn->return_type()->to_string() << " @" << function_symbol(*fn) << "(";
            const auto& params = fn->function_type()->param_types();
            for (size_t i = 0; i < params.size(); ++i) {
                if (i > 0) os_ << ", ";
                os_ << params[i]->to_string();
            }
            if (fn->function_type()->is_variadic()) os_ << (params.empty() ? "..." : ", ...");
            os_ << ")\n";
        }
    }

    for (const auto& fn : module.functions()) {
        if (!fn->is_declaration()) {
            os_ << "\n";
            print(*fn);
        }
    }
}

void IRPrinter::print(const Function& function) {
    build_value_names(function);
    os_ << "; level=" << level_name(function.level()) << " state=" << function.state_flags() << "\n";
    for(size_t r=0;r<function.regions().size();++r) {
        const auto& region=function.regions()[r];
        os_ << "; region " << r << " " << region.kind << " parent=" << region.parent
            << " entry=" << get_value_name(region.entry) << " exit=" << get_value_name(region.exit) << "\n";
    }
    if(function.level()==IRLevel::LIR) {
        os_ << "; frame=" << function.machine_layout().frame_size << " outgoing=" << function.machine_layout().outgoing_bytes << "\n";
        auto location=[&](const Value* v) {
            auto it=function.machine_layout().values.find(v);
            if(it==function.machine_layout().values.end()) return;
            const auto& l=it->second;
            os_ << "; " << get_value_name(v) << " -> ";
            if(!l.reg.empty()) os_ << l.reg;
            else os_ << (l.object ? "object " : "slot ") << l.offset << "(s0) bytes=" << l.bytes;
            os_ << "\n";
        };
        for(const auto& a:function.args()) location(a.get());
        for(const auto& b:function.blocks()) for(const auto& i:b->instructions()) location(i.get());
    }

    os_ << "define " << function.return_type()->to_string() << " @" << function_symbol(function) << "(";
    for (size_t i = 0; i < function.args().size(); ++i) {
        if (i > 0) os_ << ", ";
        auto* arg = function.get_arg(i);
        os_ << arg->type()->to_string() << " " << get_value_name(arg);
    }
    os_ << ") {\n";

    for (const auto& bb : function.blocks()) {
        print(*bb);
    }

    os_ << "}\n";
}

void IRPrinter::print(const BasicBlock& block) {
    std::string bb_name = get_value_name(&block);
    if (!bb_name.empty() && bb_name[0] == '%') {
        bb_name = bb_name.substr(1);
    }
    os_ << bb_name << ":\n";

    for (const auto& inst : block.instructions()) {
        os_ << "  ";
        print(*inst);
        os_ << "\n";
    }
}

void IRPrinter::print(const Instruction& inst) {
    if (!inst.type()->is_void()) {
        os_ << get_value_name(&inst) << " = ";
    }

    switch (inst.opcode()) {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::SRem:
        case Opcode::And: case Opcode::Or: case Opcode::Xor:
        case Opcode::Shl: case Opcode::LShr: case Opcode::AShr:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv: {
            os_ << inst.get_opcode_name();
            if(inst.has_ub_flag(UB_NUW)) os_ << " nuw";
            if(inst.has_ub_flag(UB_NSW)) os_ << " nsw";
            if(inst.has_ub_flag(UB_EXACT)) os_ << " exact";
            os_ << " " << inst.type()->to_string() << " "
                << get_value_name(inst.get_operand(0)) << ", "
                << get_value_name(inst.get_operand(1));
            break;
        }
        case Opcode::FNeg:
            os_ << "fneg " << inst.type()->to_string() << " " << get_value_name(inst.get_operand(0));
            break;
        case Opcode::ICmp: {
            auto& icmp = static_cast<const ICmpInst&>(inst);
            os_ << "icmp " << icmp.get_pred_name() << " "
                << icmp.lhs()->type()->to_string() << " "
                << get_value_name(icmp.lhs()) << ", "
                << get_value_name(icmp.rhs());
            break;
        }
        case Opcode::FCmp: {
            auto& fcmp = static_cast<const FCmpInst&>(inst);
            os_ << "fcmp " << fcmp.get_pred_name() << " "
                << fcmp.lhs()->type()->to_string() << " "
                << get_value_name(fcmp.lhs()) << ", "
                << get_value_name(fcmp.rhs());
            break;
        }
        case Opcode::SIToFP:
        case Opcode::FPToSI:
        case Opcode::ZExt:
        case Opcode::SExt: case Opcode::Trunc:
        case Opcode::BitCast:
        case Opcode::FPExt: {
            os_ << inst.get_opcode_name() << " "
                << inst.get_operand(0)->type()->to_string() << " "
                << get_value_name(inst.get_operand(0)) << " to "
                << inst.type()->to_string();
            break;
        }
        case Opcode::Alloca: {
            auto& alloca_inst = static_cast<const AllocaInst&>(inst);
            os_ << "alloca " << alloca_inst.allocated_type()->to_string();
            break;
        }
        case Opcode::Load: {
            auto& load_inst = static_cast<const LoadInst&>(inst);
            os_ << "load " << load_inst.type()->to_string() << ", "
                << load_inst.pointer()->type()->to_string() << " "
                << get_value_name(load_inst.pointer());
            break;
        }
        case Opcode::Store: {
            auto& store_inst = static_cast<const StoreInst&>(inst);
            os_ << "store " << store_inst.value()->type()->to_string() << " "
                << get_value_name(store_inst.value()) << ", "
                << store_inst.pointer()->type()->to_string() << " "
                << get_value_name(store_inst.pointer());
            break;
        }
        case Opcode::GetElementPtr: {
            auto& gep = static_cast<const GetElementPtrInst&>(inst);
            os_ << "getelementptr " << (inst.has_ub_flag(UB_INBOUNDS) ? "inbounds " : "") << gep.source_element_type()->to_string() << ", "
                << gep.base_pointer()->type()->to_string() << " "
                << get_value_name(gep.base_pointer());
            for (size_t i = 0; i < gep.num_indices(); ++i) {
                os_ << ", " << gep.index(i)->type()->to_string() << " "
                    << get_value_name(gep.index(i));
            }
            break;
        }
        case Opcode::Branch:
        case Opcode::CondBranch: {
            auto& br = static_cast<const BranchInst&>(inst);
            if (br.is_conditional()) {
                os_ << "br i1 " << get_value_name(br.condition()) << ", label "
                    << get_value_name(br.true_target()) << ", label "
                    << get_value_name(br.false_target());
            } else {
                os_ << "br label " << get_value_name(br.target());
            }
            break;
        }
        case Opcode::Return: {
            auto& ret = static_cast<const ReturnInst&>(inst);
            if (ret.has_return_value()) {
                os_ << "ret " << ret.return_value()->type()->to_string() << " "
                    << get_value_name(ret.return_value());
            } else {
                os_ << "ret void";
            }
            break;
        }
        case Opcode::Call: {
            auto& call = static_cast<const CallInst&>(inst);
            os_ << "call " << call.type()->to_string() << " @" << function_symbol(*call.callee()) << "(";
            for (size_t i = 0; i < call.num_args(); ++i) {
                if (i > 0) os_ << ", ";
                os_ << call.arg(i)->type()->to_string() << " " << get_value_name(call.arg(i));
            }
            os_ << ")";
            break;
        }
        case Opcode::Phi: {
            auto& phi = static_cast<const PhiInst&>(inst);
            os_ << "phi " << phi.type()->to_string() << " ";
            bool first=true;
            for (size_t i = 0; i < phi.num_incoming(); ++i) {
                // Internal phis use unique predecessor blocks; LLVM text
                // requires one incoming per edge, including identical edges.
                auto* pred=phi.incoming_block(i);
                auto* branch=dynamic_cast<const BranchInst*>(pred->get_terminator());
                unsigned copies=branch && branch->is_conditional() &&
                    branch->true_target()==phi.parent() && branch->false_target()==phi.parent() ? 2 : 1;
                for(unsigned edge=0;edge<copies;++edge) {
                    if(!first) os_ << ", ";first=false;
                    os_ << "[ " << get_value_name(phi.incoming_value(i)) << ", "
                        << get_value_name(pred) << " ]";
                }
            }
            break;
        }
    }
}

void IRPrinter::print(const Value* value) {
    os_ << get_value_name(value);
}

} // namespace sysy
