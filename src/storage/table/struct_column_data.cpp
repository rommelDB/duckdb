#include "duckdb/storage/table/struct_column_data.hpp"
#include "duckdb/storage/statistics/struct_statistics.hpp"

namespace duckdb {

StructColumnData::StructColumnData(DataTableInfo &info, idx_t column_index, idx_t start_row, LogicalType type_p,
					ColumnData *parent) :
	ColumnData(info, column_index, start_row, move(type_p), parent), validity(info, 0, start_row, this) {
	D_ASSERT(type.id() == LogicalTypeId::STRUCT);
	auto &child_types = type.child_types();
	D_ASSERT(child_types.size() > 0);
	// the sub column index, starting at 1 (0 is the validity mask)
	idx_t sub_column_index = 1;
	for(auto &child_type : child_types) {
		sub_columns.push_back(ColumnData::CreateColumnUnique(info, sub_column_index, start_row, child_type.second, this));
		sub_column_index++;
	}
}

bool StructColumnData::CheckZonemap(ColumnScanState &state, TableFilter &filter) {
	// table filters are not supported yet for struct columns
	return false;
}

void StructColumnData::InitializeScan(ColumnScanState &state) {
	// initialize the validity segment
	ColumnScanState validity_state;
	validity.InitializeScan(validity_state);
	state.child_states.push_back(move(validity_state));

	// initialize the sub-columns
	for(auto &sub_column : sub_columns) {
		ColumnScanState child_state;
		sub_column->InitializeScan(child_state);
		state.child_states.push_back(move(child_state));
	}
}

void StructColumnData::InitializeScanWithOffset(ColumnScanState &state, idx_t row_idx) {
	// initialize the validity segment
	ColumnScanState validity_state;
	validity.InitializeScanWithOffset(validity_state, row_idx);
	state.child_states.push_back(move(validity_state));

	// initialize the sub-columns
	for(auto &sub_column : sub_columns) {
		ColumnScanState child_state;
		sub_column->InitializeScanWithOffset(child_state, row_idx);
		state.child_states.push_back(move(child_state));
	}
}

void StructColumnData::Scan(Transaction &transaction, idx_t vector_index, ColumnScanState &state, Vector &result) {
	D_ASSERT(StructVector::HasEntries(result));
	// D_ASSERT(state.row_index == state.child_states[0].row_index);
	// ColumnData::Scan(transaction, vector_index, state, result);
	validity.Scan(transaction, vector_index, state.child_states[0], result);
	auto &child_entries = StructVector::GetEntries(result);
	for(idx_t i = 0; i < sub_columns.size(); i++) {
		sub_columns[i]->Scan(transaction, vector_index, state.child_states[i + 1], *child_entries[i].second);
	}
	state.child_states[0].Next();
}

void StructColumnData::ScanCommitted(idx_t vector_index, ColumnScanState &state, Vector &result, bool allow_updates) {
	throw NotImplementedException("FIXME: struct");

	// D_ASSERT(state.row_index == state.child_states[0].row_index);
	// ColumnData::ScanCommitted(vector_index, state, result, allow_updates);
	// validity.ScanCommitted(vector_index, state.child_states[0], result, allow_updates);
	// state.Next();
}

void StructColumnData::InitializeAppend(ColumnAppendState &state) {
	ColumnAppendState validity_append;
	validity.InitializeAppend(validity_append);
	state.child_appends.push_back(move(validity_append));

	for(auto &sub_column : sub_columns) {
		ColumnAppendState child_append;
		sub_column->InitializeAppend(child_append);
		state.child_appends.push_back(move(child_append));
	}
}

void StructColumnData::Append(BaseStatistics &stats, ColumnAppendState &state, Vector &vector, idx_t count) {
	D_ASSERT(StructVector::HasEntries(vector));

	validity.Append(*stats.validity_stats, state.child_appends[0], vector, count);

	auto &struct_stats = (StructStatistics &) stats;
	auto &child_entries = StructVector::GetEntries(vector);
	for(idx_t i = 0; i < child_entries.size(); i++) {
		sub_columns[i]->Append(*struct_stats.child_stats[i], state.child_appends[i + 1], *child_entries[i].second, count);
	}
}

void StructColumnData::RevertAppend(row_t start_row) {
	validity.RevertAppend(start_row);
	for(auto &sub_column : sub_columns) {
		sub_column->RevertAppend(start_row);
	}
}

void StructColumnData::Fetch(ColumnScanState &state, row_t row_id, Vector &result) {
	throw NotImplementedException("FIXME: struct");
	// // fetch validity mask
	// if (state.child_states.empty()) {
	// 	ColumnScanState child_state;
	// 	state.child_states.push_back(move(child_state));
	// }
	// validity.Fetch(state.child_states[0], row_id, result);
	// ColumnData::Fetch(state, row_id, result);
}

void StructColumnData::Update(Transaction &transaction, idx_t column_index, Vector &update_vector, row_t *row_ids,
                                idx_t update_count) {
	validity.Update(transaction, column_index, update_vector, row_ids, update_count);
	if (!StructVector::HasEntries(update_vector)) {
		return;
	}
	auto &child_entries = StructVector::GetEntries(update_vector);
	for(idx_t i = 0; i < child_entries.size(); i++) {
		sub_columns[i]->Update(transaction, column_index, *child_entries[i].second, row_ids, update_count);
	}
}

void StructColumnData::UpdateColumn(Transaction &transaction, const vector<column_t> &column_path,
                                      Vector &update_vector, row_t *row_ids, idx_t update_count, idx_t depth) {
	// we can never DIRECTLY update a struct column
	if (depth >= column_path.size()) {
		throw InternalException("Attempting to directly update a struct column - this should not be possible");
	}
	auto update_column = column_path[depth];
	if (update_column == 0) {
		// update the validity column
		validity.UpdateColumn(transaction, column_path, update_vector, row_ids, update_count, depth + 1);
	} else {
		if (update_column > sub_columns.size()) {
			throw InternalException("Update column_path out of range");
		}
		sub_columns[update_column - 1]->UpdateColumn(transaction, column_path, update_vector, row_ids, update_count, depth + 1);
	}
}

unique_ptr<BaseStatistics> StructColumnData::GetUpdateStatistics() {
	throw NotImplementedException("FIXME: struct update");
	// auto stats = updates ? updates->GetStatistics() : nullptr;
	// auto validity_stats = validity.GetUpdateStatistics();
	// if (!stats && !validity_stats) {
	// 	return nullptr;
	// }
	// if (!stats) {
	// 	stats = BaseStatistics::CreateEmpty(type);
	// }
	// stats->validity_stats = move(validity_stats);
	// return stats;
}

void StructColumnData::FetchRow(Transaction &transaction, ColumnFetchState &state, row_t row_id, Vector &result,
                                  idx_t result_idx) {
	throw NotImplementedException("FIXME: struct");
	// // find the segment the row belongs to
	// if (state.child_states.empty()) {
	// 	auto child_state = make_unique<ColumnFetchState>();
	// 	state.child_states.push_back(move(child_state));
	// }
	// validity.FetchRow(transaction, *state.child_states[0], row_id, result, result_idx);
	// ColumnData::FetchRow(transaction, state, row_id, result, result_idx);
}

void StructColumnData::CommitDropColumn() {
	validity.CommitDropColumn();
	for(auto &sub_column : sub_columns) {
		sub_column->CommitDropColumn();
	}
}

// struct StandardColumnCheckpointState : public ColumnCheckpointState {
// 	StandardColumnCheckpointState(RowGroup &row_group, ColumnData &column_data, TableDataWriter &writer)
// 	    : ColumnCheckpointState(row_group, column_data, writer) {
// 	}

// 	unique_ptr<BaseStatistics> GetStatistics() override {
// 		auto stats = global_stats->Copy();
// 		stats->validity_stats = validity_state->GetStatistics();
// 		return stats;
// 	}
// 	unique_ptr<ColumnCheckpointState> validity_state;

// 	void FlushToDisk() override {
// 		ColumnCheckpointState::FlushToDisk();
// 		validity_state->FlushToDisk();
// 	}
// };

unique_ptr<ColumnCheckpointState> StructColumnData::CreateCheckpointState(RowGroup &row_group,
                                                                            TableDataWriter &writer) {
	throw NotImplementedException("FIXME: struct");
	// return make_unique<StandardColumnCheckpointState>(row_group, *this, writer);
}

unique_ptr<ColumnCheckpointState> StructColumnData::Checkpoint(RowGroup &row_group, TableDataWriter &writer,
                                                                 idx_t column_idx) {
	throw NotImplementedException("FIXME: struct");
	// auto base_state = ColumnData::Checkpoint(row_group, writer, column_idx);
	// auto &checkpoint_state = (StandardColumnCheckpointState &)*base_state;
	// checkpoint_state.validity_state = validity.Checkpoint(row_group, writer, column_idx);
	// return base_state;
}

void StructColumnData::Initialize(PersistentColumnData &column_data) {
	throw NotImplementedException("FIXME: struct");
	// auto &persistent = (StandardPersistentColumnData &)column_data;
	// ColumnData::Initialize(column_data);
	// validity.Initialize(*persistent.validity);
}

shared_ptr<ColumnData> StructColumnData::Deserialize(DataTableInfo &info, idx_t column_index, idx_t start_row,
                                                       Deserializer &source, const LogicalType &type) {
	throw NotImplementedException("FIXME: struct");
	// auto result = make_shared<StandardColumnData>(info, column_index, start_row, type, nullptr);
	// BaseDeserialize(info.db, source, type, *result);
	// ColumnData::BaseDeserialize(info.db, source, LogicalType(LogicalTypeId::VALIDITY), result->validity);
	// return move(result);
}

void StructColumnData::GetStorageInfo(idx_t row_group_index, vector<idx_t> col_path, vector<vector<Value>> &result) {
	throw NotImplementedException("FIXME: struct");
	// ColumnData::GetStorageInfo(row_group_index, col_path, result);
	// col_path.push_back(0);
	// validity.GetStorageInfo(row_group_index, move(col_path), result);
}

}
