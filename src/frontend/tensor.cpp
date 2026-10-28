// Tensor lowering is an independent implementation of the public SysY2026
// operations exercised by the official suite. No compiler-framework code is used.
#include "frontend/ir_builder.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_set>

namespace sysy {

size_t IRBuilder::tensor_return_rank(const std::string& name) {
    using Scope = std::unordered_map<std::string, size_t>;
    std::function<size_t(const std::string&, std::unordered_set<std::string>)> infer;
    infer = [&](const std::string& callee, std::unordered_set<std::string> active) -> size_t {
        auto cached = tensor_return_ranks_.find(callee);
        if (cached != tensor_return_ranks_.end()) return cached->second;
        auto found = function_definitions_.find(callee);
        if (found == function_definitions_.end() || !found->second->is_tensor_return || !active.insert(callee).second)
            return 0;
        const auto& function = *found->second;
        std::function<size_t(const ExprAST*, const Scope&)> rank;
        rank = [&](const ExprAST* expr, const Scope& scope) -> size_t {
            if (auto* value = dynamic_cast<const LValAST*>(expr)) {
                auto local = scope.find(value->name);
                auto* symbol = sym_table_.lookup(value->name);
                size_t dimensions = local != scope.end() ? local->second : (symbol ? symbol->dims.size() : 0);
                return dimensions > value->indices.size() ? dimensions - value->indices.size() : 0;
            }
            if (auto* binary = dynamic_cast<const BinaryExprAST*>(expr)) {
                if (binary->op == BinaryOp::MatMul) return 2;
                return std::max(rank(binary->lhs.get(), scope), rank(binary->rhs.get(), scope));
            }
            if (auto* unary = dynamic_cast<const UnaryExprAST*>(expr)) return rank(unary->expr.get(), scope);
            if (auto* call = dynamic_cast<const CallExprAST*>(expr)) return infer(call->callee, active);
            return 0;
        };
        size_t result = 0;
        std::function<void(const ASTNode*, Scope&)> visit;
        visit = [&](const ASTNode* node, Scope& scope) {
            if (auto* block = dynamic_cast<const BlockAST*>(node)) {
                auto nested = scope;
                for (const auto& item : block->items) visit(item.get(), nested);
            } else if (auto* decl = dynamic_cast<const DeclAST*>(node)) {
                for (const auto& def : decl->defs) scope[def->name] = def->dims.size();
            } else if (auto* branch = dynamic_cast<const IfStmtAST*>(node)) {
                auto left = scope, right = scope;
                visit(branch->then_stmt.get(), left);visit(branch->else_stmt.get(), right);
            } else if (auto* loop = dynamic_cast<const WhileStmtAST*>(node)) {
                auto nested = scope;visit(loop->body.get(), nested);
            } else if (auto* ret = dynamic_cast<const ReturnStmtAST*>(node)) {
                size_t candidate = rank(ret->expr.get(), scope);
                if (result && candidate && result != candidate) report_error("Inconsistent Tensor return ranks in " + callee);
                if (candidate) result = candidate;
            }
        };
        Scope scope;
        for (const auto& parameter : function.params)
            scope[parameter->name] = parameter->is_array || parameter->is_tensor ? parameter->dims.size()+1 : 0;
        visit(function.body.get(), scope);
        if (result) tensor_return_ranks_[callee] = result;
        return result;
    };
    size_t result = infer(name, {});
    if (!result) report_error("Cannot determine Tensor return rank in " + name);
    return result;
}

bool IRBuilder::is_tensor_expr(const ExprAST* expression) {
    if (auto* value = dynamic_cast<const LValAST*>(expression)) {
        auto* symbol = sym_table_.lookup(value->name);
        return symbol && symbol->is_tensor && value->indices.size() < symbol->dims.size();
    }
    if (auto* binary = dynamic_cast<const BinaryExprAST*>(expression))
        return binary->op == BinaryOp::MatMul || is_tensor_expr(binary->lhs.get()) || is_tensor_expr(binary->rhs.get());
    if (auto* unary = dynamic_cast<const UnaryExprAST*>(expression)) return is_tensor_expr(unary->expr.get());
    if (auto* call = dynamic_cast<const CallExprAST*>(expression)) {
        auto found = function_definitions_.find(call->callee);
        return found != function_definitions_.end() && found->second->is_tensor_return;
    }
    return false;
}

void IRBuilder::require_tensor_condition(Value* condition, const std::string& message) {
    if (auto* constant = dynamic_cast<ConstantInt*>(condition)) {
        if (!constant->value()) report_error(message);
        return;
    }
    auto* valid = cur_func_->create_block(ctx_, "tensor.valid");
    auto* invalid = cur_func_->create_block(ctx_, "tensor.invalid");
    emit(std::make_unique<BranchInst>(condition, valid, invalid, ctx_));
    cur_bb_ = invalid;
    emit(std::make_unique<CallInst>(module_.get_function(".sysy.runtime.exit"), std::vector<Value*>{ConstantInt::get(ctx_,1)}));
    emit(std::make_unique<BranchInst>(valid, ctx_)); // exit does not return at runtime.
    cur_bb_ = valid;
}

Value* IRBuilder::tensor_element_count(const std::vector<Value*>& dims) {
    const int limit = (INT32_MAX - 4 * static_cast<int>(dims.size()+1)) / 4;
    bool constant=true,has_zero=false;
    for(auto* dimension:dims) {
        auto* value=dynamic_cast<ConstantInt*>(dimension);
        if(!value) constant=false;
        else {if(value->value()<0) report_error("Negative Tensor dimension");has_zero|=value->value()==0;}
    }
    if(has_zero) return ConstantInt::get(ctx_,0);
    if(constant) {
        int64_t count=1;
        for(auto* dimension:dims) {
            int value=static_cast<ConstantInt*>(dimension)->value();
            if(count>limit/value) report_error("Oversized Tensor shape");
            count*=value;
        }
        return ConstantInt::get(ctx_,static_cast<int32_t>(count));
    }
    // Start at zero for any empty shape, including a zero axis after a large axis.
    Value* zeros=ConstantInt::get(ctx_,0);
    std::vector<Value*> zero_flags;
    for(auto* dimension:dims) {
        require_tensor_condition(emit(std::make_unique<ICmpInst>(ICmpPred::SGE,dimension,ConstantInt::get(ctx_,0),ctx_)),"Negative Tensor dimension");
        auto* is_zero=emit(std::make_unique<ICmpInst>(ICmpPred::EQ,dimension,ConstantInt::get(ctx_,0),ctx_));
        auto* flag=coerce(is_zero,ctx_.get_i32_type());zero_flags.push_back(flag);
        zeros=emit(std::make_unique<BinaryInst>(Opcode::Add,zeros,flag));
    }
    Value* count=coerce(emit(std::make_unique<ICmpInst>(ICmpPred::EQ,zeros,ConstantInt::get(ctx_,0),ctx_)),ctx_.get_i32_type());
    for(size_t i=0;i<dims.size();++i) {
        auto* divisor=emit(std::make_unique<BinaryInst>(Opcode::Add,dims[i],zero_flags[i]));
        auto* maximum=emit(std::make_unique<BinaryInst>(Opcode::SDiv,ConstantInt::get(ctx_,limit),divisor));
        require_tensor_condition(emit(std::make_unique<ICmpInst>(ICmpPred::SLE,count,maximum,ctx_)),"Tensor size overflow");
        count=emit(std::make_unique<BinaryInst>(Opcode::Mul,count,dims[i]));
    }
    return count;
}

void IRBuilder::require_tensor_shape(const std::vector<Value*>& left, const std::vector<Value*>& right) {
    if (left.size()!=right.size()) report_error("Tensor rank mismatch");
    for (size_t i=0;i<left.size();++i) {
        auto* a=dynamic_cast<ConstantInt*>(left[i]);auto* b=dynamic_cast<ConstantInt*>(right[i]);
        if (a && b) {if(a->value()!=b->value()) report_error("Tensor shape mismatch");}
        else if (left[i]!=right[i]) require_tensor_condition(emit(std::make_unique<ICmpInst>(ICmpPred::EQ,left[i],right[i],ctx_)), "Tensor shape mismatch");
    }
}

IRBuilder::TensorValue IRBuilder::tensor_symbol(const Symbol& symbol) {
    if (!symbol.is_array || symbol.dims.empty()) report_error("Expected a Tensor or array value");
    if (symbol.is_parameter && !symbol.is_tensor)
        report_error("Whole Tensor operations require a Tensor parameter with shape metadata");
    TensorValue value{symbol.ir_val,symbol.type,{},nullptr};
    if (!symbol.runtime_dims.empty()) value.dims=symbol.runtime_dims;
    else for (int dimension : symbol.dims) value.dims.push_back(ConstantInt::get(ctx_,dimension));
    if (symbol.is_parameter) value.data=emit(std::make_unique<LoadInst>(value.data));
    // Flatten typed array pointers without changing the logical shape.
    auto* element=get_ir_type(symbol.type);
    value.data=coerce(value.data,ctx_.get_pointer_type(element));
    return value;
}

Value* IRBuilder::get_tensor_base_ptr(const Symbol& symbol) {return tensor_symbol(symbol).data;}

IRBuilder::TensorValue IRBuilder::tensor_lval(const LValAST& lval) {
    auto* symbol=sym_table_.lookup(lval.name);
    if (!symbol) report_error("Undeclared identifier: "+lval.name,lval.line,lval.col);
    if (lval.indices.size()>symbol->dims.size()) report_error("Too many Tensor indices",lval.line,lval.col);
    auto value=tensor_symbol(*symbol);
    Value* offset=ConstantInt::get(ctx_,0);
    for (size_t i=0;i<lval.indices.size();++i) {
        Value* index=build_expr(lval.indices[i].get());
        if (index->type()!=ctx_.get_i32_type()) report_error("Tensor indices must be integers",lval.line,lval.col);
        for (size_t j=i+1;j<value.dims.size();++j) index=emit(std::make_unique<BinaryInst>(Opcode::Mul,index,value.dims[j]));
        offset=emit(std::make_unique<BinaryInst>(Opcode::Add,offset,index));
    }
    if (!lval.indices.empty()) value.data=emit(std::make_unique<GetElementPtrInst>(value.data,std::vector<Value*>{offset},ctx_));
    value.dims.erase(value.dims.begin(),value.dims.begin()+lval.indices.size());
    return value;
}

IRBuilder::TensorValue IRBuilder::allocate_tensor(const std::vector<Value*>& dims, DataType type) {
    if (dims.empty()) report_error("Tensor value requires a shape");
    Value* count=tensor_element_count(dims);
    auto* bytes=emit(std::make_unique<BinaryInst>(Opcode::Mul,count,ConstantInt::get(ctx_,4)));
    bytes=emit(std::make_unique<BinaryInst>(Opcode::Add,bytes,ConstantInt::get(ctx_,4*static_cast<int>(dims.size()+1))));
    auto* owner=emit(std::make_unique<CallInst>(module_.get_function(".sysy.runtime.malloc"),std::vector<Value*>{bytes}));
    require_tensor_condition(emit(std::make_unique<ICmpInst>(ICmpPred::NE,owner,ConstantZero::get(owner->type()),ctx_)), "Tensor allocation failed");
    tensor_allocations_.push_back(owner);
    emit(std::make_unique<StoreInst>(ConstantInt::get(ctx_,static_cast<int>(dims.size())),owner,ctx_));
    for (size_t i=0;i<dims.size();++i) {
        auto* slot=emit(std::make_unique<GetElementPtrInst>(owner,std::vector<Value*>{ConstantInt::get(ctx_,static_cast<int>(i+1))},ctx_));
        emit(std::make_unique<StoreInst>(dims[i],slot,ctx_));
    }
    Value* data=emit(std::make_unique<GetElementPtrInst>(owner,std::vector<Value*>{ConstantInt::get(ctx_,static_cast<int>(dims.size()+1))},ctx_));
    data=coerce(data,ctx_.get_pointer_type(get_ir_type(type)));
    return TensorValue{data,type,dims,owner};
}

void IRBuilder::release_tensor_allocations(Value* keep) {
    for (auto* owner : tensor_allocations_) if (owner!=keep)
        emit(std::make_unique<CallInst>(module_.get_function(".sysy.runtime.free"),std::vector<Value*>{owner}));
    tensor_allocations_.clear(); // keep, if any, transfers to the caller.
}

IRBuilder::TensorValue IRBuilder::tensor_value(const ExprAST* expression) {
    DiagnosticScope diagnostic(*this,expression);
    if (auto* lval=dynamic_cast<const LValAST*>(expression)) {
        auto* symbol=sym_table_.lookup(lval->name);
        if (symbol && symbol->is_array && lval->indices.size()<symbol->dims.size()) return tensor_lval(*lval);
    }
    if (auto* call=dynamic_cast<const CallExprAST*>(expression); call && is_tensor_expr(call)) {
        Value* owner=build_call_expr(*call);
        size_t rank=tensor_return_rank(call->callee);
        std::vector<Value*> dims;
        for(size_t i=0;i<rank;++i) {
            auto* slot=emit(std::make_unique<GetElementPtrInst>(owner,std::vector<Value*>{ConstantInt::get(ctx_,static_cast<int>(i+1))},ctx_));
            dims.push_back(emit(std::make_unique<LoadInst>(slot)));
        }
        auto type=function_definitions_.at(call->callee)->return_type;
        Value* data=emit(std::make_unique<GetElementPtrInst>(owner,std::vector<Value*>{ConstantInt::get(ctx_,static_cast<int>(rank+1))},ctx_));
        data=coerce(data,ctx_.get_pointer_type(get_ir_type(type)));
        return TensorValue{data,type,dims,owner};
    }
    if (auto* binary=dynamic_cast<const BinaryExprAST*>(expression);binary && is_tensor_expr(binary)) {
        auto left=tensor_value(binary->lhs.get());
        auto right=tensor_value(binary->rhs.get());
        auto type=left.element_type==DataType::Float || right.element_type==DataType::Float ? DataType::Float : DataType::Int;
        if(binary->op==BinaryOp::MatMul) {
            if(left.dims.size()!=2 || right.dims.size()!=2) report_error("Matrix multiplication requires rank-2 operands");
            require_tensor_shape({left.dims[1]},{right.dims[0]});
            auto result=allocate_tensor({left.dims[0],right.dims[1]},type);
            emit_tensor_matmul(result.data,left.data,right.data,left.dims[0],left.dims[1],right.dims[1],type);
            return result;
        }
        if(binary->op!=BinaryOp::Add && binary->op!=BinaryOp::Sub && binary->op!=BinaryOp::Mul && binary->op!=BinaryOp::Div && binary->op!=BinaryOp::Mod)
            report_error("Unsupported Tensor binary operator",expression->line,expression->col);
        if(binary->op==BinaryOp::Mod && type==DataType::Float) report_error("Floating-point Tensor remainder is invalid");
        if (!left.dims.empty() && !right.dims.empty()) require_tensor_shape(left.dims,right.dims);
        const auto& dims=left.dims.empty()?right.dims:left.dims;
        auto result=allocate_tensor(dims,type);
        emit_tensor_binary_loop(result.data,left.data,right.data,left.dims.empty(),right.dims.empty(),binary->op,tensor_element_count(dims),type);
        return result;
    }
    if (auto* unary=dynamic_cast<const UnaryExprAST*>(expression);unary && is_tensor_expr(unary)) {
        auto source=tensor_value(unary->expr.get());
        if(unary->op==UnaryOp::Plus) return source;
        auto type=unary->op==UnaryOp::Not?DataType::Int:source.element_type;
        auto result=allocate_tensor(source.dims,type);
        emit_tensor_unary_loop(result.data,source.data,unary->op,tensor_element_count(source.dims),type);
        return result;
    }
    auto* scalar=build_expr(expression);
    if(!scalar || (!scalar->type()->is_integer() && scalar->type()!=ctx_.get_float_type()))
        report_error("Expected numeric Tensor operand",expression->line,expression->col);
    return TensorValue{scalar,scalar->type()==ctx_.get_float_type()?DataType::Float:DataType::Int,{},nullptr};
}

Value* IRBuilder::build_tensor_expr(const ExprAST* expression, Value* destination, const Symbol& symbol) {
    auto source=tensor_value(expression);
    std::vector<Value*> dims=symbol.runtime_dims;
    if(dims.empty()) for(int dimension:symbol.dims) dims.push_back(ConstantInt::get(ctx_,dimension));
    if(!source.dims.empty()) require_tensor_shape(dims,source.dims);
    if(source.dims.empty()) emit_tensor_copy_loop(destination,source.data,tensor_element_count(dims),symbol.type,true);
    else emit_tensor_copy_loop(destination,source.data,tensor_element_count(dims),symbol.type);
    return destination;
}

Value* IRBuilder::build_call_expr(const CallExprAST& call) {
    DiagnosticScope diagnostic(*this,&call);
    if(call.callee=="starttime" || call.callee=="stoptime") {
        if(!call.args.empty()) report_error("Timing functions take no arguments",call.line,call.col);
        return emit(std::make_unique<CallInst>(module_.get_function("_sysy_"+call.callee),
            std::vector<Value*>{ConstantInt::get(ctx_,call.line)}));
    }
    Function* callee=module_.get_function(call.callee);
    if(!callee) report_error("Call to undeclared function: "+call.callee,call.line,call.col);
    auto definition=function_definitions_.find(call.callee);
    const auto& parameters=callee->function_type()->param_types();
    bool source_function=definition!=function_definitions_.end();
    size_t fixed=source_function?definition->second->params.size():parameters.size();
    bool variadic=callee->function_type()->is_variadic();
    if(call.args.size()<fixed || (!variadic && call.args.size()!=fixed)) report_error("Wrong number of arguments to "+call.callee,call.line,call.col);
    std::vector<Value*> arguments;
    size_t parameter_index=0;
    for(size_t i=0;i<call.args.size();++i) {
        const FuncFParamAST* source_parameter=source_function?definition->second->params[i].get():nullptr;
        if(source_parameter && source_parameter->is_tensor) {
            auto value=tensor_value(call.args[i].get());
            const auto& expected=parameter_shapes_.at(call.callee)[i];
            if(value.dims.size()!=expected.size() || value.element_type!=source_parameter->type)
                report_error("Incompatible Tensor argument in "+call.callee,call.line,call.col);
            for(size_t dim=1;dim<expected.size();++dim)
                require_tensor_shape({value.dims[dim]},{ConstantInt::get(ctx_,expected[dim])});
            arguments.push_back(coerce(value.data,parameters[parameter_index++]));
            for(auto* size:value.dims) {arguments.push_back(size);++parameter_index;}
            continue;
        }
        Value* value=nullptr;
        if(is_tensor_expr(call.args[i].get())) {
            auto tensor=tensor_value(call.args[i].get());
            if(source_parameter && source_parameter->is_array) {
                const auto& expected=parameter_shapes_.at(call.callee)[i];
                if(tensor.dims.size()!=expected.size() || tensor.element_type!=source_parameter->type)
                    report_error("Incompatible array argument in "+call.callee,call.line,call.col);
                for(size_t dim=1;dim<expected.size();++dim)
                    require_tensor_shape({tensor.dims[dim]},{ConstantInt::get(ctx_,expected[dim])});
                value=coerce(tensor.data,parameters[parameter_index]);
            } else value=tensor.data;
        } else value=build_expr(call.args[i].get());
        if(i<fixed) {
            auto* expected=parameters[parameter_index++];
            if(value->type()->is_pointer()!=expected->is_pointer() || (value->type()->is_pointer() && value->type()!=expected))
                report_error("Incompatible argument type in "+call.callee,call.line,call.col);
            value=coerce(value,expected);
        } else {
            if(value->type()==ctx_.get_float_type()) value=coerce(value,ctx_.get_double_type());
            else if(!value->type()->is_integer()) report_error("Invalid variadic argument",call.line,call.col);
        }
        arguments.push_back(value);
    }
    auto* result=emit(std::make_unique<CallInst>(callee,arguments));
    if(source_function && definition->second->is_tensor_return) tensor_allocations_.push_back(result);
    return result;
}

} // namespace sysy
