#pragma once

#include <memory>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <stack>
#include "../ast/ast.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"

class CodeGen : public ASTVisitor {
public:
    CodeGen();
    ~CodeGen();

    // Generate code — fills internal module, returns raw pointer (null on failure)
    llvm::Module* generateCode(std::shared_ptr<Program> program);

    // Get the generated LLVM module (non-owning)
    llvm::Module* getModule() const { return module.get(); }

    // Optional target triple override (empty = native)
    std::string targetTriple;
    // Freestanding mode: alloc/free use esk_alloc/esk_free instead of malloc/free
    bool freestanding = false;

    // Print LLVM IR to stdout
    void printIR() const;

    // Emit native object file
    bool emitObjectFile(const std::string& filename);

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // Symbol table: maps variable/function names to LLVM Values
    std::map<std::string, llvm::Value*> symbolTable;
    std::vector<std::map<std::string, llvm::Value*>> scopeStack;

    // Current function being compiled
    llvm::Function* currentFunction = nullptr;

    // Concrete struct registry
    std::map<std::string, llvm::StructType*> structTypes;
    std::map<std::string, std::vector<StructDecl::Field>> structFields;

    // Bitfield layout: for structs that contain at least one bitfield, every
    // field maps to a physical slot in the packed LLVM struct.
    struct BitfieldSlot {
        bool isBitfield = false;
        unsigned physIndex = 0;     // index into the physical LLVM struct
        unsigned bitOffset = 0;     // bit position within the storage word
        unsigned bitWidth  = 0;     // bitfield width
        llvm::Type* storageType = nullptr;  // physical slot type
        bool isSigned = false;
    };
    std::map<std::string, std::map<std::string, BitfieldSlot>> structLayout;
    std::string structBaseTypeOf(const ExprPtr& base);  // resolve a member base to a struct name
    void storeBitfield(MemberExpr* m, llvm::Value* val); // read-modify-write a bitfield
    // Masked read-modify-write of a bitfield given the storage-word pointer.
    void storeBitfieldInto(llvm::Value* wordPtr, const BitfieldSlot& slot, llvm::Value* val);

    // Template struct registry
    std::map<std::string, StructDecl*> templateDecls;
    // Reverse map: mangled instance name -> (template name, concrete type args).
    // Lets us recover that `List_int` is `List` instantiated with [int] when
    // inferring a type parameter from a `List<T>*` argument.
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> templateInstanceArgs;
    // Structural unification: bind type params in `pattern` (e.g. List<T>*) from
    // a concrete argument type (e.g. *List_int), filling `subs`.
    void unifyTypeParam(std::string pattern, std::string concrete,
                        const std::set<std::string>& tps,
                        std::map<std::string, std::string>& subs);

    // Interface registry: name → vtable type + method order
    std::map<std::string, llvm::StructType*> ifaceVtableTypes;
    std::map<std::string, std::vector<std::string>> ifaceMethodOrder;
    // Fat pointer type per interface: %I = type { ptr, ptr }
    std::map<std::string, llvm::StructType*> ifaceFatPtrTypes;

    // Eskiu param types per function — for interface boxing at call sites
    std::map<std::string, std::vector<std::string>> funcEskiuParamTypes;
    // Eskiu return type per function — lets getExprEskiuType resolve the static
    // type of a call result (so member access on a temporary works).
    std::map<std::string, std::string> funcEskiuReturnType;

    // Interface method return types — indexed by [ifaceName][methodIndex]
    std::map<std::string, std::vector<std::string>> ifaceMethodReturnTypes;
    // Interface method param Eskiu types (excluding self) — [ifaceName][methodIndex]
    std::map<std::string, std::vector<std::vector<std::string>>> ifaceMethodParamEskiuTypes;

    // Global-scope variable type tracking (complement to varTypeStack which is function-scoped)
    std::map<std::string, std::string> globalVarTypes;

    // Evaluate an expression as an LLVM Constant (for global variable initializers).
    // Returns nullptr for expressions that cannot be folded to a constant.
    llvm::Constant* evaluateConstantExpr(const ExprPtr& expr);

    // Helpers
    llvm::Value* boxAsInterface(const std::string& ifaceName,
                                const std::string& structName,
                                llvm::Value* structPtr);
    // Wrap a top-level function in a {fn_ptr, env_ptr} closure value so a bare
    // function name can be passed where a fn(...)->R is expected. The synthesized
    // thunk ignores env and forwards to the target; cached per function.
    llvm::Value* makeFunctionPointer(llvm::Function* target);
    void ensureTemplateInstantiated(const std::string& mangledName,
                                    const std::string& templateName,
                                    const std::vector<std::string>& args);
    // Template function registry
    std::map<std::string, FunctionDecl*> funcTemplateDecls;
    // Active type param substitutions during template function instantiation
    std::map<std::string, std::string> typeParamOverride;

    // Volatile variable tracking — names of variables declared volatile
    std::set<std::string> volatileVars;

    // Variable type tracking for MemberExpr/IndexExpr resolution
    std::vector<std::map<std::string, std::string>> varTypeStack;
    void defineVarType(const std::string& name, const std::string& type);
    std::string lookupVarType(const std::string& name) const;

    // Break/continue targets for the innermost loop
    llvm::BasicBlock* breakTarget    = nullptr;
    llvm::BasicBlock* continueTarget = nullptr;

    // Exception handling: set when inside a try body
    llvm::BasicBlock* unwindTarget = nullptr;

    // Helper: creates call or invoke depending on whether we are in a try body.
    // When in a try body, returns the invoke result and advances the insert point
    // to a fresh "normal continuation" block.
    llvm::Value* createMaybeInvoke(llvm::FunctionType* fty, llvm::Value* callee,
                                    llvm::ArrayRef<llvm::Value*> args,
                                    const llvm::Twine& name = "");

    // sret (structure return) support for large struct returns
    // Maps function name → actual return struct type (the LLVM function itself returns void)
    std::map<std::string, llvm::StructType*> funcSretTypes;
    // Active sret pointer for the current function (null if not sret)
    llvm::Value* currentSretParam = nullptr;

    // Returns true if retType is an aggregate that must use sret on this target
    bool needsSret(llvm::Type* retType) const;

    // Type system: map Eskiu types to LLVM types
    llvm::Type* getTypeFromString(const std::string& typeStr);
    bool isPointerType(const std::string& typeStr) const;
    bool isIntType(const std::string& typeStr) const;
    bool isFloatType(const std::string& typeStr) const;

    // Resolve the Eskiu type string of an expression (for struct/array access)
    std::string getExprEskiuType(const ExprPtr& expr) const;

    // Expand a type alias to its underlying type string (peels pointers), so
    // downstream logic sees e.g. "*uint8" instead of an alias name like "Bytes".
    std::string expandAlias(const std::string& t) const;

    // Helper methods
    void pushScope();
    void popScope();
    llvm::Value* lookupSymbol(const std::string& name);
    void defineSymbol(const std::string& name, llvm::Value* value);

    // Visitor methods
    void visit(Program* node) override;
    void visit(FunctionDecl* node) override;
    void visit(VarDecl* node) override;
    void visit(StructDecl* node) override;
    void visit(ExternDecl* node) override;
    void visit(IntrinsicDecl* node) override;
    llvm::Value* lowerIntrinsicCall(const std::string& fn, class CallExpr* node);
    void visit(BlockStmt* node) override;
    void visit(IfStmt* node) override;
    void visit(ForStmt* node) override;
    void visit(ForInStmt* node) override;
    void visit(WhileStmt* node) override;
    void visit(ReturnStmt* node) override;
    void visit(BreakStmt* node) override;
    void visit(ExprStmt* node) override;
    void visit(BinaryExpr* node) override;
    void visit(UnaryExpr* node) override;
    void visit(QuestionExpr* node) override;
    void visit(CallExpr* node) override;
    void visit(IndexExpr* node) override;
    void visit(MemberExpr* node) override;
    void visit(CastExpr* node) override;
    void visit(LiteralExpr* node) override;
    void visit(IdentExpr* node) override;
    void visit(InterfaceDecl* node) override;
    void visit(ContinueStmt* node) override;
    void visit(SwitchStmt* node) override;
    void visit(MatchStmt* node) override;
    void visit(StructInitExpr* node) override;
    void visit(AllocWithExpr* node) override;
    void visit(TemplateCallExpr* node) override;
    void visit(LambdaExpr* node) override;
    void visit(AsmStmt* node) override;
    void visit(UnionDecl* node) override;
    void visit(SizeofExpr* node) override;
    void visit(FreeClosureExpr* node) override;
    void visit(AwaitExpr* node) override;
    void visit(ThreadCreateExpr* node) override;

    // Union registry: name → fields (all share offset 0; stored as [N x i8])
    std::map<std::string, std::vector<StructDecl::Field>> unionFields;
    void visit(ThreadJoinStmt* node) override;
    void visit(ThrowStmt* node) override;
    void visit(TryStmt* node) override;
    void visit(EnumDecl* node) override;
    void visit(TypeAliasDecl* node) override;

    // `const` integer values, by name — so a const can be used as an array size.
    std::map<std::string, long long> constInts;
    // Resolve an array-dimension string (a decimal literal, an enum constant, or
    // a const int) to its value. Returns false if it cannot be resolved.
    bool resolveArrayDim(const std::string& dim, uint64_t& out) const;

    // Enum members -> integer value; the set of enum type names (each maps to i32)
    std::map<std::string, long long> enumConstants;
    std::set<std::string> enumTypes;
    // Algebraic enums: name -> decl (payloads) and variant -> (enum, tag). The
    // LLVM type lives in structTypes[enumName] as { i32 tag, [N x i64] payload }.
    std::set<std::string> adtEnums;
    std::map<std::string, EnumDecl*> adtEnumDecls;
    std::map<std::string, std::pair<std::string, int>> adtVariants;
    // Generic algebraic enums (Option<T>): template decl + variant->(enum,tag), and
    // per-instance (Option_int) -> (generic name, concrete type args) for resolution.
    std::map<std::string, EnumDecl*> genericEnumDecls;
    std::map<std::string, std::pair<std::string, int>> genericVariants;
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> enumInstanceArgs;
    // Build an algebraic-enum value for `variant`(args) (concrete enum; args may be empty).
    llvm::Value* buildVariant(const std::string& variant, const std::vector<ExprPtr>& args);
    // Core builder: { tag, payload } value with payload fields of `fieldTypes`.
    llvm::Value* buildEnumValue(llvm::StructType* et, int tag,
                                const std::vector<llvm::Type*>& fieldTypes,
                                const std::vector<ExprPtr>& args);
    // Monomorphize a generic enum for `typeArgs`; returns the mangled instance name
    // (and creates its struct type + records enumInstanceArgs on first use).
    std::string ensureEnumInst(const std::string& genericName,
                               const std::vector<std::string>& typeArgs);
    // Names declared `intrinsic` — calls to these lower to inline IR, not a call.
    std::set<std::string> intrinsicNames;
    // Type aliases: alias name -> underlying type string
    std::map<std::string, std::string> typeAliases;

    void emitStructInitInto(llvm::Value* dest, StructInitExpr* init);
    // Resolve a struct-initializer name to a concrete struct type name, instantiating
    // the template if the name is of the form Name<Arg,...> (e.g. Pair<int,float>).
    std::string resolveStructInitName(const std::string& name);
    llvm::Function* getOrDeclareFunc(const std::string& name, llvm::Type* retType,
                                     std::vector<llvm::Type*> paramTypes, bool isVarArg = false);

    // Declare an Eskiu function prototype (no body) and register its sret/param
    // metadata. Idempotent: returns the existing llvm::Function if already created.
    // Used by visit(Program)'s prototype pre-pass so functions may be called before
    // they are defined (forward references, mutual recursion).
    llvm::Function* declareFunction(
        const std::string& name, const std::string& returnType,
        const std::vector<std::pair<std::string, std::string>>& params);
    // Create the LLVM struct type shell for a (non-template) struct. Idempotent.
    void declareStructType(StructDecl* node);

    // Expression evaluation (returns LLVM Value)
    std::stack<llvm::Value*> exprValueStack;
    llvm::Value* evaluateExpr(const ExprPtr& expr);
    llvm::Value* evaluateLValue(const ExprPtr& expr);
};
