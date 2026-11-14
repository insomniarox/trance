#include "expr.h"

#include <cmath>

namespace expr {

namespace {

enum Op : uint8_t {
    OpConst, OpLoad, OpStore, OpResult,
    OpNeg, OpNot,
    OpAdd, OpSub, OpMul, OpDiv, OpMod, OpPow,
    OpLt, OpGt, OpLe, OpGe, OpEq, OpNe, OpAnd, OpOr,
    OpSelect, OpCall
};

enum Fn {
    FnSin, FnCos, FnTan, FnAsin, FnAcos, FnAtan, FnAtan2,
    FnAbs, FnSign, FnFloor, FnCeil, FnFract, FnInt, FnSqrt, FnSqr, FnExp, FnLog, FnPow,
    FnMin, FnMax, FnMod, FnClamp, FnMix, FnStep, FnSmoothstep,
    FnIf, FnAbove, FnBelow, FnEqual, FnBand, FnBor, FnBnot,
    FnRand, FnNoise, FnLength, FnTri
};

struct FnInfo {
    const char *name;
    int minArgs, maxArgs;
    Fn id;
    const char *glsl; // %1..%3 are the arguments
};

const FnInfo functions[] = {
    {"sin", 1, 1, FnSin, "sin(%1)"},
    {"cos", 1, 1, FnCos, "cos(%1)"},
    {"tan", 1, 1, FnTan, "tan(%1)"},
    {"asin", 1, 1, FnAsin, "asin(clamp(%1,-1.0,1.0))"},
    {"acos", 1, 1, FnAcos, "acos(clamp(%1,-1.0,1.0))"},
    {"atan", 1, 2, FnAtan, "atan(%1)"},
    {"atan2", 2, 2, FnAtan2, "f_atan2(%1,%2)"},
    {"abs", 1, 1, FnAbs, "abs(%1)"},
    {"sign", 1, 1, FnSign, "sign(%1)"},
    {"floor", 1, 1, FnFloor, "floor(%1)"},
    {"ceil", 1, 1, FnCeil, "ceil(%1)"},
    {"fract", 1, 1, FnFract, "fract(%1)"},
    {"int", 1, 1, FnInt, "trunc(%1)"},
    {"sqrt", 1, 1, FnSqrt, "sqrt(abs(%1))"},
    {"sqr", 1, 1, FnSqr, "f_sqr(%1)"},
    {"exp", 1, 1, FnExp, "exp(min(%1,60.0))"},
    {"log", 1, 1, FnLog, "f_log(%1)"},
    {"pow", 2, 2, FnPow, "f_pow(%1,%2)"},
    {"min", 2, 2, FnMin, "min(%1,%2)"},
    {"max", 2, 2, FnMax, "max(%1,%2)"},
    {"mod", 2, 2, FnMod, "f_mod(%1,%2)"},
    {"clamp", 3, 3, FnClamp, "f_clamp(%1,%2,%3)"},
    {"mix", 3, 3, FnMix, "mix(%1,%2,%3)"},
    {"step", 2, 2, FnStep, "step(%1,%2)"},
    {"smoothstep", 3, 3, FnSmoothstep, "f_smoothstep(%1,%2,%3)"},
    {"if", 3, 3, FnIf, "((%1)!=0.0?(%2):(%3))"},
    {"above", 2, 2, FnAbove, "float((%1)>(%2))"},
    {"below", 2, 2, FnBelow, "float((%1)<(%2))"},
    {"equal", 2, 2, FnEqual, "float((%1)==(%2))"},
    {"band", 2, 2, FnBand, "float((%1)!=0.0&&(%2)!=0.0)"},
    {"bor", 2, 2, FnBor, "float((%1)!=0.0||(%2)!=0.0)"},
    {"bnot", 1, 1, FnBnot, "float((%1)==0.0)"},
    {"rand", 0, 1, FnRand, ""},
    {"noise", 1, 2, FnNoise, ""},
    {"length", 2, 2, FnLength, "length(vec2(%1,%2))"},
    {"tri", 1, 1, FnTri, "f_tri(%1)"},
};

} // namespace

QStringList functionNames() {
    QStringList names;
    for (const auto &fn : functions)
        names << QString::fromLatin1(fn.name);
    return names;
}

namespace {

const FnInfo *findFunction(const QString &name) {
    for (const auto &fn : functions)
        if (name == QLatin1String(fn.name))
            return &fn;
    return nullptr;
}

double random01() {
    static thread_local uint64_t state = 0x9E3779B97F4A7C15ull;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return double(state >> 11) / double(1ull << 53);
}

// The shader's f_hash, step for step and in the same single precision, so
// that noise() in pixel code gives the CPU (which works out where particles
// and motion vectors are carried) the same field the picture flows along.
float latticeHash(float x, float y) {
    const auto fract = [](float v) { return v - std::floor(v); };
    float qx = fract(x * 0.1031f), qy = fract(y * 0.1031f), qz = fract(x * 0.1031f);
    const float d = qx * (qy + 33.33f) + qy * (qz + 33.33f) + qz * (qx + 33.33f);
    qx += d;
    qy += d;
    qz += d;
    return fract((qx + qy) * qz);
}

// Division by zero is zero rather than infinity, so one bad frame of audio
// can't poison a feedback buffer with NaNs.
double safeDiv(double a, double b) { return std::abs(b) < 1e-9 ? 0.0 : a / b; }
double safeMod(double a, double b) { return std::abs(b) < 1e-9 ? 0.0 : a - b * std::floor(a / b); }
double safePow(double a, double b) {
    const double r = std::pow(std::abs(a), b);
    return (a < 0 && std::fmod(std::abs(b), 2.0) == 1.0) ? -r : r;
}
double finite(double v) { return std::isfinite(v) ? v : 0.0; }

double callFunction(int fn, int argc, const double *a) {
    switch (fn) {
    case FnSin: return std::sin(a[0]);
    case FnCos: return std::cos(a[0]);
    case FnTan: return std::tan(a[0]);
    case FnAsin: return std::asin(std::min(1.0, std::max(-1.0, a[0])));
    case FnAcos: return std::acos(std::min(1.0, std::max(-1.0, a[0])));
    case FnAtan: return argc == 2 ? std::atan2(a[0], a[1]) : std::atan(a[0]);
    case FnAtan2: return std::atan2(a[0], a[1]);
    case FnAbs: return std::abs(a[0]);
    case FnSign: return a[0] > 0 ? 1.0 : a[0] < 0 ? -1.0 : 0.0;
    case FnFloor: return std::floor(a[0]);
    case FnCeil: return std::ceil(a[0]);
    case FnFract: return a[0] - std::floor(a[0]);
    case FnInt: return std::trunc(a[0]);
    case FnSqrt: return std::sqrt(std::abs(a[0]));
    case FnSqr: return a[0] * a[0];
    case FnExp: return std::exp(std::min(a[0], 60.0));
    case FnLog: return std::log(std::max(std::abs(a[0]), 1e-9));
    case FnPow: return safePow(a[0], a[1]);
    case FnMin: return std::min(a[0], a[1]);
    case FnMax: return std::max(a[0], a[1]);
    case FnMod: return safeMod(a[0], a[1]);
    case FnClamp: return std::min(std::max(a[0], std::min(a[1], a[2])), std::max(a[1], a[2]));
    case FnMix: return a[0] + (a[1] - a[0]) * a[2];
    case FnStep: return a[1] >= a[0] ? 1.0 : 0.0;
    case FnSmoothstep: {
        if (a[0] == a[1])
            return a[2] >= a[0] ? 1.0 : 0.0;
        const double t = std::min(1.0, std::max(0.0, (a[2] - a[0]) / (a[1] - a[0])));
        return t * t * (3 - 2 * t);
    }
    case FnIf: return a[0] != 0 ? a[1] : a[2];
    case FnAbove: return a[0] > a[1] ? 1.0 : 0.0;
    case FnBelow: return a[0] < a[1] ? 1.0 : 0.0;
    case FnEqual: return a[0] == a[1] ? 1.0 : 0.0;
    case FnBand: return a[0] != 0 && a[1] != 0 ? 1.0 : 0.0;
    case FnBor: return a[0] != 0 || a[1] != 0 ? 1.0 : 0.0;
    case FnBnot: return a[0] == 0 ? 1.0 : 0.0;
    case FnRand: return random01() * (argc == 1 ? a[0] : 1.0);
    case FnNoise: return noise(a[0], argc == 2 ? a[1] : 0.0);
    case FnLength: return std::sqrt(a[0] * a[0] + a[1] * a[1]);
    case FnTri: return std::abs((a[0] - std::floor(a[0])) * 2 - 1);
    }
    return 0;
}

QString glslNumber(double v) {
    if (!std::isfinite(v))
        return "0.0";
    QString s = QString::number(v, 'g', 9);
    if (!s.contains('.') && !s.contains('e') && !s.contains("inf") && !s.contains("nan"))
        s += ".0";
    return v < 0 ? "(" + s + ")" : s;
}

struct Token {
    enum Type { End, Number, Identifier, Operator, LeftParen, RightParen, Comma, Separator } type = End;
    QString text;
    double number = 0;
    int position = 0;
};

bool tokenize(const QString &source, QList<Token> *tokens, QString *error) {
    int depth = 0;
    int i = 0;
    const int n = source.size();
    auto push = [&](Token::Type type, const QString &text, int position, double number = 0) {
        tokens->append(Token{type, text, number, position});
    };
    while (i < n) {
        const QChar c = source[i];
        if (c == '\n' || c == ';') {
            // A line break ends a statement unless it sits inside parentheses
            // or straight after an operator, so long expressions can wrap.
            const bool open = c == '\n' && (depth > 0 || (!tokens->isEmpty() &&
                              (tokens->last().type == Token::Operator || tokens->last().type == Token::Comma)));
            if (!open && !tokens->isEmpty() && tokens->last().type != Token::Separator)
                push(Token::Separator, ";", i);
            ++i;
        } else if (c.isSpace()) {
            ++i;
        } else if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            while (i < n && source[i] != '\n')
                ++i;
        } else if (c.isDigit() || (c == '.' && i + 1 < n && source[i + 1].isDigit())) {
            const int start = i;
            while (i < n && (source[i].isDigit() || source[i] == '.'))
                ++i;
            if (i < n && (source[i] == 'e' || source[i] == 'E')) {
                int j = i + 1;
                if (j < n && (source[j] == '+' || source[j] == '-'))
                    ++j;
                if (j < n && source[j].isDigit()) {
                    i = j;
                    while (i < n && source[i].isDigit())
                        ++i;
                }
            }
            bool ok = false;
            const double value = source.mid(start, i - start).toDouble(&ok);
            if (!ok) {
                *error = QString("Bad number '%1'").arg(source.mid(start, i - start));
                return false;
            }
            push(Token::Number, source.mid(start, i - start), start, value);
        } else if (c.isLetter() || c == '_') {
            const int start = i;
            while (i < n && (source[i].isLetterOrNumber() || source[i] == '_'))
                ++i;
            push(Token::Identifier, source.mid(start, i - start), start);
        } else if (c == '(') {
            ++depth;
            push(Token::LeftParen, "(", i++);
        } else if (c == ')') {
            --depth;
            push(Token::RightParen, ")", i++);
        } else if (c == ',') {
            push(Token::Comma, ",", i++);
        } else {
            static const char *const operators[] = {"<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=",
                                                    "+", "-", "*", "/", "%", "^", "<", ">", "!", "?", ":", "="};
            bool matched = false;
            for (const char *op : operators) {
                const QLatin1String text(op);
                if (source.mid(i, text.size()) == text) {
                    push(Token::Operator, text, i);
                    i += text.size();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                *error = QString("Unexpected '%1'").arg(c);
                return false;
            }
        }
    }
    while (!tokens->isEmpty() && tokens->last().type == Token::Separator)
        tokens->removeLast();
    push(Token::End, "", n);
    return true;
}

} // namespace

// ---------------------------------------------------------------- variables

int Vars::slot(const QString &name) {
    const auto it = m_index.constFind(name);
    if (it != m_index.constEnd())
        return *it;
    const int index = int(values.size());
    m_index.insert(name, index);
    m_names << name;
    values.push_back(0);
    m_defined.push_back(false);
    return index;
}

int Vars::declare(const QString &name, double value) {
    const int index = slot(name);
    values[index] = value;
    m_defined[index] = true;
    return index;
}

double Vars::get(const QString &name) const {
    const int index = find(name);
    return index < 0 ? 0.0 : values[index];
}

QStringList Vars::undefined() const {
    QStringList names;
    for (int i = 0; i < m_names.size(); ++i)
        if (!m_defined[i])
            names << m_names[i];
    return names;
}

QString GlslScope::ref(const QString &name, const Vars &vars) const {
    const auto it = locals.constFind(name);
    if (it != locals.constEnd())
        return *it;
    const int slot = vars.find(name);
    return slot < 0 ? QString("0.0") : QString("V[%1]").arg(slot);
}

// ------------------------------------------------------------------- parser

class Parser {
public:
    Parser(Program &program, Vars &vars, const QList<Token> &tokens)
        : m_program(program), m_vars(vars), m_tokens(tokens) {}

    bool run(QString *error) {
        while (peek().type != Token::End) {
            if (peek().type == Token::Separator) {
                ++m_index;
                continue;
            }
            if (!statement())
                break;
            if (peek().type != Token::End && peek().type != Token::Separator) {
                fail(QString("Unexpected '%1'").arg(peek().text));
                break;
            }
        }
        if (!m_error.isEmpty()) {
            *error = m_error;
            return false;
        }
        return true;
    }

private:
    const Token &peek(int ahead = 0) const { return m_tokens[qMin(m_index + ahead, int(m_tokens.size()) - 1)]; }
    bool isOperator(const char *text, int ahead = 0) const {
        return peek(ahead).type == Token::Operator && peek(ahead).text == QLatin1String(text);
    }
    bool accept(const char *text) {
        if (!isOperator(text))
            return false;
        ++m_index;
        return true;
    }
    int fail(const QString &message) {
        if (m_error.isEmpty())
            m_error = message;
        return -1;
    }
    int add(const Program::Node &node) {
        m_program.m_nodes.push_back(node);
        return int(m_program.m_nodes.size()) - 1;
    }
    int binary(int op, int a, int b) {
        Program::Node node;
        node.kind = Program::Node::Binary;
        node.op = op;
        node.a = a;
        node.b = b;
        return add(node);
    }
    int variable(const QString &name) {
        Program::Node node;
        node.kind = Program::Node::Variable;
        node.op = m_vars.slot(name);
        return add(node);
    }

    bool statement() {
        Program::Statement statement;
        static const char *const assignments[] = {"=", "+=", "-=", "*=", "/="};
        static const int compound[] = {-1, OpAdd, OpSub, OpMul, OpDiv};
        if (peek().type == Token::Identifier && peek(1).type == Token::Operator) {
            for (int i = 0; i < 5; ++i) {
                if (!isOperator(assignments[i], 1))
                    continue;
                const QString name = peek().text;
                if (findFunction(name)) {
                    fail(QString("'%1' is a function and can't be assigned").arg(name));
                    return false;
                }
                m_index += 2;
                int value = ternary();
                if (value < 0)
                    return false;
                if (compound[i] >= 0)
                    value = binary(compound[i], variable(name), value);
                statement.target = m_vars.slot(name);
                m_vars.markAssigned(statement.target);
                statement.root = value;
                m_program.m_statements.push_back(statement);
                return true;
            }
        }
        statement.root = ternary();
        if (statement.root < 0)
            return false;
        m_program.m_statements.push_back(statement);
        return true;
    }

    int ternary() {
        const int condition = logicalOr();
        if (condition < 0 || !accept("?"))
            return condition;
        const int yes = ternary();
        if (yes < 0)
            return -1;
        if (!accept(":"))
            return fail("Expected ':' after '?'");
        const int no = ternary();
        if (no < 0)
            return -1;
        Program::Node node;
        node.kind = Program::Node::Ternary;
        node.a = condition;
        node.b = yes;
        node.c = no;
        return add(node);
    }

    // One precedence level of left-associative binary operators.
    template <typename Next>
    int level(std::initializer_list<std::pair<const char *, int>> operators, Next next) {
        int left = (this->*next)();
        while (left >= 0) {
            bool matched = false;
            for (const auto &[text, op] : operators) {
                if (accept(text)) {
                    const int right = (this->*next)();
                    if (right < 0)
                        return -1;
                    left = binary(op, left, right);
                    matched = true;
                    break;
                }
            }
            if (!matched)
                break;
        }
        return left;
    }
    int logicalOr() { return level({{"||", OpOr}}, &Parser::logicalAnd); }
    int logicalAnd() { return level({{"&&", OpAnd}}, &Parser::equality); }
    int equality() { return level({{"==", OpEq}, {"!=", OpNe}}, &Parser::comparison); }
    int comparison() { return level({{"<=", OpLe}, {">=", OpGe}, {"<", OpLt}, {">", OpGt}}, &Parser::sum); }
    int sum() { return level({{"+", OpAdd}, {"-", OpSub}}, &Parser::product); }
    int product() { return level({{"*", OpMul}, {"/", OpDiv}, {"%", OpMod}}, &Parser::unary); }

    int unary() {
        if (accept("-") || accept("!")) {
            const bool negate = m_tokens[m_index - 1].text == "-";
            const int operand = unary();
            if (operand < 0)
                return -1;
            Program::Node node;
            node.kind = Program::Node::Unary;
            node.op = negate ? OpNeg : OpNot;
            node.a = operand;
            return add(node);
        }
        if (accept("+"))
            return unary();
        return power();
    }

    // Binds tighter than the unary minus on its left (-x^2 is -(x^2)) and
    // groups to the right (2^3^2 is 2^9).
    int power() {
        const int base = primary();
        if (base < 0 || !accept("^"))
            return base;
        const int exponent = unary();
        if (exponent < 0)
            return -1;
        return binary(OpPow, base, exponent);
    }

    int primary() {
        const Token token = peek();
        if (token.type == Token::Number) {
            ++m_index;
            Program::Node node;
            node.number = token.number;
            return add(node);
        }
        if (token.type == Token::LeftParen) {
            ++m_index;
            const int inner = ternary();
            if (inner < 0)
                return -1;
            if (peek().type != Token::RightParen)
                return fail("Missing ')'");
            ++m_index;
            return inner;
        }
        if (token.type == Token::Identifier) {
            ++m_index;
            if (peek().type != Token::LeftParen) {
                if (findFunction(token.text))
                    return fail(QString("'%1' is a function; write %1(...)").arg(token.text));
                return variable(token.text);
            }
            const FnInfo *fn = findFunction(token.text);
            if (!fn && token.text == "lerp")
                return fail("Unknown function 'lerp'; mix(a, b, t) does the same");
            if (!fn)
                return fail(QString("Unknown function '%1'").arg(token.text));
            ++m_index;
            int args[3] = {-1, -1, -1};
            int count = 0;
            if (peek().type != Token::RightParen) {
                while (true) {
                    const int arg = ternary();
                    if (arg < 0)
                        return -1;
                    if (count < 3)
                        args[count] = arg;
                    ++count;
                    if (peek().type == Token::Comma) {
                        ++m_index;
                        continue;
                    }
                    break;
                }
            }
            if (peek().type != Token::RightParen)
                return fail(QString("Missing ')' after %1(").arg(token.text));
            ++m_index;
            if (count < fn->minArgs || count > fn->maxArgs) {
                return fail(fn->minArgs == fn->maxArgs
                                ? QString("%1() takes %2 argument(s), not %3").arg(token.text).arg(fn->minArgs).arg(count)
                                : QString("%1() takes %2 to %3 arguments, not %4")
                                      .arg(token.text).arg(fn->minArgs).arg(fn->maxArgs).arg(count));
            }
            Program::Node node;
            node.kind = Program::Node::Call;
            node.op = int(fn - functions);
            node.number = count;
            node.a = args[0];
            node.b = args[1];
            node.c = args[2];
            return add(node);
        }
        if (token.type == Token::End || token.type == Token::Separator)
            return fail("Unfinished expression");
        return fail(QString("Unexpected '%1'").arg(token.text));
    }

    Program &m_program;
    Vars &m_vars;
    const QList<Token> &m_tokens;
    int m_index = 0;
    QString m_error;
};

// ------------------------------------------------------------------ program

Program Program::constant(double value) {
    Program program;
    Node node;
    node.number = value;
    program.m_nodes.push_back(node);
    program.m_statements.push_back(Statement{-1, 0});
    program.m_code.push_back(Instruction{OpConst, 0, value});
    program.m_code.push_back(Instruction{OpResult, 0, 0});
    program.m_constant = true;
    return program;
}

bool Program::parse(const QString &source, Vars &vars, QString *error) {
    m_nodes.clear();
    m_statements.clear();
    m_code.clear();
    m_constant = false;
    QList<Token> tokens;
    QString message;
    if (!tokenize(source, &tokens, &message) || !Parser(*this, vars, tokens).run(&message) || !compile(&message)) {
        m_nodes.clear();
        m_statements.clear();
        m_code.clear();
        if (error)
            *error = message;
        return false;
    }
    m_constant = m_statements.size() == 1 && m_statements[0].target < 0 &&
                 m_nodes[m_statements[0].root].kind == Node::Number;
    return true;
}

constexpr int StackSize = 48;

bool Program::compile(QString *error) {
    int maxDepth = 0;
    for (const auto &statement : m_statements) {
        generate(statement.root, 0, &maxDepth);
        if (statement.target >= 0)
            m_code.push_back(Instruction{OpStore, statement.target, 0});
        else
            m_code.push_back(Instruction{OpResult, 0, 0});
    }
    if (maxDepth >= StackSize) {
        *error = "Expression is nested too deeply";
        return false;
    }
    return true;
}

int Program::generate(int index, int depth, int *maxDepth) {
    const Node node = m_nodes[index];
    *maxDepth = qMax(*maxDepth, depth + 1);
    switch (node.kind) {
    case Node::Number:
        m_code.push_back(Instruction{OpConst, 0, node.number});
        break;
    case Node::Variable:
        m_code.push_back(Instruction{OpLoad, node.op, 0});
        break;
    case Node::Unary:
        generate(node.a, depth, maxDepth);
        m_code.push_back(Instruction{uint8_t(node.op), 0, 0});
        break;
    case Node::Binary:
        generate(node.a, depth, maxDepth);
        generate(node.b, depth + 1, maxDepth);
        m_code.push_back(Instruction{uint8_t(node.op), 0, 0});
        break;
    case Node::Ternary:
        generate(node.a, depth, maxDepth);
        generate(node.b, depth + 1, maxDepth);
        generate(node.c, depth + 2, maxDepth);
        m_code.push_back(Instruction{OpSelect, 0, 0});
        break;
    case Node::Call: {
        const int argc = int(node.number);
        const int args[3] = {node.a, node.b, node.c};
        for (int i = 0; i < argc; ++i)
            generate(args[i], depth + i, maxDepth);
        // The function index and the argument count share the operand.
        m_code.push_back(Instruction{OpCall, node.op * 4 + argc, 0});
        break;
    }
    }
    return depth + 1;
}

double Program::run(std::vector<double> &values) const {
    double stack[StackSize];
    int top = 0;
    double result = 0;
    for (const Instruction &ins : m_code) {
        switch (ins.op) {
        case OpConst: stack[top++] = ins.number; break;
        case OpLoad: stack[top++] = values[ins.arg]; break;
        case OpStore: result = values[ins.arg] = finite(stack[--top]); break;
        case OpResult: result = finite(stack[--top]); break;
        case OpNeg: stack[top - 1] = -stack[top - 1]; break;
        case OpNot: stack[top - 1] = stack[top - 1] == 0 ? 1.0 : 0.0; break;
        case OpSelect:
            top -= 2;
            stack[top - 1] = stack[top - 1] != 0 ? stack[top] : stack[top + 1];
            break;
        case OpCall: {
            const int argc = ins.arg & 3;
            top -= argc;
            const double value = callFunction(functions[ins.arg >> 2].id, argc, stack + top);
            stack[top++] = value;
            break;
        }
        default: {
            const double b = stack[--top];
            double &a = stack[top - 1];
            switch (ins.op) {
            case OpAdd: a += b; break;
            case OpSub: a -= b; break;
            case OpMul: a *= b; break;
            case OpDiv: a = safeDiv(a, b); break;
            case OpMod: a = safeMod(a, b); break;
            case OpPow: a = safePow(a, b); break;
            case OpLt: a = a < b; break;
            case OpGt: a = a > b; break;
            case OpLe: a = a <= b; break;
            case OpGe: a = a >= b; break;
            case OpEq: a = a == b; break;
            case OpNe: a = a != b; break;
            case OpAnd: a = a != 0 && b != 0; break;
            case OpOr: a = a != 0 || b != 0; break;
            }
        }
        }
    }
    return result;
}

// --------------------------------------------------------------------- GLSL

QString Program::glslNode(int index, const GlslScope &scope, const Vars &vars, int *randCalls) const {
    const Node &node = m_nodes[index];
    auto sub = [&](int child) { return glslNode(child, scope, vars, randCalls); };
    switch (node.kind) {
    case Node::Number:
        return glslNumber(node.number);
    case Node::Variable:
        return scope.ref(vars.name(node.op), vars);
    case Node::Unary:
        return node.op == OpNeg ? "(-" + sub(node.a) + ")" : "float(" + sub(node.a) + "==0.0)";
    case Node::Ternary:
        return "((" + sub(node.a) + ")!=0.0?(" + sub(node.b) + "):(" + sub(node.c) + "))";
    case Node::Binary: {
        const QString a = sub(node.a), b = sub(node.b);
        switch (node.op) {
        case OpAdd: return "(" + a + "+" + b + ")";
        case OpSub: return "(" + a + "-" + b + ")";
        case OpMul: return "(" + a + "*" + b + ")";
        case OpDiv: return "f_div(" + a + "," + b + ")";
        case OpMod: return "f_mod(" + a + "," + b + ")";
        case OpPow: return "f_pow(" + a + "," + b + ")";
        case OpLt: return "float(" + a + "<" + b + ")";
        case OpGt: return "float(" + a + ">" + b + ")";
        case OpLe: return "float(" + a + "<=" + b + ")";
        case OpGe: return "float(" + a + ">=" + b + ")";
        case OpEq: return "float(" + a + "==" + b + ")";
        case OpNe: return "float(" + a + "!=" + b + ")";
        case OpAnd: return "float(" + a + "!=0.0&&" + b + "!=0.0)";
        case OpOr: return "float(" + a + "!=0.0||" + b + "!=0.0)";
        }
        return "0.0";
    }
    case Node::Call: {
        const FnInfo &fn = functions[node.op];
        const int argc = int(node.number);
        const QString a = argc > 0 ? sub(node.a) : QString();
        const QString b = argc > 1 ? sub(node.b) : QString();
        const QString c = argc > 2 ? sub(node.c) : QString();
        if (fn.id == FnRand) // Each call site gets its own stream.
            return QString("(f_rand(%1.0)*%2)").arg((*randCalls)++).arg(argc ? a : QString("1.0"));
        if (fn.id == FnNoise)
            return "f_noise(vec2(" + a + "," + (argc > 1 ? b : QString("0.0")) + "))";
        if (fn.id == FnAtan && argc == 2)
            return "f_atan2(" + a + "," + b + ")";
        QString text = QLatin1String(fn.glsl);
        text.replace("%1", a);
        text.replace("%2", b);
        text.replace("%3", c);
        return text;
    }
    }
    return "0.0";
}

QString Program::glsl(GlslScope &scope, const Vars &vars) const {
    QString code;
    int randCalls = 0;
    for (const auto &statement : m_statements) {
        if (statement.target < 0)
            continue;
        const QString name = vars.name(statement.target);
        // The right side is emitted first, so `q = q * 2` reads the frame's
        // q before the local of the same name exists.
        const QString value = glslNode(statement.root, scope, vars, &randCalls);
        if (scope.locals.contains(name)) {
            code += QString("  %1 = %2;\n").arg(scope.locals.value(name), value);
        } else {
            scope.predeclare(name);
            code += QString("  float %1 = %2;\n").arg(scope.locals.value(name), value);
        }
    }
    return code;
}

QString glslPrelude() {
    return QStringLiteral(R"(
float f_sqr(float x) { return x * x; }
float f_div(float a, float b) { return abs(b) < 1e-9 ? 0.0 : a / b; }
float f_mod(float a, float b) { return abs(b) < 1e-9 ? 0.0 : a - b * floor(a / b); }
float f_pow(float a, float b) {
  float r = pow(abs(a), b);
  return (a < 0.0 && mod(abs(b), 2.0) == 1.0) ? -r : r;
}
float f_log(float x) { return log(max(abs(x), 1e-9)); }
float f_atan2(float y, float x) { return (x == 0.0 && y == 0.0) ? 0.0 : atan(y, x); }
float f_clamp(float x, float a, float b) { return min(max(x, min(a, b)), max(a, b)); }
float f_smoothstep(float a, float b, float x) {
  if (a == b) return float(x >= a);
  float t = clamp((x - a) / (b - a), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
float f_tri(float x) { return abs(fract(x) * 2.0 - 1.0); }
float f_hash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}
float f_noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(f_hash(i), f_hash(i + vec2(1.0, 0.0)), f.x),
             mix(f_hash(i + vec2(0.0, 1.0)), f_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float f_rand(float stream) { return f_hash(gl_FragCoord.xy + vec2(u_seed * 61.7 + stream * 17.3, u_seed * 23.1)); }
)");
}

double noise(double x, double y) {
    const double fx = std::floor(x), fy = std::floor(y);
    double tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    const float ix = float(fx), iy = float(fy);
    const double a = latticeHash(ix, iy), b = latticeHash(ix + 1, iy);
    const double c = latticeHash(ix, iy + 1), d = latticeHash(ix + 1, iy + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

} // namespace expr
