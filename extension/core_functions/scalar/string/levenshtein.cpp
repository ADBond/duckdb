#include "core_functions/scalar/string_functions.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/common/string_util.hpp"

#include <ctype.h>
#include <algorithm>

namespace duckdb {

// See: https://www.kdnuggets.com/2020/10/optimizing-levenshtein-distance-measuring-text-similarity.html
// And: Iterative 2-row algorithm: https://en.wikipedia.org/wiki/Levenshtein_distance
// Note: A first implementation using the array algorithm version resulted in an error raised by duckdb
// (too muach memory usage)

static idx_t LevenshteinDistance(const string_t &txt, const string_t &tgt, const int64_t &threshold = -1) {
	auto txt_len = txt.GetSize();
	auto tgt_len = tgt.GetSize();
	// TODO: something not this
	auto true_threshold = (threshold == -1 ? std::max({txt_len, tgt_len}) : threshold);
	const idx_t fail_return_value = true_threshold + 1;

	// If one string is empty, the distance equals the length of the other string
	if (txt_len == 0) {
		return tgt_len;
	} else if (tgt_len == 0) {
		return txt_len;
	}

	auto txt_str = txt.GetData();
	auto tgt_str = tgt.GetData();

	// Create two working vectors
	vector<idx_t> distances0(tgt_len + 1, 0);
	vector<idx_t> distances1(tgt_len + 1, 0);

	idx_t cost_substitution = 0;
	idx_t cost_insertion = 0;
	idx_t cost_deletion = 0;

	// initialize distances0 vector
	// edit distance for an empty txt string is just the number of characters to delete from tgt
	for (idx_t pos_tgt = 0; pos_tgt <= tgt_len; pos_tgt++) {
		distances0[pos_tgt] = pos_tgt;
	}

	for (idx_t pos_txt = 0; pos_txt < txt_len; pos_txt++) {
		// calculate distances1 (current raw distances) from the previous row

		distances1[0] = pos_txt + 1;

		for (idx_t pos_tgt = 0; pos_tgt < tgt_len; pos_tgt++) {
			cost_deletion = distances0[pos_tgt + 1] + 1;
			cost_insertion = distances1[pos_tgt] + 1;
			cost_substitution = distances0[pos_tgt];

			if (txt_str[pos_txt] != tgt_str[pos_tgt]) {
				cost_substitution += 1;
			}

			distances1[pos_tgt + 1] = MinValue(cost_deletion, MinValue(cost_substitution, cost_insertion));
		}
		auto min_distance = std::min_element(std::begin(distances1), std::end(distances1));
		if ((*min_distance) > true_threshold) {
			return fail_return_value;
		}
		// copy distances1 (current row) to distances0 (previous row) for next iteration
		// since data in distances1 is always invalidated, a swap without copy is more efficient
		distances0 = distances1;
	}

	return distances0[tgt_len] <= true_threshold ? distances0[tgt_len] : fail_return_value;
}

static int64_t LevenshteinScalarFunction(Vector &result, const string_t str, string_t tgt, int64_t thresh = -1) {
	return (int64_t)LevenshteinDistance(str, tgt, thresh);
}

static void LevenshteinFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &str_vec = args.data[0];
	auto &tgt_vec = args.data[1];
	D_ASSERT(args.ColumnCount() == 2 || args.ColumnCount() == 3);
	if (args.ColumnCount() == 2) {
		BinaryExecutor::Execute<string_t, string_t, int64_t>(
		    str_vec, tgt_vec, result, args.size(),
		    [&](const string_t &str, const string_t &tgt) { return LevenshteinScalarFunction(result, str, tgt, -1); });
		return;
	} else {
		auto &thresh = args.data[2];
		TernaryExecutor::Execute<string_t, string_t, int64_t, int64_t>(
		    str_vec, tgt_vec, thresh, result, args.size(), [&](string_t str, string_t tgt, int64_t thresh) {
			    return LevenshteinScalarFunction(result, str, tgt, thresh);
		    });
	}
}

// ScalarFunctionSet JaroWinklerSimilarityFun::GetFunctions() {
// 	ScalarFunctionSet jaroWinkler;

// 	const auto list_type = LogicalType::LIST(LogicalType::VARCHAR);
// 	auto fun = ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::DOUBLE, JaroWinklerFunction);
// 	jaroWinkler.AddFunction(fun);

// 	fun = ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::DOUBLE}, LogicalType::DOUBLE,
// 	                     JaroWinklerFunction);
// 	jaroWinkler.AddFunction(fun);
// 	return jaroWinkler;
// }

ScalarFunctionSet LevenshteinFun::GetFunctions() {

	ScalarFunctionSet levenshtein;

	const auto list_type = LogicalType::LIST(LogicalType::VARCHAR);
	auto fun = ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BIGINT}, LogicalType::BIGINT,
	                          LevenshteinFunction);
	levenshtein.AddFunction(fun);

	fun = ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::BIGINT, LevenshteinFunction);
	levenshtein.AddFunction(fun);
	return levenshtein;
}

} // namespace duckdb
