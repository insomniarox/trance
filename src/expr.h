#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

// The little language every preset is written in. One syntax, two targets:
// programs run on the CPU once a frame (or once a point), and the same
// programs are emitted as GLSL to run once a pixel.
namespace expr {

// Every program in a preset shares one table, so a value the frame program
// computes can be read by a wave, a shape, or the pixel shader.
class Vars {
public:
    // Finds the variable, creating it when a program mentions it first.
    int slot(const QString &name);
    int find(const QString &name) const { return m_index.value(name, -1); }
    // A variable the engine provides; reading it is never a mistake.
    int declare(const QString &name, double value = 0);
    void markAssigned(int slot) { m_defined[slot] = true; }
    const QString &name(int slot) const { return m_names[slot]; }
    int size() const { return int(values.size()); }
    double get(const QString &name) const;
    // Names that are read somewhere but never assigned: almost always typos.
    QStringList undefined() const;

    std::vector<double> values;

private:
    QHash<QString, int> m_index;
    QStringList m_names;
    std::vector<bool> m_defined;
};

// Which names are plain GLSL locals while emitting a shader; everything else
// reads the frame's value from the uniform array V.
struct GlslScope {
    QHash<QString, QString> locals;
    void predeclare(const QString &name) { locals.insert(name, "l_" + name); }
    QString ref(const QString &name, const Vars &vars) const;
};

class Program {
public:
    static Program constant(double value);

    // Statements are separated by ';' or a line break. Each is an assignment
    // (x = ..., x += ...) or a bare expression. Returns false with a message.
    bool parse(const QString &source, Vars &vars, QString *error);
    bool isEmpty() const { return m_code.empty(); }
    bool isConstant() const { return m_constant; }
    // Whether any statement reads the variable in this slot.
    bool reads(int slot) const;

    // Runs every statement and returns the value of the last one.
    double run(std::vector<double> &values) const;

    // The statements as GLSL. An assigned name becomes a local, declared on
    // first use and seeded from the frame's value when it has one.
    QString glsl(GlslScope &scope, const Vars &vars) const;

private:
    friend class Parser;
    struct Node {
        enum Kind : uint8_t { Number, Variable, Unary, Binary, Ternary, Call } kind = Number;
        int op = 0;         // operator, function, or variable slot
        double number = 0;
        int a = -1, b = -1, c = -1;
    };
    struct Statement {
        int target = -1; // slot, or -1 for a bare expression
        int root = -1;
    };
    struct Instruction {
        uint8_t op;
        int32_t arg;
        double number;
    };
    bool compile(QString *error);
    int generate(int node, int depth, int *maxDepth);
    QString glslNode(int node, const GlslScope &scope, const Vars &vars, int *randCalls) const;

    std::vector<Node> m_nodes;
    std::vector<Statement> m_statements;
    std::vector<Instruction> m_code;
    bool m_constant = false;
};

// Helper functions the emitted GLSL calls; goes in front of any shader that
// embeds a program. Expects a `uniform float u_seed`.
QString glslPrelude();

// Smooth value noise in 0..1, the CPU twin of the shader's f_noise.
double noise(double x, double y);

// Every function formulas can call, in the order the docs list them.
QStringList functionNames();

} // namespace expr
