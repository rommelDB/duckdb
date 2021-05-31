//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/statistics/struct_statistics.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/statistics/base_statistics.hpp"

namespace duckdb {

class StructStatistics : public BaseStatistics {
public:
	explicit StructStatistics(LogicalType type);

	vector<unique_ptr<BaseStatistics>> child_stats;

public:
	void Merge(const BaseStatistics &other) override;
	FilterPropagateResult CheckZonemap(ExpressionType comparison_type, const Value &constant);

	unique_ptr<BaseStatistics> Copy() override;
	void Serialize(Serializer &serializer) override;
	static unique_ptr<BaseStatistics> Deserialize(Deserializer &source, LogicalType type);
	void Verify(Vector &vector, idx_t count) override;

	string ToString() override;
};

} // namespace duckdb
