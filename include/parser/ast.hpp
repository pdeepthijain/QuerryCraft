#pragma once

#include "parser/token.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace querycraft::parser {

// Supported SQL Data Types
enum class DataType : std::uint8_t {
    INT,
    FLOAT,
    TEXT,
    VARCHAR
};

constexpr std::string_view data_type_to_string(DataType type) noexcept {
    switch (type) {
        case DataType::INT:     return "INT";
        case DataType::FLOAT:   return "FLOAT";
        case DataType::TEXT:    return "TEXT";
        case DataType::VARCHAR: return "VARCHAR";
    }
    return "UNKNOWN";
}

// Supported Binary Operators
enum class BinaryOp : std::uint8_t {
    // Arithmetic
    Add,
    Subtract,
    Multiply,
    Divide,

    // Comparison
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,

    // Logical
    And,
    Or
};

constexpr std::string_view binary_op_to_string(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add:          return "+";
        case BinaryOp::Subtract:     return "-";
        case BinaryOp::Multiply:     return "*";
        case BinaryOp::Divide:       return "/";
        case BinaryOp::Equal:        return "=";
        case BinaryOp::NotEqual:     return "!=";
        case BinaryOp::LessThan:     return "<";
        case BinaryOp::LessEqual:    return "<=";
        case BinaryOp::GreaterThan:  return ">";
        case BinaryOp::GreaterEqual: return ">=";
        case BinaryOp::And:          return "AND";
        case BinaryOp::Or:           return "OR";
    }
    return "UNKNOWN";
}

// Supported Unary Operators
enum class UnaryOp : std::uint8_t {
    Plus,
    Minus,
    Not
};

constexpr std::string_view unary_op_to_string(UnaryOp op) noexcept {
    switch (op) {
        case UnaryOp::Plus:  return "+";
        case UnaryOp::Minus: return "-";
        case UnaryOp::Not:   return "NOT";
    }
    return "UNKNOWN";
}

// Statement Types
enum class StatementType : std::uint8_t {
    SELECT,
    CREATE_TABLE,
    INSERT,
    UPDATE,
    DELETE,
    CREATE_INDEX
};

constexpr std::string_view statement_type_to_string(StatementType type) noexcept {
    switch (type) {
        case StatementType::SELECT:       return "SELECT";
        case StatementType::CREATE_TABLE: return "CREATE_TABLE";
        case StatementType::INSERT:       return "INSERT";
        case StatementType::UPDATE:       return "UPDATE";
        case StatementType::DELETE:       return "DELETE";
        case StatementType::CREATE_INDEX: return "CREATE_INDEX";
    }
    return "UNKNOWN";
}

// Expression Types
enum class ExpressionType : std::uint8_t {
    LITERAL,
    COLUMN_REF,
    BINARY,
    UNARY
};

// Abstract base class for all AST nodes
class AstNode {
public:
    explicit AstNode(SourceLocation location = {1, 1}) : location_(location) {}
    virtual ~AstNode() = default;

    AstNode(const AstNode&) = default;
    AstNode& operator=(const AstNode&) = default;
    AstNode(AstNode&&) noexcept = default;
    AstNode& operator=(AstNode&&) noexcept = default;

    [[nodiscard]] const SourceLocation& location() const noexcept { return location_; }
    [[nodiscard]] std::size_t line() const noexcept { return location_.line; }
    [[nodiscard]] std::size_t column() const noexcept { return location_.column; }

    [[nodiscard]] virtual std::string to_string() const = 0;

private:
    SourceLocation location_;
};

// -----------------------------------------------------------------------------
// Expressions
// -----------------------------------------------------------------------------

class Expression : public AstNode {
public:
    explicit Expression(ExpressionType expr_type, SourceLocation location = {1, 1})
        : AstNode(location), expr_type_(expr_type) {}

    [[nodiscard]] ExpressionType expr_type() const noexcept { return expr_type_; }
    [[nodiscard]] virtual std::unique_ptr<Expression> clone() const = 0;

private:
    ExpressionType expr_type_;
};

// Literal Expression: Holds null, integer, float, string, or boolean
class LiteralExpression : public Expression {
public:
    using ValueType = std::variant<std::monostate, std::int64_t, double, std::string, bool>;

    explicit LiteralExpression(ValueType value, SourceLocation location = {1, 1})
        : Expression(ExpressionType::LITERAL, location), value_(std::move(value)) {}

    static std::unique_ptr<LiteralExpression> make_int(std::int64_t val, SourceLocation loc = {1, 1}) {
        return std::make_unique<LiteralExpression>(val, loc);
    }

    static std::unique_ptr<LiteralExpression> make_float(double val, SourceLocation loc = {1, 1}) {
        return std::make_unique<LiteralExpression>(val, loc);
    }

    static std::unique_ptr<LiteralExpression> make_string(std::string val, SourceLocation loc = {1, 1}) {
        return std::make_unique<LiteralExpression>(std::move(val), loc);
    }

    static std::unique_ptr<LiteralExpression> make_null(SourceLocation loc = {1, 1}) {
        return std::make_unique<LiteralExpression>(std::monostate{}, loc);
    }

    static std::unique_ptr<LiteralExpression> make_bool(bool val, SourceLocation loc = {1, 1}) {
        return std::make_unique<LiteralExpression>(val, loc);
    }

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::monostate>(value_);
    }

    [[nodiscard]] bool is_int() const noexcept {
        return std::holds_alternative<std::int64_t>(value_);
    }

    [[nodiscard]] bool is_float() const noexcept {
        return std::holds_alternative<double>(value_);
    }

    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(value_);
    }

    [[nodiscard]] bool is_bool() const noexcept {
        return std::holds_alternative<bool>(value_);
    }

    [[nodiscard]] std::int64_t int_value() const {
        return std::get<std::int64_t>(value_);
    }

    [[nodiscard]] double float_value() const {
        return std::get<double>(value_);
    }

    [[nodiscard]] const std::string& string_value() const {
        return std::get<std::string>(value_);
    }

    [[nodiscard]] bool bool_value() const {
        return std::get<bool>(value_);
    }

    [[nodiscard]] const ValueType& raw_value() const noexcept {
        return value_;
    }

    [[nodiscard]] std::string to_string() const override;

    [[nodiscard]] std::unique_ptr<Expression> clone() const override {
        return std::make_unique<LiteralExpression>(value_, location());
    }

private:
    ValueType value_;
};

// Column Reference Expression: e.g. name, cgpa, or students.cgpa
class ColumnRefExpression : public Expression {
public:
    ColumnRefExpression(std::string column_name, std::string table_name = "", SourceLocation location = {1, 1})
        : Expression(ExpressionType::COLUMN_REF, location),
          column_name_(std::move(column_name)),
          table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& column_name() const noexcept { return column_name_; }
    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }
    [[nodiscard]] bool has_table_name() const noexcept { return !table_name_.empty(); }

    [[nodiscard]] std::string to_string() const override {
        if (table_name_.empty()) {
            return column_name_;
        }
        return table_name_ + "." + column_name_;
    }

    [[nodiscard]] std::unique_ptr<Expression> clone() const override {
        return std::make_unique<ColumnRefExpression>(column_name_, table_name_, location());
    }

private:
    std::string column_name_;
    std::string table_name_;
};

// Binary Expression: e.g. a + b, x > 9.0, cond1 AND cond2
class BinaryExpression : public Expression {
public:
    BinaryExpression(BinaryOp op,
                     std::unique_ptr<Expression> left,
                     std::unique_ptr<Expression> right,
                     SourceLocation location = {1, 1})
        : Expression(ExpressionType::BINARY, location),
          op_(op),
          left_(std::move(left)),
          right_(std::move(right)) {}

    [[nodiscard]] BinaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expression* left() const noexcept { return left_.get(); }
    [[nodiscard]] const Expression* right() const noexcept { return right_.get(); }
    [[nodiscard]] std::unique_ptr<Expression>& left_mut() noexcept { return left_; }
    [[nodiscard]] std::unique_ptr<Expression>& right_mut() noexcept { return right_; }

    [[nodiscard]] std::string to_string() const override;

    [[nodiscard]] std::unique_ptr<Expression> clone() const override {
        return std::make_unique<BinaryExpression>(
            op_,
            left_ ? left_->clone() : nullptr,
            right_ ? right_->clone() : nullptr,
            location()
        );
    }

private:
    BinaryOp op_;
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};

// Unary Expression: e.g. -x, +x, NOT cond
class UnaryExpression : public Expression {
public:
    UnaryExpression(UnaryOp op,
                    std::unique_ptr<Expression> operand,
                    SourceLocation location = {1, 1})
        : Expression(ExpressionType::UNARY, location),
          op_(op),
          operand_(std::move(operand)) {}

    [[nodiscard]] UnaryOp op() const noexcept { return op_; }
    [[nodiscard]] const Expression* operand() const noexcept { return operand_.get(); }
    [[nodiscard]] std::unique_ptr<Expression>& operand_mut() noexcept { return operand_; }

    [[nodiscard]] std::string to_string() const override;

    [[nodiscard]] std::unique_ptr<Expression> clone() const override {
        return std::make_unique<UnaryExpression>(
            op_,
            operand_ ? operand_->clone() : nullptr,
            location()
        );
    }

private:
    UnaryOp op_;
    std::unique_ptr<Expression> operand_;
};

// -----------------------------------------------------------------------------
// Statements
// -----------------------------------------------------------------------------

class Statement : public AstNode {
public:
    explicit Statement(StatementType stmt_type, SourceLocation location = {1, 1})
        : AstNode(location), stmt_type_(stmt_type) {}

    [[nodiscard]] StatementType stmt_type() const noexcept { return stmt_type_; }

private:
    StatementType stmt_type_;
};

// Order By Specification for SELECT queries
struct OrderByElement {
    std::unique_ptr<Expression> expr;
    bool is_asc{true};

    OrderByElement(std::unique_ptr<Expression> e, bool asc = true)
        : expr(std::move(e)), is_asc(asc) {}

    OrderByElement(const OrderByElement& other)
        : expr(other.expr ? other.expr->clone() : nullptr), is_asc(other.is_asc) {}

    OrderByElement& operator=(const OrderByElement& other) {
        if (this != &other) {
            expr = other.expr ? other.expr->clone() : nullptr;
            is_asc = other.is_asc;
        }
        return *this;
    }

    OrderByElement(OrderByElement&&) noexcept = default;
    OrderByElement& operator=(OrderByElement&&) noexcept = default;
};

// SELECT Statement
class SelectStatement : public Statement {
public:
    explicit SelectStatement(SourceLocation location = {1, 1})
        : Statement(StatementType::SELECT, location) {}

    [[nodiscard]] bool select_all() const noexcept { return select_all_; }
    void set_select_all(bool val) noexcept { select_all_ = val; }

    [[nodiscard]] const std::vector<std::unique_ptr<Expression>>& select_list() const noexcept {
        return select_list_;
    }
    std::vector<std::unique_ptr<Expression>>& select_list_mut() noexcept {
        return select_list_;
    }
    void add_select_item(std::unique_ptr<Expression> expr) {
        select_list_.push_back(std::move(expr));
    }

    [[nodiscard]] const std::string& from_table() const noexcept { return from_table_; }
    void set_from_table(std::string table) { from_table_ = std::move(table); }

    [[nodiscard]] const Expression* where_clause() const noexcept { return where_clause_.get(); }
    [[nodiscard]] std::unique_ptr<Expression>& where_clause_mut() noexcept { return where_clause_; }
    void set_where_clause(std::unique_ptr<Expression> where_clause) {
        where_clause_ = std::move(where_clause);
    }

    [[nodiscard]] const std::vector<OrderByElement>& order_by() const noexcept { return order_by_; }
    std::vector<OrderByElement>& order_by_mut() noexcept { return order_by_; }
    void add_order_by(std::unique_ptr<Expression> expr, bool is_asc = true) {
        order_by_.emplace_back(std::move(expr), is_asc);
    }

    [[nodiscard]] const std::optional<std::int64_t>& limit() const noexcept { return limit_; }
    void set_limit(std::optional<std::int64_t> limit) noexcept { limit_ = limit; }

    [[nodiscard]] std::string to_string() const override;

private:
    bool select_all_{false};
    std::vector<std::unique_ptr<Expression>> select_list_;
    std::string from_table_;
    std::unique_ptr<Expression> where_clause_{nullptr};
    std::vector<OrderByElement> order_by_;
    std::optional<std::int64_t> limit_{std::nullopt};
};

// Column Definition in CREATE TABLE
struct ColumnDefinition {
    std::string name;
    DataType data_type;
    bool is_primary_key{false};
    std::optional<std::size_t> length{std::nullopt}; // For VARCHAR(length)

    ColumnDefinition(std::string col_name, DataType type, bool primary_key = false,
                     std::optional<std::size_t> len = std::nullopt)
        : name(std::move(col_name)), data_type(type), is_primary_key(primary_key), length(len) {}
};

// CREATE TABLE Statement
class CreateTableStatement : public Statement {
public:
    explicit CreateTableStatement(std::string table_name, SourceLocation location = {1, 1})
        : Statement(StatementType::CREATE_TABLE, location), table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }
    [[nodiscard]] const std::vector<ColumnDefinition>& columns() const noexcept { return columns_; }
    std::vector<ColumnDefinition>& columns_mut() noexcept { return columns_; }

    void add_column(ColumnDefinition col) {
        if (col.is_primary_key) {
            primary_key_columns_.push_back(col.name);
        }
        columns_.push_back(std::move(col));
    }

    [[nodiscard]] const std::vector<std::string>& primary_key_columns() const noexcept {
        return primary_key_columns_;
    }

    void add_primary_key_column(std::string col_name) {
        // Also mark corresponding column definition if present
        for (auto& col : columns_) {
            if (col.name == col_name) {
                col.is_primary_key = true;
            }
        }
        primary_key_columns_.push_back(std::move(col_name));
    }

    [[nodiscard]] std::string to_string() const override;

private:
    std::string table_name_;
    std::vector<ColumnDefinition> columns_;
    std::vector<std::string> primary_key_columns_;
};

// INSERT Statement
class InsertStatement : public Statement {
public:
    explicit InsertStatement(std::string table_name, SourceLocation location = {1, 1})
        : Statement(StatementType::INSERT, location), table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }

    [[nodiscard]] const std::vector<std::string>& columns() const noexcept { return columns_; }
    std::vector<std::string>& columns_mut() noexcept { return columns_; }
    void add_column(std::string column) { columns_.push_back(std::move(column)); }

    [[nodiscard]] const std::vector<std::unique_ptr<Expression>>& values() const noexcept { return values_; }
    std::vector<std::unique_ptr<Expression>>& values_mut() noexcept { return values_; }
    void add_value(std::unique_ptr<Expression> val) { values_.push_back(std::move(val)); }

    [[nodiscard]] std::string to_string() const override;

private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::vector<std::unique_ptr<Expression>> values_;
};

// Assignment in UPDATE: column = expr
struct UpdateAssignment {
    std::string column;
    std::unique_ptr<Expression> value;

    UpdateAssignment(std::string col, std::unique_ptr<Expression> val)
        : column(std::move(col)), value(std::move(val)) {}

    UpdateAssignment(const UpdateAssignment& other)
        : column(other.column), value(other.value ? other.value->clone() : nullptr) {}

    UpdateAssignment& operator=(const UpdateAssignment& other) {
        if (this != &other) {
            column = other.column;
            value = other.value ? other.value->clone() : nullptr;
        }
        return *this;
    }

    UpdateAssignment(UpdateAssignment&&) noexcept = default;
    UpdateAssignment& operator=(UpdateAssignment&&) noexcept = default;
};

// UPDATE Statement
class UpdateStatement : public Statement {
public:
    explicit UpdateStatement(std::string table_name, SourceLocation location = {1, 1})
        : Statement(StatementType::UPDATE, location), table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }

    [[nodiscard]] const std::vector<UpdateAssignment>& assignments() const noexcept { return assignments_; }
    std::vector<UpdateAssignment>& assignments_mut() noexcept { return assignments_; }
    void add_assignment(std::string col, std::unique_ptr<Expression> val) {
        assignments_.emplace_back(std::move(col), std::move(val));
    }

    [[nodiscard]] const Expression* where_clause() const noexcept { return where_clause_.get(); }
    [[nodiscard]] std::unique_ptr<Expression>& where_clause_mut() noexcept { return where_clause_; }
    void set_where_clause(std::unique_ptr<Expression> where_clause) {
        where_clause_ = std::move(where_clause);
    }

    [[nodiscard]] std::string to_string() const override;

private:
    std::string table_name_;
    std::vector<UpdateAssignment> assignments_;
    std::unique_ptr<Expression> where_clause_{nullptr};
};

// DELETE Statement
class DeleteStatement : public Statement {
public:
    explicit DeleteStatement(std::string table_name, SourceLocation location = {1, 1})
        : Statement(StatementType::DELETE, location), table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }

    [[nodiscard]] const Expression* where_clause() const noexcept { return where_clause_.get(); }
    [[nodiscard]] std::unique_ptr<Expression>& where_clause_mut() noexcept { return where_clause_; }
    void set_where_clause(std::unique_ptr<Expression> where_clause) {
        where_clause_ = std::move(where_clause);
    }

    [[nodiscard]] std::string to_string() const override;

private:
    std::string table_name_;
    std::unique_ptr<Expression> where_clause_{nullptr};
};

// CREATE INDEX Statement
class CreateIndexStatement : public Statement {
public:
    CreateIndexStatement(std::string index_name, std::string table_name, SourceLocation location = {1, 1})
        : Statement(StatementType::CREATE_INDEX, location),
          index_name_(std::move(index_name)),
          table_name_(std::move(table_name)) {}

    [[nodiscard]] const std::string& index_name() const noexcept { return index_name_; }
    [[nodiscard]] const std::string& table_name() const noexcept { return table_name_; }

    [[nodiscard]] const std::vector<std::string>& columns() const noexcept { return columns_; }
    std::vector<std::string>& columns_mut() noexcept { return columns_; }
    void add_column(std::string col) { columns_.push_back(std::move(col)); }

    [[nodiscard]] std::string to_string() const override;

private:
    std::string index_name_;
    std::string table_name_;
    std::vector<std::string> columns_;
};

// Stream insertion operators for logging and tests
inline std::ostream& operator<<(std::ostream& os, const AstNode& node) {
    os << node.to_string();
    return os;
}

// Pretty debug-printing utility for AST nodes with indentation
std::string ast_debug_print(const AstNode& node, std::size_t indent_level = 0);

} // namespace querycraft::parser
