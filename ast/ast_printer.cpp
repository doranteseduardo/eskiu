#include "ast_printer.h"

const std::string ASTPrinter::INDENT_STR = "  ";

void ASTPrinter::indent() {
    for (int i = 0; i < indentLevel; i++) {
        std::cout << INDENT_STR;
    }
}

void ASTPrinter::println(const std::string& text) {
    indent();
    std::cout << text << std::endl;
}

void ASTPrinter::print(std::shared_ptr<Program> program) {
    indentLevel = 0;
    program->accept(this);
}

void ASTPrinter::visit(Program* node) {
    println("Program");
    indentLevel++;
    for (auto& decl : node->declarations) {
        decl->accept(this);
    }
    indentLevel--;
}

void ASTPrinter::printTypeParams(const std::vector<std::string>& typeParams,
                                 const std::map<std::string, std::vector<std::string>>& constraints) {
    if (typeParams.empty()) return;
    println("TypeParams:");
    indentLevel++;
    for (const auto& tp : typeParams) {
        auto it = constraints.find(tp);
        if (it != constraints.end() && !it->second.empty()) {
            std::string s = tp + ": ";
            for (size_t i = 0; i < it->second.size(); ++i) {
                if (i) s += " + ";
                s += it->second[i];
            }
            println(s);
        } else {
            println(tp);
        }
    }
    indentLevel--;
}

void ASTPrinter::visit(FunctionDecl* node) {
    println("FunctionDecl: " + node->name + " -> " + node->returnType +
            (node->isAsync ? " (async)" : ""));
    indentLevel++;

    printTypeParams(node->typeParams, node->constraints);
    println("Parameters:");
    indentLevel++;
    for (auto& param : node->params) {
        println(param.first + " " + param.second);
    }
    indentLevel--;

    // A forward declaration (prototype) has no body — omit the section rather than
    // dereferencing a null body (cf. ReturnStmt skipping a null value).
    if (node->body) {
        println("Body:");
        indentLevel++;
        node->body->accept(this);
        indentLevel--;
    }

    indentLevel--;
}

void ASTPrinter::visit(VarDecl* node) {
    println("VarDecl: " + node->name + ": " + node->type);
    indentLevel++;
    if (node->initializer) {
        println("Initializer:");
        indentLevel++;
        node->initializer->accept(this);
        indentLevel--;
    }
    indentLevel--;
}

void ASTPrinter::visit(StructDecl* node) {
    println("StructDecl: " + node->name);
    indentLevel++;

    printTypeParams(node->typeParams, node->constraints);
    println("Fields:");
    indentLevel++;
    for (auto& field : node->fields) {
        std::string line = field.type + " " + field.name;
        if (field.bitWidth > 0) line += " : " + std::to_string(field.bitWidth);
        println(line);
    }
    indentLevel--;

    if (!node->methods.empty()) {
        println("Methods:");
        indentLevel++;
        for (auto& m : node->methods) m->accept(this);
        indentLevel--;
    }

    indentLevel--;
}

void ASTPrinter::visit(ExternDecl* node) {
    println("ExternDecl: " + node->name + " -> " + node->returnType);
    indentLevel++;

    println("Parameters:");
    indentLevel++;
    for (auto& param : node->params) {
        println(param.first + " " + param.second);
    }
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(IntrinsicDecl* node) {
    println("IntrinsicDecl: " + node->name + " -> " + node->returnType);
    indentLevel++;

    println("Parameters:");
    indentLevel++;
    for (auto& param : node->params) {
        println(param.first + " " + param.second);
    }
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(BlockStmt* node) {
    println("BlockStmt");
    indentLevel++;

    for (auto& item : node->items) {
        if (std::holds_alternative<DeclPtr>(item)) {
            std::get<DeclPtr>(item)->accept(this);
        } else if (std::holds_alternative<StmtPtr>(item)) {
            std::get<StmtPtr>(item)->accept(this);
        }
    }

    indentLevel--;
}

void ASTPrinter::visit(IfStmt* node) {
    println("IfStmt");
    indentLevel++;

    println("Condition:");
    indentLevel++;
    node->condition->accept(this);
    indentLevel--;

    println("Then:");
    indentLevel++;
    node->thenBranch->accept(this);
    indentLevel--;

    if (node->elseBranch) {
        println("Else:");
        indentLevel++;
        node->elseBranch->accept(this);
        indentLevel--;
    }

    indentLevel--;
}

void ASTPrinter::visit(ForStmt* node) {
    println("ForStmt");
    indentLevel++;

    if (node->init) {
        println("Init:");
        indentLevel++;
        node->init->accept(this);
        indentLevel--;
    }

    if (node->condition) {
        println("Condition:");
        indentLevel++;
        node->condition->accept(this);
        indentLevel--;
    }

    if (node->step) {
        println("Step:");
        indentLevel++;
        node->step->accept(this);
        indentLevel--;
    }

    println("Body:");
    indentLevel++;
    node->body->accept(this);
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(ForInStmt* node) {
    println("ForInStmt (" + node->varName + " in ...)");
    indentLevel++;
    println("Iterable:");
    indentLevel++;
    node->iterable->accept(this);
    indentLevel--;
    println("Body:");
    indentLevel++;
    node->body->accept(this);
    indentLevel--;
    indentLevel--;
}

void ASTPrinter::visit(WhileStmt* node) {
    println("WhileStmt");
    indentLevel++;

    println("Condition:");
    indentLevel++;
    node->condition->accept(this);
    indentLevel--;

    println("Body:");
    indentLevel++;
    node->body->accept(this);
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(DoWhileStmt* node) {
    println("DoWhileStmt");
    indentLevel++;
    println("Body:");
    indentLevel++;
    node->body->accept(this);
    indentLevel--;
    println("Condition:");
    indentLevel++;
    node->condition->accept(this);
    indentLevel--;
    indentLevel--;
}

void ASTPrinter::visit(ReturnStmt* node) {
    println("ReturnStmt");
    if (node->value) {
        indentLevel++;
        node->value->accept(this);
        indentLevel--;
    }
}

void ASTPrinter::visit(BreakStmt* node) {
    println("BreakStmt");
}

void ASTPrinter::visit(ExprStmt* node) {
    println("ExprStmt");
    indentLevel++;
    node->expr->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(BinaryExpr* node) {
    println("BinaryExpr: " + node->op);
    indentLevel++;

    println("Left:");
    indentLevel++;
    node->left->accept(this);
    indentLevel--;

    println("Right:");
    indentLevel++;
    node->right->accept(this);
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(UnaryExpr* node) {
    println("UnaryExpr: " + node->op);
    indentLevel++;
    node->operand->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(IncDecExpr* node) {
    std::string tag = std::string(node->prefix ? "pre" : "post") + (node->decrement ? "--" : "++");
    println("IncDecExpr: " + tag);
    indentLevel++;
    node->operand->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(QuestionExpr* node) {
    println("QuestionExpr (?)");
    indentLevel++;
    node->operand->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(TernaryExpr* node) {
    println("TernaryExpr (?:)");
    indentLevel++;
    node->condition->accept(this);
    node->thenExpr->accept(this);
    node->elseExpr->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(CallExpr* node) {
    println("CallExpr");
    indentLevel++;

    println("Callee:");
    indentLevel++;
    node->callee->accept(this);
    indentLevel--;

    println("Args:");
    indentLevel++;
    for (auto& arg : node->args) {
        arg->accept(this);
    }
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(IndexExpr* node) {
    println("IndexExpr");
    indentLevel++;

    println("Base:");
    indentLevel++;
    node->base->accept(this);
    indentLevel--;

    println("Index:");
    indentLevel++;
    node->index->accept(this);
    indentLevel--;

    indentLevel--;
}

void ASTPrinter::visit(MemberExpr* node) {
    println("MemberExpr: ." + node->member);
    indentLevel++;
    node->base->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(CastExpr* node) {
    println("CastExpr: (" + node->targetType + ")");
    indentLevel++;
    node->expr->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(LiteralExpr* node) {
    std::string kind;
    switch (node->kind) {
        case LiteralExpr::Kind::INT: kind = "INT"; break;
        case LiteralExpr::Kind::FLOAT: kind = "FLOAT"; break;
        case LiteralExpr::Kind::STRING: kind = "STRING"; break;
        case LiteralExpr::Kind::CHAR: kind = "CHAR"; break;
        case LiteralExpr::Kind::BOOL: kind = "BOOL"; break;
        case LiteralExpr::Kind::NULL_VAL: kind = "NULL"; break;
    }
    println("LiteralExpr(" + kind + "): " + node->value);
}

void ASTPrinter::visit(IdentExpr* node) {
    println("IdentExpr: " + node->name);
}

void ASTPrinter::visit(InterfaceDecl* node) {
    println("InterfaceDecl: " + node->name);
    indentLevel++;
    for (const auto& m : node->methods) {
        std::string sig = m.returnType + " " + m.name + "(";
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i) sig += ", ";
            sig += m.params[i].first;
        }
        sig += ")";
        println(sig);
    }
    indentLevel--;
}

void ASTPrinter::visit(ContinueStmt* node) { println("ContinueStmt"); }

void ASTPrinter::visit(MatchStmt* node) {
    println("MatchStmt");
    indentLevel++;
    println("Subject:");
    indentLevel++;
    node->subject->accept(this);
    indentLevel--;
    for (auto& arm : node->arms) {
        std::string label = arm.variant.empty() ? "_" : arm.variant;
        if (!arm.bindings.empty()) {
            label += "(";
            for (size_t i = 0; i < arm.bindings.size(); ++i) { if (i) label += ", "; label += arm.bindings[i]; }
            label += ")";
        }
        println("Arm " + label + ":");
        indentLevel++;
        if (arm.body) arm.body->accept(this);
        indentLevel--;
    }
    indentLevel--;
}

void ASTPrinter::visit(SwitchStmt* node) {
    println("SwitchStmt");
    indentLevel++;
    println("Subject:");
    indentLevel++;
    node->subject->accept(this);
    indentLevel--;
    for (auto& c : node->cases) {
        if (c.value) {
            println("Case:");
            indentLevel++;
            c.value->accept(this);
        } else {
            println("Default:");
            indentLevel++;
        }
        for (auto& s : c.stmts) s->accept(this);
        indentLevel--;
    }
    indentLevel--;
}

void ASTPrinter::visit(TemplateCallExpr* node) {
    std::string sig = node->templateName + "<";
    for (size_t i = 0; i < node->typeArgs.size(); ++i) {
        if (i) sig += ",";
        sig += node->typeArgs[i];
    }
    sig += ">";
    println("TemplateCallExpr: " + sig);
    indentLevel++;
    for (auto& a : node->args) a->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(AllocWithExpr* node) {
    println("AllocWithExpr: alloc_with(<allocator>, " + node->elemType + ", ...)");
    indentLevel++;
    node->allocator->accept(this);
    node->count->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(StructInitExpr* node) {
    println("StructInitExpr: " + node->structName);
    indentLevel++;
    for (const auto& [name, expr] : node->fieldInits) {
        if (!name.empty()) println("." + name + ":");
        indentLevel++;
        expr->accept(this);
        indentLevel--;
    }
    indentLevel--;
}

void ASTPrinter::visit(ArrayLitExpr* node) {
    println("ArrayLitExpr");
    indentLevel++;
    for (const auto& el : node->elements) el->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(LambdaExpr* node) {
    std::string sig = node->returnType + "(";
    for (size_t i = 0; i < node->params.size(); ++i) {
        if (i) sig += ", ";
        sig += node->params[i].first + " " + node->params[i].second;
    }
    sig += ")";
    println("LambdaExpr: " + sig);
    if (node->body) { indentLevel++; node->body->accept(this); indentLevel--; }
}

void ASTPrinter::visit(AsmStmt* node) {
    println("AsmStmt: asm(\"" + node->asmString + "\")");
    if (!node->inputs.empty()) {
        indentLevel++;
        for (const auto& [c, e] : node->inputs) {
            println("input: \"" + c + "\"");
            indentLevel++; e->accept(this); indentLevel--;
        }
        indentLevel--;
    }
}

void ASTPrinter::visit(ThreadCreateExpr* node) {
    println("ThreadCreateExpr");
    indentLevel++; node->worker->accept(this); indentLevel--;
}

void ASTPrinter::visit(ThreadJoinStmt* node) {
    println("ThreadJoinStmt");
    indentLevel++; node->tid->accept(this); indentLevel--;
}

void ASTPrinter::visit(ThrowStmt* node) {
    println("ThrowStmt");
    if (node->value) { indentLevel++; node->value->accept(this); indentLevel--; }
}

void ASTPrinter::visit(TryStmt* node) {
    println("TryStmt");
    indentLevel++;
    if (node->body) node->body->accept(this);
    for (const auto& c : node->catches) {
        println("CatchClause: " + c.type + " " + c.name);
        indentLevel++;
        if (c.body) c.body->accept(this);
        indentLevel--;
    }
    if (node->finally) { println("Finally"); indentLevel++; node->finally->accept(this); indentLevel--; }
    indentLevel--;
}

void ASTPrinter::visit(DeferStmt* node) {
    println(node->isErr ? "DeferStmt (err)" : "DeferStmt");
    indentLevel++;
    if (node->body) node->body->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(SizeofExpr* node) {
    println("SizeofExpr: sizeof(" + node->typeName + ")");
}

void ASTPrinter::visit(FreeClosureExpr* node) {
    println("FreeClosureExpr:");
    indentLevel++;
    if (node->closure) node->closure->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(AwaitExpr* node) {
    println("AwaitExpr:");
    indentLevel++;
    if (node->operand) node->operand->accept(this);
    indentLevel--;
}

void ASTPrinter::visit(UnionDecl* node) {
    println("UnionDecl: " + node->name);
    indentLevel++;
    for (const auto& f : node->fields)
        println(f.type + " " + f.name);
    indentLevel--;
}

void ASTPrinter::visit(EnumDecl* node) {
    println("EnumDecl: " + node->name);
    indentLevel++;
    printTypeParams(node->typeParams, {});   // EnumDecl has no constraints field
    for (size_t i = 0; i < node->members.size(); ++i) {
        std::string line = node->members[i].first + " = " + std::to_string(node->members[i].second);
        if (i < node->payloads.size() && !node->payloads[i].empty()) {
            line += "(";
            for (size_t j = 0; j < node->payloads[i].size(); ++j) {
                if (j) line += ", ";
                line += node->payloads[i][j];
            }
            line += ")";
        }
        println(line);
    }
    indentLevel--;
}

void ASTPrinter::visit(TypeAliasDecl* node) {
    println("TypeAlias: " + node->name + " = " + node->aliased);
}
