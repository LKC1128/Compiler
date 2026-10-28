#include "frontend/ir_builder.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include "analysis/cfg.hpp"
#include "ir/int_math.hpp"

namespace sysy {
namespace { class SemanticFailure : public std::runtime_error { public: using std::runtime_error::runtime_error; }; }

IRBuilder::IRBuilder(Module& module)
    : module_(module), ctx_(module.type_context()) {
    module_.register_runtime_functions();
}

void IRBuilder::report_error(const std::string& msg, int line, int col) {
    if(line <= 0 && diagnostic_node_) {line=diagnostic_node_->line;col=diagnostic_node_->col;}
    if (!error_msg_.empty()) error_msg_ += "\n";
    error_msg_ += "Semantic error at line " + std::to_string(line) + ", col " + std::to_string(col) + ": " + msg;
    throw SemanticFailure(error_msg_);
}

Instruction* IRBuilder::emit(std::unique_ptr<Instruction> inst) {
    if (!cur_bb_) return nullptr;
    if (cur_bb_->has_terminator()) {
        // Drop dead instructions after terminator
        return nullptr;
    }
    return cur_bb_->append_instruction(std::move(inst));
}

Value* IRBuilder::emit_source_integer(Opcode op,Value* lhs,Value* rhs) {
    auto inst=std::make_unique<BinaryInst>(op,lhs,rhs);
    if(assume_signed_no_overflow_) inst->set_ub_flags(UB_NSW);
    return emit(std::move(inst));
}

Type* IRBuilder::get_ir_type(DataType dt) {
    switch (dt) {
        case DataType::Void: return ctx_.get_void_type();
        case DataType::Int: return ctx_.get_i32_type();
        case DataType::Float: return ctx_.get_float_type();
    }
    return ctx_.get_i32_type();
}

Value* IRBuilder::coerce(Value* val, Type* target_type) {
    if (!val || val->type() == target_type) return val;

    if (val->type()->is_pointer() && target_type->is_pointer())
        return emit(std::make_unique<CastInst>(Opcode::BitCast, val, target_type));

    if (val->type() == ctx_.get_float_type() && target_type == ctx_.get_double_type())
        return emit(std::make_unique<CastInst>(Opcode::FPExt, val, target_type));

    // i1 to i32
    if (val->type() == ctx_.get_i1_type() && target_type == ctx_.get_i32_type()) {
        return emit(std::make_unique<CastInst>(Opcode::ZExt, val, ctx_.get_i32_type()));
    }
    // i32 to float
    if (val->type() == ctx_.get_i32_type() && target_type == ctx_.get_float_type()) {
        return emit(std::make_unique<CastInst>(Opcode::SIToFP, val, ctx_.get_float_type()));
    }
    // float to i32
    if (val->type() == ctx_.get_float_type() && target_type == ctx_.get_i32_type()) {
        return emit(std::make_unique<CastInst>(Opcode::FPToSI, val, ctx_.get_i32_type()));
    }
    // i32 to i1
    if (val->type() == ctx_.get_i32_type() && target_type == ctx_.get_i1_type()) {
        return emit(std::make_unique<ICmpInst>(ICmpPred::NE, val, ConstantInt::get(ctx_, 0), ctx_));
    }
    // float to i1
    if (val->type() == ctx_.get_float_type() && target_type == ctx_.get_i1_type()) {
        return emit(std::make_unique<FCmpInst>(FCmpPred::UNE, val, ConstantFloat::get(ctx_, 0.0f), ctx_));
    }

    return val;
}

std::optional<IRBuilder::ConstValue> IRBuilder::try_eval_const(const ExprAST* expr) {
    auto iv = [](int32_t x) { return ConstValue{false, x, 0.0f}; };
    auto fv = [](float x) { return ConstValue{true, 0, x}; };
    if (!expr) return std::nullopt;
    if (auto* x = dynamic_cast<const IntLiteralAST*>(expr)) return iv(x->value);
    if (auto* x = dynamic_cast<const FloatLiteralAST*>(expr)) return fv(x->value);
    if (auto* x = dynamic_cast<const LValAST*>(expr)) {
        auto* sym = sym_table_.lookup(x->name);
        if (!sym || !sym->is_const) return std::nullopt;
        if (!sym->is_array && x->indices.empty())
            return sym->type == DataType::Float ? fv(sym->const_float) : iv(sym->const_int);
        if (x->indices.size() != sym->dims.size()) return std::nullopt;
        size_t idx = 0;
        for (size_t k = 0; k < x->indices.size(); ++k) {
            auto n = try_eval_const(x->indices[k].get());
            if (!n || n->floating || n->integer < 0 || n->integer >= sym->dims[k]) return std::nullopt;
            idx = idx * static_cast<size_t>(sym->dims[k]) + n->integer;
        }
        if (sym->type == DataType::Float) return fv(sym->const_float_arr.empty() ? 0.0f : sym->const_float_arr.at(idx));
        return iv(sym->const_int_arr.empty() ? 0 : sym->const_int_arr.at(idx));
    }
    if (auto* x = dynamic_cast<const UnaryExprAST*>(expr)) {
        auto v = try_eval_const(x->expr.get()); if (!v) return std::nullopt;
        switch (x->op) {
            case UnaryOp::Plus: return v;
            case UnaryOp::Minus: return v->floating ? fv(-v->real) : iv(int_sub(0, v->integer));
            case UnaryOp::Not: return iv(!v->truth());
        }
    }
    if (auto* x = dynamic_cast<const BinaryExprAST*>(expr)) {
        auto l = try_eval_const(x->lhs.get()); if (!l) return std::nullopt;
        if (x->op == BinaryOp::And && !l->truth()) return iv(0);
        if (x->op == BinaryOp::Or && l->truth()) return iv(1);
        auto r = try_eval_const(x->rhs.get()); if (!r) return std::nullopt;
        if (x->op == BinaryOp::And) return iv(l->truth() && r->truth());
        if (x->op == BinaryOp::Or) return iv(l->truth() || r->truth());
        if (l->floating || r->floating) {
            float a=l->as_float(), b=r->as_float();
            switch (x->op) {
                case BinaryOp::Add: return fv(a+b);
                case BinaryOp::Sub: return fv(a-b);
                case BinaryOp::Mul: return fv(a*b);
                case BinaryOp::Div: return fv(a/b);
                case BinaryOp::Lt: return iv(a<b); case BinaryOp::Le: return iv(a<=b);
                case BinaryOp::Gt: return iv(a>b); case BinaryOp::Ge: return iv(a>=b);
                case BinaryOp::Eq: return iv(a==b); case BinaryOp::Ne: return iv(a!=b);
                default: return std::nullopt;
            }
        }
        int32_t a=l->integer, b=r->integer;
        switch (x->op) {
            case BinaryOp::Add: return iv(int_add(a,b));
            case BinaryOp::Sub: return iv(int_sub(a,b));
            case BinaryOp::Mul: return iv(int_mul(a,b));
            case BinaryOp::Div: if (int_div_defined(a,b)) return iv(a/b); return std::nullopt;
            case BinaryOp::Mod: if (int_div_defined(a,b)) return iv(a%b); return std::nullopt;
            case BinaryOp::Lt: return iv(a<b); case BinaryOp::Le: return iv(a<=b);
            case BinaryOp::Gt: return iv(a>b); case BinaryOp::Ge: return iv(a>=b);
            case BinaryOp::Eq: return iv(a==b); case BinaryOp::Ne: return iv(a!=b);
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

int32_t IRBuilder::eval_const_int(const ExprAST* expr) {
    auto v=try_eval_const(expr);
    if (!v) report_error("Expression is not a valid constant expression", expr ? expr->line : 0, expr ? expr->col : 0);
    if (!v->floating) return v->integer;
    if (!std::isfinite(v->real) || v->real < -2147483648.0 || v->real >= 2147483648.0)
        report_error("Constant conversion to int is out of range", expr->line, expr->col);
    return static_cast<int32_t>(v->real);
}
float IRBuilder::eval_const_float(const ExprAST* expr) {
    auto v=try_eval_const(expr);
    if (!v) report_error("Expression is not a valid constant expression", expr ? expr->line : 0, expr ? expr->col : 0);
    return v->as_float();
}

bool IRBuilder::build(const CompUnitAST& comp_unit) {
    DiagnosticScope diagnostic(*this,&comp_unit);
    try {
    for (const auto& item : comp_unit.items)
        if (auto* fn = dynamic_cast<const FuncDefAST*>(item.get())) function_definitions_[fn->name] = fn;
    for (const auto& item : comp_unit.items) {
        if (auto* decl = dynamic_cast<const DeclAST*>(item.get())) {
            build_decl(*decl);
        } else if (auto* func = dynamic_cast<const FuncDefAST*>(item.get())) {
            build_func_def(*func);
        }
        if (!error_msg_.empty()) return false;
    }
    auto* main_fn = module_.get_function("main");
    if (!main_fn || main_fn->is_declaration() || main_fn->return_type() != ctx_.get_i32_type() || !main_fn->args().empty())
        report_error("Program must define exactly one int main() with no parameters");
    return error_msg_.empty();
    } catch (const SemanticFailure&) { return false; }
}

void IRBuilder::build_decl(const DeclAST& decl) {
    for (const auto& def : decl.defs) {
        build_var_def(*def, decl.type, decl.is_const, decl.is_tensor);
        release_tensor_allocations();
    }
}

void IRBuilder::flatten_const_init_val(const InitValAST* initializer, const std::vector<int>& dims,
                                       size_t dimension, std::vector<Constant*>& values, DataType type) {
    size_t extent=1;
    for(size_t i=dimension;i<dims.size();++i) extent*=static_cast<size_t>(dims[i]);
    Constant* zero=type==DataType::Float?static_cast<Constant*>(ConstantFloat::get(ctx_,0)):
                                          static_cast<Constant*>(ConstantInt::get(ctx_,0));
    size_t base=values.size();values.resize(base+extent,zero);
    std::vector<InitEntry> entries;
    plan_initializer(initializer,dims,dimension,0,entries);
    for(const auto& entry:entries) if(entry.expression) {
        values[base+entry.index]=type==DataType::Float?static_cast<Constant*>(ConstantFloat::get(ctx_,eval_const_float(entry.expression))):
                                                      static_cast<Constant*>(ConstantInt::get(ctx_,eval_const_int(entry.expression)));
    }
}

// Helper to construct multidimensional ConstantArray
static Constant* make_nested_constant_array(TypeContext& ctx, Type* elem_ty, const std::vector<int>& dims,
                                            size_t dim_idx, const std::vector<Constant*>& flat_consts, size_t& offset) {
    if (std::find(dims.begin()+dim_idx,dims.end(),0)!=dims.end()) {
        Type* type=elem_ty;
        for(size_t i=dims.size();i>dim_idx;--i) type=ctx.get_array_type(type,dims[i-1]);
        return ConstantZero::get(type);
    }
    if (dim_idx == dims.size() - 1) {
        std::vector<Constant*> leaf_elements;
        for (int i = 0; i < dims[dim_idx]; ++i) {
            leaf_elements.push_back(flat_consts[offset++]);
        }
        auto* arr_ty = ctx.get_array_type(elem_ty, dims[dim_idx]);
        return ctx.own_constant(std::make_unique<ConstantArray>(arr_ty, std::move(leaf_elements)));
    }

    std::vector<Constant*> sub_elements;
    for (int i = 0; i < dims[dim_idx]; ++i) {
        sub_elements.push_back(make_nested_constant_array(ctx, elem_ty, dims, dim_idx + 1, flat_consts, offset));
    }
    auto* sub_ty = sub_elements[0]->type();
    auto* arr_ty = ctx.get_array_type(sub_ty, dims[dim_idx]);
    return ctx.own_constant(std::make_unique<ConstantArray>(arr_ty, std::move(sub_elements)));
}

const ExprAST* IRBuilder::scalar_initializer(const InitValAST* initializer) {
    if (!initializer) return nullptr;
    if (!initializer->is_list) return initializer->expr.get();
    if (initializer->list.size()>1) report_error("Too many scalar initializers",initializer->line,initializer->col);
    return initializer->list.empty()?nullptr:scalar_initializer(initializer->list.front().get());
}

void IRBuilder::build_var_def(const VarDefAST& def, DataType type, bool is_const, bool is_tensor) {
    DiagnosticScope diagnostic(*this,&def);
    Symbol sym;
    sym.name = def.name;
    sym.type = type;
    sym.is_const = is_const;
    sym.is_tensor = is_tensor;

    for (const auto& d : def.dims) {
        auto dimension = try_eval_const(d.get());
        if (!dimension || dimension->floating || dimension->integer < 0)
            report_error("Array dimension must be a nonnegative integer constant", d->line, d->col);
        sym.dims.push_back(dimension->integer);
    }
    if (is_tensor && sym.dims.empty()) report_error("Tensor variables require dimensions", def.line, def.col);
    int64_t elements = std::find(sym.dims.begin(),sym.dims.end(),0)!=sym.dims.end()?0:1;
    for (int dimension : sym.dims) {
        if (dimension && elements > (INT32_MAX / 4) / dimension) report_error("Array object is too large", def.line, def.col);
        elements *= dimension;
    }
    sym.is_array = !sym.dims.empty();

    const ExprAST* scalar_init = !sym.is_array && def.init ? scalar_initializer(def.init.get()) : nullptr;
    if (sym.is_array && !is_tensor && def.init && !def.init->is_list)
        report_error("Array initializer must be brace-enclosed", def.line, def.col);
    Type* ir_elem_ty = get_ir_type(type);
    Type* ir_ty = ir_elem_ty;
    for (auto it = sym.dims.rbegin(); it != sym.dims.rend(); ++it) {
        ir_ty = ctx_.get_array_type(ir_ty, *it);
    }

    if (sym_table_.is_global_scope()) {
        // Global variable
        Constant* init_c = nullptr;
        if (sym.is_array) {
            if (!def.init || (def.init->is_list && def.init->list.empty())) {
                init_c = ConstantZero::get(ir_ty);
            } else {
                std::vector<Constant*> flat_consts;
                flatten_const_init_val(def.init.get(), sym.dims, 0, flat_consts, type);
                if (is_const) {
                    if (type == DataType::Float) {
                        for (auto* c : flat_consts) sym.const_float_arr.push_back(static_cast<ConstantFloat*>(c)->value());
                    } else {
                        for (auto* c : flat_consts) sym.const_int_arr.push_back(static_cast<ConstantInt*>(c)->value());
                    }
                }
                size_t offset = 0;
                init_c = make_nested_constant_array(ctx_, ir_elem_ty, sym.dims, 0, flat_consts, offset);
            }
        } else {
            if (def.init) {
                if (type == DataType::Float) {
                    float fv = (scalar_init ? eval_const_float(scalar_init) : 0.0f);
                    sym.const_float = fv;
                    init_c = ConstantFloat::get(ctx_, fv);
                } else {
                    int32_t iv = (scalar_init ? eval_const_int(scalar_init) : 0);
                    sym.const_int = iv;
                    init_c = ConstantInt::get(ctx_, iv);
                }
            } else {
                init_c = ConstantZero::get(ir_ty);
            }
        }

        auto gv = std::make_unique<GlobalVariable>(ir_ty, is_const, init_c, def.name, &module_);
        sym.ir_val = module_.add_global(std::move(gv));
        if (!sym_table_.insert(sym)) report_error("Duplicate declaration: " + sym.name);
    } else {
        // Local variable: allocate in entry block
        assert(cur_func_);
        auto* entry_bb = cur_func_->get_entry_block();
        // Insert alloca at beginning of entry block (or before first non-alloca)
        Instruction* pos = nullptr;
        for (const auto& inst : entry_bb->instructions()) {
            if (!inst->is_alloca()) {
                pos = inst.get();
                break;
            }
        }

        std::unique_ptr<Instruction> alloca_inst = std::make_unique<AllocaInst>(ir_ty, ctx_, entry_bb, def.name);
        Instruction* alloca_ptr = nullptr;
        if (pos) {
            alloca_ptr = entry_bb->insert_before(pos, std::move(alloca_inst));
        } else {
            alloca_ptr = entry_bb->append_instruction(std::move(alloca_inst));
        }
        sym.ir_val = alloca_ptr;

        if (is_const) {
            if (sym.is_array) {
                std::vector<Constant*> flat_consts;
                flatten_const_init_val(def.init.get(), sym.dims, 0, flat_consts, type);
                if (type == DataType::Float) {
                    for (auto* c : flat_consts) sym.const_float_arr.push_back(static_cast<ConstantFloat*>(c)->value());
                } else {
                    for (auto* c : flat_consts) sym.const_int_arr.push_back(static_cast<ConstantInt*>(c)->value());
                }
            } else if (def.init) {
                if (type == DataType::Float) {
                    sym.const_float = (scalar_init ? eval_const_float(scalar_init) : 0.0f);
                } else {
                    sym.const_int = (scalar_init ? eval_const_int(scalar_init) : 0);
                }
            }
        }

        if (!sym_table_.insert(sym)) report_error("Duplicate declaration: " + sym.name);

        // Store initialization in current block
        if (def.init) {
            if (is_tensor && !def.init->is_list) {
                build_tensor_expr(def.init->expr.get(), get_tensor_base_ptr(sym), sym);
            } else if (sym.is_array) {
                size_t count=1;
                for(int dimension:sym.dims) count*=static_cast<size_t>(dimension);
                auto* memset_fn=module_.get_function(".sysy.runtime.memset");
                Value* base=coerce(alloca_ptr,ctx_.get_pointer_type(ir_elem_ty));
                auto clear=[&](Value* pointer,size_t elements) {
                    emit(std::make_unique<CallInst>(memset_fn,std::vector<Value*>{
                        coerce(pointer,memset_fn->function_type()->param_types()[0]),
                        ConstantInt::get(ctx_,0),ConstantInt::get(ctx_,static_cast<int32_t>(elements*4))}));
                };
                clear(base,count);
                std::vector<InitEntry> entries;
                plan_initializer(def.init.get(),sym.dims,0,0,entries);
                for(const auto& entry:entries) {
                    auto* pointer=emit(std::make_unique<GetElementPtrInst>(base,
                        std::vector<Value*>{ConstantInt::get(ctx_,static_cast<int32_t>(entry.index))},ctx_));
                    if(entry.expression) {
                        // Finish each initializer, including its store, before evaluating
                        // the next expression. Calls can observe or modify this array.
                        Value* value=coerce(build_expr(entry.expression),ir_elem_ty);
                        emit(std::make_unique<StoreInst>(value,pointer,ctx_));
                        release_tensor_allocations();
                    } else if(entry.index!=0 || entry.zero_count!=count) clear(pointer,entry.zero_count);
                }
            } else {
                Value* init_val = scalar_init ? build_expr(scalar_init) : static_cast<Value*>(ConstantZero::get(ir_elem_ty));
                init_val = coerce(init_val, ir_elem_ty);
                emit(std::make_unique<StoreInst>(init_val, alloca_ptr, ctx_));
            }
        }
    }
}

void IRBuilder::build_func_def(const FuncDefAST& func) {
    DiagnosticScope diagnostic(*this,&func);
    if (func.name == "starttime" || func.name == "stoptime" || module_.get_function(func.name))
        report_error("Duplicate function: " + func.name, func.line, func.col);
    Type* ret_ty = func.is_tensor_return ? static_cast<Type*>(ctx_.get_pointer_type(ctx_.get_i32_type()))
                                         : get_ir_type(func.return_type);
    std::vector<Type*> param_types;
    std::vector<std::vector<int>> shapes;
    if (func.is_tensor_return) tensor_return_rank(func.name);

    for (const auto& p : func.params) {
        Type* p_elem_ty = get_ir_type(p->type);
        std::vector<int> shape;
        if (p->is_array || p->is_tensor) {
            shape.push_back(0);
            for (const auto& d : p->dims) {
                auto dimension = try_eval_const(d.get());
                if (!dimension || dimension->floating || dimension->integer < 0)
                    report_error("Parameter dimensions must be nonnegative integer constants", p->line, p->col);
                shape.push_back(dimension->integer);
            }
        }
        int64_t trailing_elements = shape.size()>1 && std::find(shape.begin()+1,shape.end(),0)!=shape.end()?0:1;
        for (size_t dim=1;dim<shape.size();++dim) {
            if (shape[dim] && trailing_elements > (INT32_MAX/4)/shape[dim]) report_error("Array parameter shape is too large",p->line,p->col);
            trailing_elements *= shape[dim];
        }
        shapes.push_back(shape);
        if (p->is_tensor) {
            param_types.push_back(ctx_.get_pointer_type(p_elem_ty));
            for (size_t dim=0;dim<shape.size();++dim) param_types.push_back(ctx_.get_i32_type());
        } else if (p->is_array) {
            // First dimension omitted -> pointer to subarray or element
            Type* sub_ty = p_elem_ty;
            for (auto it = p->dims.rbegin(); it != p->dims.rend(); ++it) {
                int dim = eval_const_int(it->get());
                sub_ty = ctx_.get_array_type(sub_ty, dim);
            }
            param_types.push_back(ctx_.get_pointer_type(sub_ty));
        } else {
            param_types.push_back(p_elem_ty);
        }
    }

    parameter_shapes_[func.name] = shapes;
    auto* func_ty = ctx_.get_function_type(ret_ty, param_types);
    auto fn = std::make_unique<Function>(func_ty, func.name, &module_, false);
    cur_func_ = module_.add_function(std::move(fn));
    cur_func_->set_representation(IRLevel::HIR,SF_STRUCTURED | SF_SSA);

    sym_table_.enter_scope();

    cur_bb_ = cur_func_->create_block(ctx_, "entry");

    // Process parameters
    size_t argument_index = 0;
    for (size_t i = 0; i < func.params.size(); ++i) {
        const auto& p = func.params[i];
        Argument* arg = cur_func_->get_arg(argument_index++);
        arg->set_name(p->name);

        Symbol sym;
        sym.name = p->name;
        sym.type = p->type;
        sym.is_const = false;
        sym.is_tensor = p->is_tensor;
        sym.is_array = (p->is_array || p->is_tensor);
        sym.is_parameter = sym.is_array;
        sym.dims = shapes[i];
        if (p->is_tensor) {
            for (size_t dimension=0;dimension<sym.dims.size();++dimension) {
                auto* size = cur_func_->get_arg(argument_index++);
                size->set_name(p->name + ".dim" + std::to_string(dimension));
                sym.runtime_dims.push_back(size);
            }
        }

        // Allocate local slot for parameter
        auto* alloca_inst = cur_bb_->append_instruction(std::make_unique<AllocaInst>(arg->type(), ctx_, cur_bb_, p->name));
        cur_bb_->append_instruction(std::make_unique<StoreInst>(arg, alloca_inst, ctx_));
        sym.ir_val = alloca_inst;

        if (!sym_table_.insert(sym)) report_error("Duplicate declaration: " + sym.name,p->line,p->col);
    }

    // Build body
    build_block(*func.body);

    // Fallthrough return if not terminated
    if (cur_bb_ && !cur_bb_->has_terminator()) {
        if (func.return_type == DataType::Void) {
            emit(std::make_unique<ReturnInst>(ctx_));
        } else if (func.is_tensor_return) {
            emit(std::make_unique<ReturnInst>(ConstantZero::get(ret_ty), ctx_));
        } else if (func.name == "main") {
            emit(std::make_unique<ReturnInst>(ConstantInt::get(ctx_, 0), ctx_));
        } else if (func.return_type == DataType::Float) {
            emit(std::make_unique<ReturnInst>(ConstantFloat::get(ctx_, 0.0f), ctx_));
        } else {
            emit(std::make_unique<ReturnInst>(ConstantInt::get(ctx_, 0), ctx_));
        }
    }

    CFGAnalysis::compute_cfg(*cur_func_);

    sym_table_.exit_scope();
    cur_func_ = nullptr;
    cur_bb_ = nullptr;
}

void IRBuilder::build_block(const BlockAST& block) {
    cur_func_->begin_region("scope",cur_bb_);
    sym_table_.enter_scope();
    for (const auto& item : block.items) {
        // Semantic rules apply even after return/break/continue. Lower the
        // remaining syntax into a disconnected block; CFG cleanup removes it.
        if (cur_bb_ && cur_bb_->has_terminator()) cur_bb_=cur_func_->create_block(ctx_, "unreachable");
        if (auto* decl = dynamic_cast<const DeclAST*>(item.get())) {
            build_decl(*decl);
        } else if (auto* stmt = dynamic_cast<const StmtAST*>(item.get())) {
            build_stmt(*stmt);
        }
    }
    sym_table_.exit_scope();
    cur_func_->end_region();
}

void IRBuilder::build_stmt(const StmtAST& stmt) {
    DiagnosticScope diagnostic(*this,&stmt);
    if (auto* assign = dynamic_cast<const AssignStmtAST*>(&stmt)) {
        auto* sym = sym_table_.lookup(assign->lval->name);
        if (sym && sym->is_tensor && assign->lval->indices.size() < sym->dims.size()) {
            if (sym->is_const) report_error("Cannot assign to const tensor", stmt.line, stmt.col);
            auto destination = tensor_lval(*assign->lval);
            auto source = tensor_value(assign->expr.get());
            if (!source.dims.empty()) require_tensor_shape(destination.dims, source.dims);
            if (source.dims.empty())
                emit_tensor_copy_loop(destination.data, source.data, tensor_element_count(destination.dims), destination.element_type, true);
            else
                emit_tensor_copy_loop(destination.data, source.data, tensor_element_count(destination.dims), destination.element_type);
            release_tensor_allocations();
            return;
        }

        Value* lval_ptr = build_lval(*assign->lval, true);
        Value* rhs = build_expr(assign->expr.get());
        Type* elem_ty = static_cast<PointerType*>(lval_ptr->type())->pointee();
        rhs = coerce(rhs, elem_ty);
        emit(std::make_unique<StoreInst>(rhs, lval_ptr, ctx_));
        release_tensor_allocations();
    } else if (auto* expr_stmt = dynamic_cast<const ExprStmtAST*>(&stmt)) {
        if (expr_stmt->expr) {
            build_expr(expr_stmt->expr.get());
            release_tensor_allocations();
        }
    } else if (auto* blk = dynamic_cast<const BlockAST*>(&stmt)) {
        build_block(*blk);
    } else if (auto* if_stmt = dynamic_cast<const IfStmtAST*>(&stmt)) {
        auto* then_bb = cur_func_->create_block(ctx_, "if.then");
        auto* else_bb = if_stmt->else_stmt ? cur_func_->create_block(ctx_, "if.else") : nullptr;
        auto* merge_bb = cur_func_->create_block(ctx_, "if.merge");

        cur_func_->begin_region("if",cur_bb_,merge_bb);
        BasicBlock* false_target = else_bb ? else_bb : merge_bb;
        build_condition_branch(if_stmt->cond.get(), then_bb, false_target);

        // Then block
        cur_bb_ = then_bb;
        build_stmt(*if_stmt->then_stmt);
        if (cur_bb_ && !cur_bb_->has_terminator()) {
            emit(std::make_unique<BranchInst>(merge_bb, ctx_));
        }

        // Else block
        if (else_bb) {
            cur_bb_ = else_bb;
            build_stmt(*if_stmt->else_stmt);
            if (cur_bb_ && !cur_bb_->has_terminator()) {
                emit(std::make_unique<BranchInst>(merge_bb, ctx_));
            }
        }

        cur_bb_ = merge_bb;
        cur_func_->end_region();
    } else if (auto* while_stmt = dynamic_cast<const WhileStmtAST*>(&stmt)) {
        auto* cond_bb = cur_func_->create_block(ctx_, "while.cond");
        auto* body_bb = cur_func_->create_block(ctx_, "while.body");
        auto* exit_bb = cur_func_->create_block(ctx_, "while.exit");

        emit(std::make_unique<BranchInst>(cond_bb, ctx_));

        cur_func_->begin_region("while",cond_bb,exit_bb);
        // Cond block
        cur_bb_ = cond_bb;
        build_condition_branch(while_stmt->cond.get(), body_bb, exit_bb);

        // Body block
        cur_bb_ = body_bb;
        loop_stack_.push_back({cond_bb, exit_bb});
        build_stmt(*while_stmt->body);
        if (cur_bb_ && !cur_bb_->has_terminator()) {
            emit(std::make_unique<BranchInst>(cond_bb, ctx_));
        }
        loop_stack_.pop_back();

        cur_bb_ = exit_bb;
        cur_func_->end_region();
    } else if (dynamic_cast<const BreakStmtAST*>(&stmt)) {
        if (!loop_stack_.empty()) {
            emit(std::make_unique<BranchInst>(loop_stack_.back().exit_bb, ctx_));
        } else {
            report_error("break statement outside of loop");
        }
    } else if (dynamic_cast<const ContinueStmtAST*>(&stmt)) {
        if (!loop_stack_.empty()) {
            emit(std::make_unique<BranchInst>(loop_stack_.back().cond_bb, ctx_));
        } else {
            report_error("continue statement outside of loop");
        }
    } else if (auto* ret_stmt = dynamic_cast<const ReturnStmtAST*>(&stmt)) {
        if (ret_stmt->expr) {
            if (cur_func_->return_type()->is_void()) report_error("Void function cannot return a value", ret_stmt->line, ret_stmt->col);
            if (cur_func_->return_type()->is_pointer()) {
                auto value = tensor_value(ret_stmt->expr.get());
                if (value.dims.size() != tensor_return_rank(cur_func_->name()))
                    report_error("Tensor return rank mismatch", stmt.line, stmt.col);
                // Borrowed variables/parameters must be copied before their frame disappears.
                // An owned temporary may be transferred directly to the caller.
                if (!value.owner || value.element_type != function_definitions_.at(cur_func_->name())->return_type) {
                    auto copy = allocate_tensor(value.dims, function_definitions_.at(cur_func_->name())->return_type);
                    emit_tensor_copy_loop(copy.data, value.data, tensor_element_count(value.dims), copy.element_type);
                    value = copy;
                }
                release_tensor_allocations(value.owner);
                emit(std::make_unique<ReturnInst>(value.owner, ctx_));
                return;
            }
            Value* v = build_expr(ret_stmt->expr.get());
            v = coerce(v, cur_func_->return_type());
            release_tensor_allocations();
            emit(std::make_unique<ReturnInst>(v, ctx_));
        } else {
            if (!cur_func_->return_type()->is_void()) report_error("Non-void function must return a value", ret_stmt->line, ret_stmt->col);
            emit(std::make_unique<ReturnInst>(ctx_));
        }
    }
}

void IRBuilder::build_condition_branch(const ExprAST* cond, BasicBlock* true_bb, BasicBlock* false_bb) {
    if (auto* bin = dynamic_cast<const BinaryExprAST*>(cond)) {
        if (bin->op == BinaryOp::And) {
            auto* rhs_bb = cur_func_->create_block(ctx_, "and.rhs");
            build_condition_branch(bin->lhs.get(), rhs_bb, false_bb);
            cur_bb_ = rhs_bb;
            build_condition_branch(bin->rhs.get(), true_bb, false_bb);
            return;
        } else if (bin->op == BinaryOp::Or) {
            auto* rhs_bb = cur_func_->create_block(ctx_, "or.rhs");
            build_condition_branch(bin->lhs.get(), true_bb, rhs_bb);
            cur_bb_ = rhs_bb;
            build_condition_branch(bin->rhs.get(), true_bb, false_bb);
            return;
        }
    } else if (auto* un = dynamic_cast<const UnaryExprAST*>(cond)) {
        if (un->op == UnaryOp::Not) {
            build_condition_branch(un->expr.get(), false_bb, true_bb);
            return;
        }
    }

    Value* val = build_expr(cond);
    if (val->type()->is_pointer() || val->type()->is_void()) report_error("Condition requires a scalar value", cond->line, cond->col);
    val = coerce(val, ctx_.get_i1_type());
    release_tensor_allocations();
    emit(std::make_unique<BranchInst>(val, true_bb, false_bb, ctx_));
}

Value* IRBuilder::build_expr(const ExprAST* expr) {
    DiagnosticScope diagnostic(*this,expr);
    if (!expr) return nullptr;
    if (is_tensor_expr(expr)) return tensor_value(expr).data;

    if (auto* il = dynamic_cast<const IntLiteralAST*>(expr)) {
        return ConstantInt::get(ctx_, il->value);
    }
    if (auto* fl = dynamic_cast<const FloatLiteralAST*>(expr)) {
        return ConstantFloat::get(ctx_, fl->value);
    }
    if (auto* text = dynamic_cast<const StringLiteralAST*>(expr)) {
        std::vector<Constant*> bytes;
        for (unsigned char c : text->value) bytes.push_back(ConstantInt::get_byte(ctx_, c));
        bytes.push_back(ConstantInt::get_byte(ctx_, 0));
        auto* ty = ctx_.get_array_type(ctx_.get_i8_type(), bytes.size());
        auto* init = ctx_.own_constant(std::make_unique<ConstantArray>(ty, std::move(bytes)));
        auto* gv = module_.add_global(std::make_unique<GlobalVariable>(ty, true, init, ".L.sysy_str_" + std::to_string(string_counter_++), &module_));
        return emit(std::make_unique<GetElementPtrInst>(gv, std::vector<Value*>{ConstantInt::get(ctx_,0),ConstantInt::get(ctx_,0)}, ctx_));
    }
    if (auto* lv = dynamic_cast<const LValAST*>(expr)) {
        return build_lval(*lv, false);
    }
    if (auto* un = dynamic_cast<const UnaryExprAST*>(expr)) {
        return build_unary_expr(*un);
    }
    if (auto* bin = dynamic_cast<const BinaryExprAST*>(expr)) {
        return build_binary_expr(*bin);
    }
    if (auto* call = dynamic_cast<const CallExprAST*>(expr)) {
        return build_call_expr(*call);
    }

    return nullptr;
}

Value* IRBuilder::build_lval(const LValAST& lval, bool as_lvalue) {
    DiagnosticScope diagnostic(*this,&lval);
    auto* sym = sym_table_.lookup(lval.name);
    if (!sym) {
        report_error("Undeclared identifier: " + lval.name);
        return nullptr;
    }

    if (as_lvalue && sym->is_const)
        report_error("Cannot assign to const: " + lval.name, lval.line, lval.col);
    if ((!sym->is_array && !lval.indices.empty()) || lval.indices.size() > sym->dims.size())
        report_error("Invalid number of array indices", lval.line, lval.col);
    if (as_lvalue && sym->is_array && lval.indices.size() != sym->dims.size())
        report_error("Assignment requires a scalar array element", lval.line, lval.col);

    if (sym->is_tensor) {
        auto value = tensor_lval(lval);
        if (as_lvalue || !value.dims.empty()) return value.data;
        return emit(std::make_unique<LoadInst>(value.data));
    }

    // Check if it is a const scalar that can be directly folded
    if (!as_lvalue && sym->is_const && lval.indices.empty() && !sym->is_array) {
        if (sym->type == DataType::Float) {
            return ConstantFloat::get(ctx_, sym->const_float);
        } else {
            return ConstantInt::get(ctx_, sym->const_int);
        }
    }

    Value* base = sym->ir_val;
    assert(base && base->type()->is_pointer());

    if (!sym->is_array) {
        // Scalar variable
        if (as_lvalue) {
            return base;
        }
        return emit(std::make_unique<LoadInst>(base));
    }

    // Array variable
    std::vector<Value*> indices;
    bool is_param_array = sym->is_parameter; // first dimension 0 means passed as pointer

    if (is_param_array) {
        // Pointer parameter: load the pointer value first!
        Value* loaded_ptr = emit(std::make_unique<LoadInst>(base));
        if (lval.indices.empty()) {
            return loaded_ptr;
        }
        // First index indexes directly through loaded_ptr
        indices.push_back(coerce(build_expr(lval.indices[0].get()), ctx_.get_i32_type()));
        for (size_t i = 1; i < lval.indices.size(); ++i) {
            indices.push_back(coerce(build_expr(lval.indices[i].get()), ctx_.get_i32_type()));
        }
        Value* gep = emit(std::make_unique<GetElementPtrInst>(loaded_ptr, indices, ctx_));
        if (as_lvalue || lval.indices.size() == sym->dims.size()) {
            if (as_lvalue) return gep;
            return emit(std::make_unique<LoadInst>(gep));
        }
        // Subarray decay: gep pointer to first element of remaining
        std::vector<Value*> decay_indices = {ConstantInt::get(ctx_, 0), ConstantInt::get(ctx_, 0)};
        return emit(std::make_unique<GetElementPtrInst>(gep, decay_indices, ctx_));
    } else {
        // Local or global array
        if (lval.indices.empty()) {
            std::vector<Value*> decay_indices = {ConstantInt::get(ctx_, 0), ConstantInt::get(ctx_, 0)};
            return emit(std::make_unique<GetElementPtrInst>(base, decay_indices, ctx_));
        }
        indices.push_back(ConstantInt::get(ctx_, 0));
        for (const auto& idx_expr : lval.indices) {
            indices.push_back(coerce(build_expr(idx_expr.get()), ctx_.get_i32_type()));
        }
        Value* gep = emit(std::make_unique<GetElementPtrInst>(base, indices, ctx_));
        if (as_lvalue || lval.indices.size() == sym->dims.size()) {
            if (as_lvalue) return gep;
            return emit(std::make_unique<LoadInst>(gep));
        }
        // Array decay
        std::vector<Value*> decay_indices = {ConstantInt::get(ctx_, 0), ConstantInt::get(ctx_, 0)};
        return emit(std::make_unique<GetElementPtrInst>(gep, decay_indices, ctx_));
    }
}

Value* IRBuilder::build_unary_expr(const UnaryExprAST& un) {
    Value* v = build_expr(un.expr.get());
    switch (un.op) {
        case UnaryOp::Plus:
            return v;
        case UnaryOp::Minus: {
            if (v->type() == ctx_.get_float_type()) {
                return emit(std::make_unique<FNegInst>(v));
            } else {
                v = coerce(v, ctx_.get_i32_type());
                // The lexer represents the magnitude of -2147483648 by its
                // i32 bit pattern. Preserve this valid literal construction.
                auto* literal=dynamic_cast<const IntLiteralAST*>(un.expr.get());
                if(literal && literal->value==std::numeric_limits<int32_t>::min())
                    return ConstantInt::get(ctx_,std::numeric_limits<int32_t>::min());
                return emit_source_integer(Opcode::Sub,ConstantInt::get(ctx_,0),v);
            }
        }
        case UnaryOp::Not: {
            Value* cond = coerce(v, ctx_.get_i1_type());
            Value* not_cond = emit(std::make_unique<ICmpInst>(ICmpPred::EQ, cond, ConstantInt::get_bool(ctx_, false), ctx_));
            return coerce(not_cond, ctx_.get_i32_type());
        }
    }
    return v;
}

Value* IRBuilder::build_binary_expr(const BinaryExprAST& bin) {
    if (bin.op == BinaryOp::And || bin.op == BinaryOp::Or) {
        // Value context for logical AND / OR
        // Use alloca + branches to implement short circuit!
        auto* res_alloca = cur_func_->get_entry_block()->insert_front(
            std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, cur_func_->get_entry_block(), "logical_res"));

        auto* true_bb = cur_func_->create_block(ctx_, "log.true");
        auto* false_bb = cur_func_->create_block(ctx_, "log.false");
        auto* merge_bb = cur_func_->create_block(ctx_, "log.merge");

        build_condition_branch(&bin, true_bb, false_bb);

        cur_bb_ = true_bb;
        emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 1), res_alloca, ctx_));
        emit(std::make_unique<BranchInst>(merge_bb, ctx_));

        cur_bb_ = false_bb;
        emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), res_alloca, ctx_));
        emit(std::make_unique<BranchInst>(merge_bb, ctx_));

        cur_bb_ = merge_bb;
        return emit(std::make_unique<LoadInst>(res_alloca));
    }

    Value* lhs = build_expr(bin.lhs.get());
    Value* rhs = build_expr(bin.rhs.get());

    if (lhs->type()->is_void() || rhs->type()->is_void() || lhs->type()->is_pointer() || rhs->type()->is_pointer())
        report_error("Arithmetic requires scalar numeric operands", bin.line, bin.col);
    bool is_float = (lhs->type() == ctx_.get_float_type() || rhs->type() == ctx_.get_float_type());
    if (is_float) {
        lhs = coerce(lhs, ctx_.get_float_type());
        rhs = coerce(rhs, ctx_.get_float_type());
    } else {
        lhs = coerce(lhs, ctx_.get_i32_type());
        rhs = coerce(rhs, ctx_.get_i32_type());
    }

    if (is_float) {
        switch (bin.op) {
            case BinaryOp::Add: return emit(std::make_unique<BinaryInst>(Opcode::FAdd, lhs, rhs));
            case BinaryOp::Sub: return emit(std::make_unique<BinaryInst>(Opcode::FSub, lhs, rhs));
            case BinaryOp::Mul: return emit(std::make_unique<BinaryInst>(Opcode::FMul, lhs, rhs));
            case BinaryOp::Div: return emit(std::make_unique<BinaryInst>(Opcode::FDiv, lhs, rhs));
            case BinaryOp::Lt:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::OLT, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Le:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::OLE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Gt:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::OGT, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Ge:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::OGE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Eq:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::OEQ, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Ne:  return coerce(emit(std::make_unique<FCmpInst>(FCmpPred::UNE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            default: break;
        }
    } else {
        switch (bin.op) {
            case BinaryOp::Add: return emit_source_integer(Opcode::Add,lhs,rhs);
            case BinaryOp::Sub: return emit_source_integer(Opcode::Sub,lhs,rhs);
            case BinaryOp::Mul: return emit_source_integer(Opcode::Mul,lhs,rhs);
            case BinaryOp::Div: return emit(std::make_unique<BinaryInst>(Opcode::SDiv, lhs, rhs));
            case BinaryOp::Mod: return emit(std::make_unique<BinaryInst>(Opcode::SRem, lhs, rhs));
            case BinaryOp::Lt:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::SLT, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Le:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::SLE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Gt:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::SGT, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Ge:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::SGE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Eq:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::EQ, lhs, rhs, ctx_)), ctx_.get_i32_type());
            case BinaryOp::Ne:  return coerce(emit(std::make_unique<ICmpInst>(ICmpPred::NE, lhs, rhs, ctx_)), ctx_.get_i32_type());
            default: break;
        }
    }
    report_error("Invalid binary operator or operand type", bin.line, bin.col);
    return nullptr;
}

void IRBuilder::emit_tensor_binary_loop(Value* dest_ptr, Value* lhs_ptr, Value* rhs_ptr,
                                        bool lhs_is_scalar, bool rhs_is_scalar,
                                        BinaryOp op, Value* total_elems, DataType type) {
    auto* loop_cond = cur_func_->create_block(ctx_, "tensor.cond");
    auto* loop_body = cur_func_->create_block(ctx_, "tensor.body");
    auto* loop_end = cur_func_->create_block(ctx_, "tensor.end");

    auto* entry_bb = cur_func_->get_entry_block();
    auto idx_alloca = std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "tensor.idx");
    Instruction* idx_ptr = entry_bb->insert_front(std::move(idx_alloca));

    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_cond;
    Value* cur_idx = emit(std::make_unique<LoadInst>(idx_ptr));
    Value* cmp = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, cur_idx, total_elems, ctx_));
    emit(std::make_unique<BranchInst>(cmp, loop_body, loop_end, ctx_));

    cur_bb_ = loop_body;
    Value* cur_idx_body = emit(std::make_unique<LoadInst>(idx_ptr));

    Value* val_l = nullptr;
    if (lhs_is_scalar) {
        val_l = lhs_ptr;
    } else {
        std::vector<Value*> idx_vec = {cur_idx_body};
        Value* elem_l_ptr = emit(std::make_unique<GetElementPtrInst>(lhs_ptr, idx_vec, ctx_));
        val_l = emit(std::make_unique<LoadInst>(elem_l_ptr));
    }

    Value* val_r = nullptr;
    if (rhs_is_scalar) {
        val_r = rhs_ptr;
    } else {
        std::vector<Value*> idx_vec = {cur_idx_body};
        Value* elem_r_ptr = emit(std::make_unique<GetElementPtrInst>(rhs_ptr, idx_vec, ctx_));
        val_r = emit(std::make_unique<LoadInst>(elem_r_ptr));
    }

    Value* res = nullptr;
    if (type == DataType::Float) {
        val_l = coerce(val_l, ctx_.get_float_type());
        val_r = coerce(val_r, ctx_.get_float_type());
        switch (op) {
            case BinaryOp::Add: res = emit(std::make_unique<BinaryInst>(Opcode::FAdd, val_l, val_r)); break;
            case BinaryOp::Sub: res = emit(std::make_unique<BinaryInst>(Opcode::FSub, val_l, val_r)); break;
            case BinaryOp::Mul: res = emit(std::make_unique<BinaryInst>(Opcode::FMul, val_l, val_r)); break;
            case BinaryOp::Div: res = emit(std::make_unique<BinaryInst>(Opcode::FDiv, val_l, val_r)); break;
            default: report_error("Unsupported tensor operator");
        }
    } else {
        val_l = coerce(val_l, ctx_.get_i32_type());
        val_r = coerce(val_r, ctx_.get_i32_type());
        switch (op) {
            case BinaryOp::Add: res = emit(std::make_unique<BinaryInst>(Opcode::Add, val_l, val_r)); break;
            case BinaryOp::Sub: res = emit(std::make_unique<BinaryInst>(Opcode::Sub, val_l, val_r)); break;
            case BinaryOp::Mul: res = emit(std::make_unique<BinaryInst>(Opcode::Mul, val_l, val_r)); break;
            case BinaryOp::Div: res = emit(std::make_unique<BinaryInst>(Opcode::SDiv, val_l, val_r)); break;
            case BinaryOp::Mod: res = emit(std::make_unique<BinaryInst>(Opcode::SRem, val_l, val_r)); break;
            default: report_error("Unsupported tensor operator");
        }
    }

    std::vector<Value*> dest_idx_vec = {cur_idx_body};
    Value* elem_d_ptr = emit(std::make_unique<GetElementPtrInst>(dest_ptr, dest_idx_vec, ctx_));
    emit(std::make_unique<StoreInst>(res, elem_d_ptr, ctx_));

    Value* next_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_idx_body, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_idx, idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_end;
}

void IRBuilder::emit_tensor_unary_loop(Value* dest_ptr, Value* src_ptr, UnaryOp op, Value* total_elems, DataType type) {
    auto* loop_cond = cur_func_->create_block(ctx_, "tensor.u.cond");
    auto* loop_body = cur_func_->create_block(ctx_, "tensor.u.body");
    auto* loop_end = cur_func_->create_block(ctx_, "tensor.u.end");

    auto* entry_bb = cur_func_->get_entry_block();
    auto idx_alloca = std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "tensor.u.idx");
    Instruction* idx_ptr = entry_bb->insert_front(std::move(idx_alloca));

    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_cond;
    Value* cur_idx = emit(std::make_unique<LoadInst>(idx_ptr));
    Value* cmp = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, cur_idx, total_elems, ctx_));
    emit(std::make_unique<BranchInst>(cmp, loop_body, loop_end, ctx_));

    cur_bb_ = loop_body;
    Value* cur_idx_body = emit(std::make_unique<LoadInst>(idx_ptr));
    std::vector<Value*> idx_vec = {cur_idx_body};
    Value* src_elem_ptr = emit(std::make_unique<GetElementPtrInst>(src_ptr, idx_vec, ctx_));
    Value* val = emit(std::make_unique<LoadInst>(src_elem_ptr));

    Value* res = nullptr;
    if (op == UnaryOp::Not) {
        auto* condition = coerce(val, ctx_.get_i1_type());
        res = coerce(emit(std::make_unique<ICmpInst>(ICmpPred::EQ, condition, ConstantInt::get_bool(ctx_,false), ctx_)),ctx_.get_i32_type());
    } else if (type == DataType::Float) {
        val = coerce(val, ctx_.get_float_type());
        res = emit(std::make_unique<FNegInst>(val));
    } else {
        val = coerce(val, ctx_.get_i32_type());
        res = emit(std::make_unique<BinaryInst>(Opcode::Sub, ConstantInt::get(ctx_, 0), val));
    }

    Value* dest_elem_ptr = emit(std::make_unique<GetElementPtrInst>(dest_ptr, idx_vec, ctx_));
    emit(std::make_unique<StoreInst>(res, dest_elem_ptr, ctx_));

    Value* next_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_idx_body, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_idx, idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_end;
}

void IRBuilder::emit_tensor_copy_loop(Value* dest_ptr, Value* src_ptr, Value* total_elems, DataType type, bool broadcast) {
    auto* loop_cond = cur_func_->create_block(ctx_, "tensor.cp.cond");
    auto* loop_body = cur_func_->create_block(ctx_, "tensor.cp.body");
    auto* loop_end = cur_func_->create_block(ctx_, "tensor.cp.end");

    auto* entry_bb = cur_func_->get_entry_block();
    auto idx_alloca = std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "tensor.cp.idx");
    Instruction* idx_ptr = entry_bb->insert_front(std::move(idx_alloca));

    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_cond;
    Value* cur_idx = emit(std::make_unique<LoadInst>(idx_ptr));
    Value* cmp = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, cur_idx, total_elems, ctx_));
    emit(std::make_unique<BranchInst>(cmp, loop_body, loop_end, ctx_));

    cur_bb_ = loop_body;
    Value* cur_idx_body = emit(std::make_unique<LoadInst>(idx_ptr));
    std::vector<Value*> idx_vec = {cur_idx_body};
    Value* val = src_ptr;
    if (!broadcast) {
        auto* src_elem_ptr = emit(std::make_unique<GetElementPtrInst>(src_ptr, idx_vec, ctx_));
        val = emit(std::make_unique<LoadInst>(src_elem_ptr));
    }
    val = coerce(val, get_ir_type(type));

    Value* dest_elem_ptr = emit(std::make_unique<GetElementPtrInst>(dest_ptr, idx_vec, ctx_));
    emit(std::make_unique<StoreInst>(val, dest_elem_ptr, ctx_));

    Value* next_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_idx_body, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_idx, idx_ptr, ctx_));
    emit(std::make_unique<BranchInst>(loop_cond, ctx_));

    cur_bb_ = loop_end;
}

void IRBuilder::emit_tensor_matmul(Value* dest_ptr, Value* a_ptr, Value* b_ptr,
                                   Value* M, Value* K, Value* N, DataType type) {
    auto* entry_bb = cur_func_->get_entry_block();
    Instruction* i_var = entry_bb->insert_front(std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "mm.i"));
    Instruction* j_var = entry_bb->insert_front(std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "mm.j"));
    Instruction* k_var = entry_bb->insert_front(std::make_unique<AllocaInst>(ctx_.get_i32_type(), ctx_, entry_bb, "mm.k"));
    Type* sum_ty = (type == DataType::Float) ? static_cast<Type*>(ctx_.get_float_type()) : static_cast<Type*>(ctx_.get_i32_type());
    Instruction* sum_var = entry_bb->insert_front(std::make_unique<AllocaInst>(sum_ty, ctx_, entry_bb, "mm.sum"));

    auto* loop_i_cond = cur_func_->create_block(ctx_, "mm.i.cond");
    auto* loop_i_body = cur_func_->create_block(ctx_, "mm.i.body");
    auto* loop_i_end  = cur_func_->create_block(ctx_, "mm.i.end");

    auto* loop_j_cond = cur_func_->create_block(ctx_, "mm.j.cond");
    auto* loop_j_body = cur_func_->create_block(ctx_, "mm.j.body");
    auto* loop_j_end  = cur_func_->create_block(ctx_, "mm.j.end");

    auto* loop_k_cond = cur_func_->create_block(ctx_, "mm.k.cond");
    auto* loop_k_body = cur_func_->create_block(ctx_, "mm.k.body");
    auto* loop_k_end  = cur_func_->create_block(ctx_, "mm.k.end");

    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), i_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_i_cond, ctx_));

    cur_bb_ = loop_i_cond;
    Value* i_val = emit(std::make_unique<LoadInst>(i_var));
    Value* cmp_i = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, i_val, M, ctx_));
    emit(std::make_unique<BranchInst>(cmp_i, loop_i_body, loop_i_end, ctx_));

    cur_bb_ = loop_i_body;
    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), j_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_j_cond, ctx_));

    cur_bb_ = loop_j_cond;
    Value* j_val = emit(std::make_unique<LoadInst>(j_var));
    Value* cmp_j = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, j_val, N, ctx_));
    emit(std::make_unique<BranchInst>(cmp_j, loop_j_body, loop_j_end, ctx_));

    cur_bb_ = loop_j_body;
    Value* zero_init = (type == DataType::Float) ? static_cast<Value*>(ConstantFloat::get(ctx_, 0.0f))
                                                 : static_cast<Value*>(ConstantInt::get(ctx_, 0));
    emit(std::make_unique<StoreInst>(zero_init, sum_var, ctx_));
    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_, 0), k_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_k_cond, ctx_));

    cur_bb_ = loop_k_cond;
    Value* k_val = emit(std::make_unique<LoadInst>(k_var));
    Value* cmp_k = emit(std::make_unique<ICmpInst>(ICmpPred::SLT, k_val, K, ctx_));
    emit(std::make_unique<BranchInst>(cmp_k, loop_k_body, loop_k_end, ctx_));

    cur_bb_ = loop_k_body;
    Value* cur_i = emit(std::make_unique<LoadInst>(i_var));
    Value* cur_j = emit(std::make_unique<LoadInst>(j_var));
    Value* cur_k = emit(std::make_unique<LoadInst>(k_var));

    Value* ik = emit(std::make_unique<BinaryInst>(Opcode::Mul, cur_i, K));
    Value* a_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, ik, cur_k));
    Value* a_elem_ptr = emit(std::make_unique<GetElementPtrInst>(a_ptr, std::vector<Value*>{a_idx}, ctx_));
    Value* a_val = emit(std::make_unique<LoadInst>(a_elem_ptr));

    Value* kn = emit(std::make_unique<BinaryInst>(Opcode::Mul, cur_k, N));
    Value* b_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, kn, cur_j));
    Value* b_elem_ptr = emit(std::make_unique<GetElementPtrInst>(b_ptr, std::vector<Value*>{b_idx}, ctx_));
    Value* b_val = emit(std::make_unique<LoadInst>(b_elem_ptr));

    Value* cur_sum = emit(std::make_unique<LoadInst>(sum_var));
    Value* prod = nullptr;
    Value* new_sum = nullptr;
    if (type == DataType::Float) {
        a_val = coerce(a_val, ctx_.get_float_type());
        b_val = coerce(b_val, ctx_.get_float_type());
        prod = emit(std::make_unique<BinaryInst>(Opcode::FMul, a_val, b_val));
        new_sum = emit(std::make_unique<BinaryInst>(Opcode::FAdd, cur_sum, prod));
    } else {
        a_val = coerce(a_val, ctx_.get_i32_type());
        b_val = coerce(b_val, ctx_.get_i32_type());
        prod = emit(std::make_unique<BinaryInst>(Opcode::Mul, a_val, b_val));
        new_sum = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_sum, prod));
    }
    emit(std::make_unique<StoreInst>(new_sum, sum_var, ctx_));

    Value* next_k = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_k, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_k, k_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_k_cond, ctx_));

    cur_bb_ = loop_k_end;
    Value* final_i = emit(std::make_unique<LoadInst>(i_var));
    Value* final_j = emit(std::make_unique<LoadInst>(j_var));
    Value* in = emit(std::make_unique<BinaryInst>(Opcode::Mul, final_i, N));
    Value* c_idx = emit(std::make_unique<BinaryInst>(Opcode::Add, in, final_j));
    Value* c_elem_ptr = emit(std::make_unique<GetElementPtrInst>(dest_ptr, std::vector<Value*>{c_idx}, ctx_));
    Value* final_sum = emit(std::make_unique<LoadInst>(sum_var));
    emit(std::make_unique<StoreInst>(final_sum, c_elem_ptr, ctx_));

    Value* next_j = emit(std::make_unique<BinaryInst>(Opcode::Add, final_j, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_j, j_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_j_cond, ctx_));

    cur_bb_ = loop_j_end;
    Value* cur_i_jend = emit(std::make_unique<LoadInst>(i_var));
    Value* next_i = emit(std::make_unique<BinaryInst>(Opcode::Add, cur_i_jend, ConstantInt::get(ctx_, 1)));
    emit(std::make_unique<StoreInst>(next_i, i_var, ctx_));
    emit(std::make_unique<BranchInst>(loop_i_cond, ctx_));

    cur_bb_ = loop_i_end;
}

} // namespace sysy
