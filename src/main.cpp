#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/ir_builder.hpp"
#include "ir/module.hpp"
#include "ir/ir_printer.hpp"
#include "ir/ir_verifier.hpp"
#include "passes/mem2reg.hpp"
#include "passes/const_folding.hpp"
#include "passes/dce.hpp"
#include "passes/cfg_simplify.hpp"
#include "passes/phi_simplify.hpp"
#include "passes/cse.hpp"
#include "passes/phi_elim.hpp"
#include "backend/arm64_codegen.hpp"
#include "passes/middle_end.hpp"
#include <map>
#include <cctype>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <new>
#include <stdexcept>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input.sy> -S -o <output.s> [-O0|-O1] [--emit-hir|--emit-mir|--emit-lir] [--passes=a,b] [--trace-passes] [--opt-bisect-limit=N]\n";
}

static int run_compiler(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;
    std::string output_file;
    bool emit_asm = false;
    bool emit_ir = false;
    sysy::IRLevel dump_level=sysy::IRLevel::MIR;
    int bisect_limit=-1;bool trace_passes=false,assume_no_overflow=false;
    // Verification is always enforced; --verify-ir remains a compatible option.
    int opt_level = 0;
    std::string run_pass;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.empty()) {std::cerr << "Error: empty command-line argument\n";return 1;}
        if (arg == "-S") {
            emit_asm = true;
        } else if (arg == "-o") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: -o requires an argument\n";
                return 1;
            }
        } else if (arg == "-O0") {
            opt_level = 0;
        } else if (arg == "-O1" || arg == "-O2" || arg == "-O3") {
            opt_level = 1;
        } else if (arg == "-O") {
            if (i + 1 < argc && isdigit(argv[i + 1][0])) {
                opt_level = (std::stoi(argv[++i]) > 0) ? 1 : 0;
            } else {
                opt_level = 1;
            }
        } else if (arg.rfind("-O", 0) == 0) {
            opt_level = 1;
        } else if (arg == "--emit-ir" || arg=="--emit-mir" || arg=="--emit-hir" || arg=="--emit-lir") {
            emit_ir = true;
            dump_level=arg=="--emit-hir" ? sysy::IRLevel::HIR : arg=="--emit-lir" ? sysy::IRLevel::LIR : sysy::IRLevel::MIR;
        } else if(arg=="--assume-signed-no-overflow") {assume_no_overflow=true;
        } else if(arg=="--trace-passes") {trace_passes=true;
        } else if(arg.rfind("--opt-bisect-limit=",0)==0 || arg.rfind("-opt-bisect-limit=",0)==0) {
            auto value=arg.substr(arg.find('=')+1);size_t used=0;
            bisect_limit=std::stoi(value,&used);
            if(used!=value.size() || bisect_limit< -1) throw std::runtime_error("Invalid optimization bisect limit");
        } else if(arg.rfind("--passes=",0)==0) {run_pass=arg.substr(9);
        } else if (arg == "--verify-ir" || arg=="--verify-each") {
            // Already enabled unconditionally.
        } else if (arg.rfind("--run-pass=", 0) == 0) {
            run_pass = arg.substr(11);
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: no input file specified\n";
        return 1;
    }

    std::ifstream ifs(input_file);
    if (!ifs.is_open()) {
        std::cerr << "Error: could not open input file: " << input_file << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string source = buffer.str();

    // 1. Lexical and Syntax Analysis
    sysy::Lexer lexer(source);
    sysy::Parser parser(lexer);
    auto comp_unit = parser.parse_comp_unit();

    if (!lexer.error_message().empty()) {
        std::cerr << "Lexical error: " << lexer.error_message() << "\n";
        return 1;
    }
    if (parser.has_error()) {
        std::cerr << parser.error_message() << "\n";
        return 1;
    }

    // 2. Semantic Analysis & IR Generation
    sysy::Module module(input_file);
    sysy::IRBuilder ir_builder(module);
    ir_builder.set_assume_signed_no_overflow(assume_no_overflow);
    if (!ir_builder.build(*comp_unit)) {
        std::cerr << ir_builder.error_message() << "\n";
        return 1;
    }

    sysy::PassManager manager(bisect_limit,trace_passes ? &std::cerr : nullptr);
    manager.verify(module,"after frontend HIR construction");
    auto make_pass=[](const std::string& name)->std::unique_ptr<sysy::Pass> {
        if(name=="mem2reg")return std::make_unique<sysy::Mem2RegPass>();
        if(name=="const_folding")return std::make_unique<sysy::ConstFoldingPass>();
        if(name=="canonicalize")return std::make_unique<sysy::CanonicalizePass>();
        if(name=="phi_simplify")return std::make_unique<sysy::PhiSimplifyPass>();
        if(name=="cfg_simplify")return std::make_unique<sysy::CFGSimplifyPass>();
        if(name=="dce")return std::make_unique<sysy::DCEPass>();
        if(name=="cse")return std::make_unique<sysy::CSEPass>();
        if(name=="gvn")return std::make_unique<sysy::GVNPass>();
        if(name=="inline")return std::make_unique<sysy::InlinePass>();
        if(name=="licm")return std::make_unique<sysy::LICMPass>();
        if(name=="induction")return std::make_unique<sysy::InductionPass>();
        if(name=="unroll")return std::make_unique<sysy::UnrollPass>();
        if(name=="strength_reduce")return std::make_unique<sysy::StrengthReducePass>();
        throw std::runtime_error("Unknown pass: "+name);
    };
    auto run=[&](const std::string& name) {auto pass=make_pass(name);return manager.run_pass(*pass,module);};
    if(!emit_ir || dump_level!=sysy::IRLevel::HIR) {
        sysy::LowerHIRPass lower;manager.run_pass(lower,module);
        if(!run_pass.empty()) {
            std::istringstream sequence(run_pass);std::string name;
            while(std::getline(sequence,name,',')) run(name);
        } else if(opt_level>=1) {
            // Reduction phases strictly decrease the canonical rank. Expansion
            // phases have independent budgets and run at explicit boundaries.
            auto reduce=[&]() {
                for(;;) {
                    auto before=sysy::canonical_rank(module);
                    bool changed=run("canonicalize");
                    changed |= run("phi_simplify");changed |= run("cfg_simplify");
                    changed |= run("gvn");changed |= run("dce");
                    auto after=sysy::canonical_rank(module);
                    if(after<before) continue;
                    if(!changed) break;
                    throw std::runtime_error("Reduction pipeline failed strict rank descent");
                }
            };
            run("mem2reg");reduce();
            run("inline");run("mem2reg");reduce();
            run("licm");run("induction");run("unroll");run("strength_reduce");reduce();
        }
    }

    auto emit_output = [&](bool assembly) -> int {
        std::ofstream file;
        std::ostream* stream = &std::cout;
        if (!output_file.empty() && (assembly || !emit_asm)) {
            file.open(output_file);
            if (!file) { std::cerr << "Cannot open output file: " << output_file << "\n"; return 1; }
            stream = &file;
        }
        if (assembly) sysy::ARM64CodeGen(*stream).generate(module);
        else sysy::IRPrinter(*stream).print(module);
        stream->flush();
        if (!*stream) { std::cerr << "Cannot write output: " << output_file << "\n"; return 1; }
        if (file.is_open()) {
            file.close();
            if (file.fail()) { std::cerr << "Cannot close output: " << output_file << "\n"; return 1; }
        }
        return 0;
    };
    if(emit_ir && dump_level!=sysy::IRLevel::LIR) return emit_output(false);
    if(emit_asm || (emit_ir && dump_level==sysy::IRLevel::LIR)) {
        sysy::LowerLIRPass lower(opt_level>=1);manager.run_pass(lower,module);
    }
    if(emit_ir) return emit_output(false);
    return emit_asm ? emit_output(true) : 0;
}

int main(int argc, char* argv[]) {
    try { return run_compiler(argc, argv); }
    catch (const std::bad_alloc&) { std::cerr << "Error: compiler memory limit exceeded\n"; }
    catch (const std::exception& error) { std::cerr << "Error: " << error.what() << "\n"; }
    return 1;
}
