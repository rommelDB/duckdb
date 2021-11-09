#include "duckdb/planner/expression_binder/check_binder.hpp"

#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"

namespace duckdb {

CheckBinder::CheckBinder(Binder &binder, ClientContext &context, string table, vector<ColumnDefinition> &columns,
                         unordered_set<column_t> &bound_columns)
    : ExpressionBinder(binder, context), table(move(table)), columns(columns), bound_columns(bound_columns) {
	target_type = LogicalType::INTEGER;
}

BindResult CheckBinder::BindExpression(unique_ptr<ParsedExpression> *expr_ptr, idx_t depth, bool root_expression) {
	auto &expr = **expr_ptr;
	switch (expr.GetExpressionClass()) {
	case ExpressionClass::WINDOW:
		return BindResult("window functions are not allowed in check constraints");
	case ExpressionClass::SUBQUERY:
		return BindResult("cannot use subquery in check constraint");
	case ExpressionClass::COLUMN_REF:
		return BindCheckColumn((ColumnRefExpression &)expr);
	default:
		return ExpressionBinder::BindExpression(expr_ptr, depth);
	}
}

string CheckBinder::UnsupportedAggregateMessage() {
	return "aggregate functions are not allowed in check constraints";
}

BindResult CheckBinder::BindCheckColumn(ColumnRefExpression &colref) {
	if (colref.IsQualified()) {
		throw InternalException("FIXME: bind check column with > references");
	}
	for (idx_t i = 0; i < columns.size(); i++) {
		if (colref.column_names[0] == columns[i].name) {
			bound_columns.insert(i);
			return BindResult(make_unique<BoundReferenceExpression>(columns[i].type, i));
		}
	}
	throw BinderException("Table does not contain column %s referenced in check constraint!", colref.column_names[0]);
}

} // namespace duckdb
