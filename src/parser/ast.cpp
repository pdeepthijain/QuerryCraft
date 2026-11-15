#include "parser/ast.hpp"

#include <sstream>

namespace querycraft::parser {

namespace {

std::string make_indent(std::size_t level) {
    return std::string(level * 2, ' ');
}

} // namespace

std::string LiteralExpression::to_string() const {
    if (is_null()) {
        return "NULL";
    }
    if (is_int()) {
        return std::to_string(int_value());
    }
    if (is_float()) {
        std::string s = std::to_string(float_value());
        // Strip trailing zeros after decimal point for cleaner output, but retain at least .0
        auto dot_pos = s.find('.');
        if (dot_pos != std::string::npos) {
            auto last_non_zero = s.find_last_not_of('0');
            if (last_non_zero != std::string::npos && last_non_zero > dot_pos) {
                s.erase(last_non_zero + 1);
            } else if (last_non_zero == dot_pos) {
                s.erase(dot_pos + 2); // retain one zero e.g. 9.0
            }
        }
        return s;
    }
    if (is_string()) {
        return "'" + string_value() + "'";
    }
    if (is_bool()) {
        return bool_value() ? "TRUE" : "FALSE";
    }
    return "UNKNOWN_LITERAL";
}

std::string BinaryExpression::to_string() const {
    std::string l = left_ ? left_->to_string() : "NULL";
    std::string r = right_ ? right_->to_string() : "NULL";
    return "(" + l + " " + std::string(binary_op_to_string(op_)) + " " + r + ")";
}

std::string UnaryExpression::to_string() const {
    std::string op_str = std::string(unary_op_to_string(op_));
    std::string operand_str = operand_ ? operand_->to_string() : "NULL";
    if (op_ == UnaryOp::Not) {
        return "NOT (" + operand_str + ")";
    }
    return op_str + "(" + operand_str + ")";
}

std::string SelectStatement::to_string() const {
    std::ostringstream ss;
    ss << "SELECT ";
    if (select_all_) {
        ss << "*";
    } else {
        for (std::size_t i = 0; i < select_list_.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << (select_list_[i] ? select_list_[i]->to_string() : "NULL");
        }
    }
    ss << " FROM " << from_table_;
    if (where_clause_) {
        ss << " WHERE " << where_clause_->to_string();
    }
    if (!order_by_.empty()) {
        ss << " ORDER BY ";
        for (std::size_t i = 0; i < order_by_.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << (order_by_[i].expr ? order_by_[i].expr->to_string() : "NULL");
            ss << (order_by_[i].is_asc ? " ASC" : " DESC");
        }
    }
    if (limit_.has_value()) {
        ss << " LIMIT " << *limit_;
    }
    ss << ";";
    return ss.str();
}

std::string CreateTableStatement::to_string() const {
    std::ostringstream ss;
    ss << "CREATE TABLE " << table_name_ << " (";
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << columns_[i].name << " " << data_type_to_string(columns_[i].data_type);
        if (columns_[i].length.has_value()) {
            ss << "(" << *columns_[i].length << ")";
        }
        if (columns_[i].is_primary_key) {
            ss << " PRIMARY KEY";
        }
    }
    if (!primary_key_columns_.empty()) {
        // Table level primary key if multiple or explicit
        bool is_table_constraint = false;
        for (const auto& pk : primary_key_columns_) {
            bool found_column_pk = false;
            for (const auto& c : columns_) {
                if (c.name == pk && c.is_primary_key) {
                    found_column_pk = true;
                    break;
                }
            }
            if (!found_column_pk) {
                is_table_constraint = true;
                break;
            }
        }
        if (is_table_constraint) {
            ss << ", PRIMARY KEY (";
            for (std::size_t i = 0; i < primary_key_columns_.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << primary_key_columns_[i];
            }
            ss << ")";
        }
    }
    ss << ");";
    return ss.str();
}

std::string InsertStatement::to_string() const {
    std::ostringstream ss;
    ss << "INSERT INTO " << table_name_;
    if (!columns_.empty()) {
        ss << " (";
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << columns_[i];
        }
        ss << ")";
    }
    ss << " VALUES (";
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << (values_[i] ? values_[i]->to_string() : "NULL");
    }
    ss << ");";
    return ss.str();
}

std::string UpdateStatement::to_string() const {
    std::ostringstream ss;
    ss << "UPDATE " << table_name_ << " SET ";
    for (std::size_t i = 0; i < assignments_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << assignments_[i].column << " = "
           << (assignments_[i].value ? assignments_[i].value->to_string() : "NULL");
    }
    if (where_clause_) {
        ss << " WHERE " << where_clause_->to_string();
    }
    ss << ";";
    return ss.str();
}

std::string DeleteStatement::to_string() const {
    std::ostringstream ss;
    ss << "DELETE FROM " << table_name_;
    if (where_clause_) {
        ss << " WHERE " << where_clause_->to_string();
    }
    ss << ";";
    return ss.str();
}

std::string CreateIndexStatement::to_string() const {
    std::ostringstream ss;
    ss << "CREATE INDEX " << index_name_ << " ON " << table_name_ << " (";
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << columns_[i];
    }
    ss << ");";
    return ss.str();
}

std::string ast_debug_print(const AstNode& node, std::size_t indent) {
    std::ostringstream ss;
    std::string ind = make_indent(indent);

    // Dynamic downcasting to print detailed tree structure
    if (const auto* select = dynamic_cast<const SelectStatement*>(&node)) {
        ss << ind << "SelectStatement\n";
        ss << ind << "  SelectAll: " << (select->select_all() ? "true" : "false") << "\n";
        ss << ind << "  Columns:\n";
        for (const auto& col : select->select_list()) {
            ss << ast_debug_print(*col, indent + 2);
        }
        ss << ind << "  FromTable: " << select->from_table() << "\n";
        if (select->where_clause()) {
            ss << ind << "  WhereClause:\n";
            ss << ast_debug_print(*select->where_clause(), indent + 2);
        }
        if (!select->order_by().empty()) {
            ss << ind << "  OrderBy:\n";
            for (const auto& elem : select->order_by()) {
                ss << ind << "    Direction: " << (elem.is_asc ? "ASC" : "DESC") << "\n";
                if (elem.expr) {
                    ss << ast_debug_print(*elem.expr, indent + 3);
                }
            }
        }
        if (select->limit().has_value()) {
            ss << ind << "  Limit: " << *select->limit() << "\n";
        }
    } else if (const auto* create = dynamic_cast<const CreateTableStatement*>(&node)) {
        ss << ind << "CreateTableStatement\n";
        ss << ind << "  TableName: " << create->table_name() << "\n";
        ss << ind << "  Columns:\n";
        for (const auto& col : create->columns()) {
            ss << ind << "    " << col.name << " " << data_type_to_string(col.data_type);
            if (col.length) ss << "(" << *col.length << ")";
            if (col.is_primary_key) ss << " [PRIMARY KEY]";
            ss << "\n";
        }
        if (!create->primary_key_columns().empty()) {
            ss << ind << "  PrimaryKeys: [";
            for (std::size_t i = 0; i < create->primary_key_columns().size(); ++i) {
                if (i > 0) ss << ", ";
                ss << create->primary_key_columns()[i];
            }
            ss << "]\n";
        }
    } else if (const auto* insert = dynamic_cast<const InsertStatement*>(&node)) {
        ss << ind << "InsertStatement\n";
        ss << ind << "  TableName: " << insert->table_name() << "\n";
        if (!insert->columns().empty()) {
            ss << ind << "  Columns: [";
            for (std::size_t i = 0; i < insert->columns().size(); ++i) {
                if (i > 0) ss << ", ";
                ss << insert->columns()[i];
            }
            ss << "]\n";
        }
        ss << ind << "  Values:\n";
        for (const auto& val : insert->values()) {
            ss << ast_debug_print(*val, indent + 2);
        }
    } else if (const auto* update = dynamic_cast<const UpdateStatement*>(&node)) {
        ss << ind << "UpdateStatement\n";
        ss << ind << "  TableName: " << update->table_name() << "\n";
        ss << ind << "  Assignments:\n";
        for (const auto& assign : update->assignments()) {
            ss << ind << "    " << assign.column << " =\n";
            if (assign.value) {
                ss << ast_debug_print(*assign.value, indent + 3);
            }
        }
        if (update->where_clause()) {
            ss << ind << "  WhereClause:\n";
            ss << ast_debug_print(*update->where_clause(), indent + 2);
        }
    } else if (const auto* del = dynamic_cast<const DeleteStatement*>(&node)) {
        ss << ind << "DeleteStatement\n";
        ss << ind << "  TableName: " << del->table_name() << "\n";
        if (del->where_clause()) {
            ss << ind << "  WhereClause:\n";
            ss << ast_debug_print(*del->where_clause(), indent + 2);
        }
    } else if (const auto* idx = dynamic_cast<const CreateIndexStatement*>(&node)) {
        ss << ind << "CreateIndexStatement\n";
        ss << ind << "  IndexName: " << idx->index_name() << "\n";
        ss << ind << "  TableName: " << idx->table_name() << "\n";
        ss << ind << "  Columns: [";
        for (std::size_t i = 0; i < idx->columns().size(); ++i) {
            if (i > 0) ss << ", ";
            ss << idx->columns()[i];
        }
        ss << "]\n";
    } else if (const auto* bin = dynamic_cast<const BinaryExpression*>(&node)) {
        ss << ind << "BinaryExpression (" << binary_op_to_string(bin->op()) << ")\n";
        if (bin->left()) {
            ss << ast_debug_print(*bin->left(), indent + 1);
        }
        if (bin->right()) {
            ss << ast_debug_print(*bin->right(), indent + 1);
        }
    } else if (const auto* un = dynamic_cast<const UnaryExpression*>(&node)) {
        ss << ind << "UnaryExpression (" << unary_op_to_string(un->op()) << ")\n";
        if (un->operand()) {
            ss << ast_debug_print(*un->operand(), indent + 1);
        }
    } else if (const auto* col = dynamic_cast<const ColumnRefExpression*>(&node)) {
        ss << ind << "ColumnRef: " << col->to_string() << "\n";
    } else if (const auto* lit = dynamic_cast<const LiteralExpression*>(&node)) {
        ss << ind << "Literal: " << lit->to_string() << "\n";
    } else {
        ss << ind << "AstNode: " << node.to_string() << "\n";
    }

    return ss.str();
}

} // namespace querycraft::parser
