#include "test_support.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace recto::test {
namespace {

struct ProvenanceRow {
	std::string path;
	std::string sha256;
	std::string license;
	std::string origin;
	std::string purpose;
	size_t line_number{0};
};

struct ProvenanceParseResult {
	bool ok{false};
	std::vector<ProvenanceRow> rows;
	std::vector<std::string> errors;
};

struct CorpusValidationResult {
	bool ok{false};
	size_t verified_file_count{0};
	std::vector<std::string> errors;
};

std::string_view trim_ascii(std::string_view sv) noexcept
{
	while (!sv.empty() &&
		   (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() &&
		   (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
		sv.remove_suffix(1);
	}
	return sv;
}

bool is_valid_sha256_hex(std::string_view sv) noexcept
{
	if (sv.size() != 64u) {
		return false;
	}
	for (const char ch : sv) {
		const bool is_digit = (ch >= '0' && ch <= '9');
		const bool is_lower_hex = (ch >= 'a' && ch <= 'f');
		if (!is_digit && !is_lower_hex) {
			return false;
		}
	}
	return true;
}

bool is_safe_relative_corpus_path(std::string_view path) noexcept
{
	if (path.empty() || path.front() == '/' || path.back() == '/') {
		return false;
	}
	if (path == "README.md" || path == "PROVENANCE.md") {
		return false;
	}
	for (const char ch : path) {
		if (ch == '\\' || ch == ':' || static_cast<unsigned char>(ch) < 0x20u) {
			return false;
		}
	}
	size_t start = 0;
	while (start <= path.size()) {
		const size_t slash = path.find('/', start);
		const std::string_view seg =
			(slash == std::string_view::npos) ? path.substr(start) : path.substr(start, slash - start);
		if (seg.empty() || seg == "." || seg == "..") {
			return false;
		}
		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1u;
	}
	return true;
}

std::vector<std::string_view> split_markdown_table_row(std::string_view line, bool* out_valid_delimiters)
{
	std::vector<std::string_view> cells;
	const std::string_view trimmed = trim_ascii(line);
	if (trimmed.size() < 2u || trimmed.front() != '|' || trimmed.back() != '|') {
		*out_valid_delimiters = false;
		return cells;
	}
	*out_valid_delimiters = true;
	const std::string_view inner = trimmed.substr(1u, trimmed.size() - 2u);
	size_t pos = 0;
	while (pos <= inner.size()) {
		const size_t bar = inner.find('|', pos);
		if (bar == std::string_view::npos) {
			cells.push_back(trim_ascii(inner.substr(pos)));
			break;
		}
		cells.push_back(trim_ascii(inner.substr(pos, bar - pos)));
		pos = bar + 1u;
	}
	return cells;
}

bool is_separator_cell(std::string_view cell) noexcept
{
	if (cell.size() < 3u) {
		return false;
	}
	size_t dashes = 0;
	for (const char ch : cell) {
		if (ch == '-') {
			++dashes;
		} else if (ch != ':') {
			return false;
		}
	}
	return dashes >= 3u;
}

ProvenanceParseResult parse_provenance_markdown(std::string_view markdown)
{
	ProvenanceParseResult result;
	if (markdown.starts_with("\xEF\xBB\xBF")) {
		markdown.remove_prefix(3u);
	}

	enum class State {
		BeforeHeader,
		ExpectSeparator,
		InRows,
		AfterTable,
	};

	State state = State::BeforeHeader;
	std::map<std::string, size_t> seen_paths;
	size_t line_no = 0;
	size_t pos = 0;

	while (pos <= markdown.size()) {
		const size_t nl = markdown.find('\n', pos);
		std::string_view raw_line =
			(nl == std::string_view::npos) ? markdown.substr(pos) : markdown.substr(pos, nl - pos);
		if (!raw_line.empty() && raw_line.back() == '\r') {
			raw_line.remove_suffix(1u);
		}
		++line_no;

		const std::string_view trimmed = trim_ascii(raw_line);
		if (state == State::BeforeHeader) {
			if (trimmed.starts_with('|')) {
				bool valid_delim = false;
				const auto cells = split_markdown_table_row(trimmed, &valid_delim);
				if (!valid_delim || cells.size() != 5u || cells[0] != "Path" ||
					cells[1] != "SHA-256" || cells[2] != "License" || cells[3] != "Origin" ||
					cells[4] != "Purpose") {
					result.errors.push_back(
						"line " + std::to_string(line_no) +
						": invalid PROVENANCE.md table header (expected '| Path | SHA-256 | License | Origin | Purpose |')");
				}
				state = State::ExpectSeparator;
			}
		} else if (state == State::ExpectSeparator) {
			bool valid_delim = false;
			const auto cells = split_markdown_table_row(trimmed, &valid_delim);
			bool all_sep = valid_delim && (cells.size() == 5u);
			if (all_sep) {
				for (const auto cell : cells) {
					if (!is_separator_cell(cell)) {
						all_sep = false;
						break;
					}
				}
			}
			if (!all_sep) {
				result.errors.push_back(
					"line " + std::to_string(line_no) +
					": invalid PROVENANCE.md table separator row");
			}
			state = State::InRows;
		} else if (state == State::InRows) {
			if (trimmed.empty()) {
				state = State::AfterTable;
			} else {
				bool valid_delim = false;
				const auto cells = split_markdown_table_row(trimmed, &valid_delim);
				if (!valid_delim || cells.size() != 5u) {
					result.errors.push_back(
						"line " + std::to_string(line_no) +
						": malformed table row (expected 5 pipe-delimited columns)");
				} else {
					ProvenanceRow row{
						.path = std::string(cells[0]),
						.sha256 = std::string(cells[1]),
						.license = std::string(cells[2]),
						.origin = std::string(cells[3]),
						.purpose = std::string(cells[4]),
						.line_number = line_no,
					};

					if (!is_safe_relative_corpus_path(row.path)) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + ": invalid corpus path '" +
							row.path + "'");
					} else if (const auto [it, inserted] = seen_paths.emplace(row.path, line_no);
							   !inserted) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + ": duplicate corpus path '" +
							row.path + "' (first seen on line " + std::to_string(it->second) + ")");
					}

					if (!is_valid_sha256_hex(row.sha256)) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + " (" + row.path +
							"): malformed SHA-256 hash '" + row.sha256 + "'");
					}

					if (row.license.empty()) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + " (" + row.path +
							"): empty license cell");
					} else if (row.license != "CC0-1.0") {
						result.errors.push_back(
							"line " + std::to_string(line_no) + " (" + row.path +
							"): unapproved license '" + row.license + "' (expected CC0-1.0)");
					}

					if (row.origin.empty()) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + " (" + row.path +
							"): empty origin cell");
					}

					if (row.purpose.empty()) {
						result.errors.push_back(
							"line " + std::to_string(line_no) + " (" + row.path +
							"): empty purpose cell");
					}

					result.rows.push_back(std::move(row));
				}
			}
		} else if (state == State::AfterTable) {
			if (!trimmed.empty()) {
				result.errors.push_back(
					"line " + std::to_string(line_no) +
					": unexpected trailing content after PROVENANCE.md table");
			}
		}

		if (nl == std::string_view::npos) {
			break;
		}
		pos = nl + 1u;
	}

	if (state == State::BeforeHeader || state == State::ExpectSeparator) {
		result.errors.push_back("PROVENANCE.md missing valid markdown table header");
	}
	if (result.rows.empty()) {
		result.errors.push_back("PROVENANCE.md contains zero corpus rows");
	}

	result.ok = result.errors.empty() && !result.rows.empty();
	return result;
}

CorpusValidationResult validate_corpus_directory(const std::filesystem::path& corpus_dir)
{
	CorpusValidationResult result;
	const std::filesystem::path long_root = to_long_path(corpus_dir);
	std::error_code ec;
	if (!std::filesystem::is_directory(long_root, ec)) {
		result.errors.push_back("corpus directory does not exist: " + corpus_dir.string());
		return result;
	}

	const std::filesystem::path provenance_path = corpus_dir / "PROVENANCE.md";
	if (!std::filesystem::is_regular_file(to_long_path(provenance_path), ec)) {
		result.errors.push_back("missing PROVENANCE.md in corpus directory: " + provenance_path.string());
		return result;
	}

	std::string markdown_text;
	try {
		const auto bytes = read_file_bytes(provenance_path);
		markdown_text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	} catch (const std::exception& ex) {
		result.errors.push_back(std::string("failed to read PROVENANCE.md: ") + ex.what());
		return result;
	}

	const ProvenanceParseResult parsed = parse_provenance_markdown(markdown_text);
	result.errors.insert(result.errors.end(), parsed.errors.begin(), parsed.errors.end());

	std::map<std::string, ProvenanceRow> expected_by_path;
	for (const auto& row : parsed.rows) {
		if (!row.path.empty()) {
			expected_by_path.emplace(row.path, row);
		}
	}

	std::map<std::string, std::filesystem::path> actual_files;
	for (std::filesystem::recursive_directory_iterator it(long_root, ec), end;
		 !ec && it != end;
		 it.increment(ec)) {
		const auto& entry = *it;
		if (entry.is_directory(ec)) {
			continue;
		}
		const std::string rel = entry.path().lexically_relative(long_root).generic_string();
		if (!entry.is_regular_file(ec)) {
			result.errors.push_back("non-regular file entry in corpus directory: " + rel);
			continue;
		}
		if (rel == "README.md" || rel == "PROVENANCE.md") {
			continue;
		}
		actual_files.emplace(rel, entry.path());
	}
	if (ec) {
		result.errors.push_back("error iterating corpus directory: " + ec.message());
	}

	for (const auto& [rel_path, row] : expected_by_path) {
		const auto actual_it = actual_files.find(rel_path);
		if (actual_it == actual_files.end()) {
			result.errors.push_back("missing corpus file listed in PROVENANCE.md: " + rel_path);
			continue;
		}
		try {
			const std::string actual_sha256 = sha256_file(actual_it->second);
			if (actual_sha256 != row.sha256) {
				result.errors.push_back(
					"SHA-256 mismatch for " + rel_path + ": expected " + row.sha256 + ", got " +
					actual_sha256);
			} else {
				++result.verified_file_count;
			}
		} catch (const std::exception& ex) {
			result.errors.push_back(
				"failed to hash corpus file " + rel_path + ": " + std::string(ex.what()));
		}
	}

	for (const auto& [rel_path, disk_path] : actual_files) {
		(void)disk_path;
		if (expected_by_path.find(rel_path) == expected_by_path.end()) {
			result.errors.push_back(
				"unlisted file in corpus directory (missing from PROVENANCE.md): " + rel_path);
		}
	}

	result.ok = result.errors.empty() && !expected_by_path.empty() &&
		(result.verified_file_count == expected_by_path.size());
	return result;
}

std::string join_errors(const std::vector<std::string>& errors)
{
	std::ostringstream oss;
	for (size_t i = 0; i < errors.size(); ++i) {
		if (i > 0) {
			oss << "\n  ";
		}
		oss << errors[i];
	}
	return oss.str();
}

bool contains_ErrorSubstring(const std::vector<std::string>& errors, std::string_view needle)
{
	return std::any_of(errors.begin(), errors.end(), [needle](const std::string& err) {
		return err.find(needle) != std::string::npos;
	});
}

void copy_live_corpus_to_temp(const TempDir& temp_dir)
{
	const std::filesystem::path src_root = to_long_path(corpus_path());
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(src_root, ec), end;
		 !ec && it != end;
		 it.increment(ec)) {
		if (!it->is_regular_file(ec)) {
			continue;
		}
		const std::string rel = it->path().lexically_relative(src_root).generic_string();
		const auto bytes = read_file_bytes(it->path());
		write_file_bytes(temp_dir.file_path(rel), bytes);
	}
	ASSERT_FALSE(ec) << "Failed to copy live corpus to TempDir: " << ec.message();
}

TEST(Corpus, Provenance)
{
	const std::filesystem::path root = corpus_path();
	const CorpusValidationResult res = validate_corpus_directory(root);
	ASSERT_TRUE(res.ok) << "Corpus provenance check failed for " << root.string() << ":\n  "
						<< join_errors(res.errors);
	EXPECT_TRUE(res.errors.empty());
	EXPECT_GE(res.verified_file_count, 39u);
	RecordProperty("verified_file_count", static_cast<int>(res.verified_file_count));
	std::cout << "[          ] Corpus.Provenance verified " << res.verified_file_count
			  << " corpus files in " << root.string() << "\n";
}

TEST(Corpus, DetectsUnlistedAndCorruptedFiles)
{
	TempDir temp_corpus;
	copy_live_corpus_to_temp(temp_corpus);

	const CorpusValidationResult baseline = validate_corpus_directory(temp_corpus.path());
	ASSERT_TRUE(baseline.ok) << "Baseline copy should pass provenance check:\n  "
							 << join_errors(baseline.errors);

	// Plant an unlisted dummy file and a 1-byte mutation in an existing file.
	const std::string unlisted_rel = "render/unlisted-dummy.pdf";
	const std::string mutated_rel = "render/simple-1p.pdf";
	const std::array<uint8_t, 9> dummy_bytes{'%', 'P', 'D', 'F', '-', '1', '.', '7', '\n'};
	write_file_bytes(temp_corpus.file_path(unlisted_rel), dummy_bytes);

	auto simple_bytes = read_file_bytes(temp_corpus.file_path(mutated_rel));
	ASSERT_FALSE(simple_bytes.empty());
	simple_bytes[simple_bytes.size() / 2u] ^= 0x01u;
	write_file_bytes(temp_corpus.file_path(mutated_rel), simple_bytes);

	const CorpusValidationResult faulted = validate_corpus_directory(temp_corpus.path());
	EXPECT_FALSE(faulted.ok);
	EXPECT_TRUE(contains_ErrorSubstring(faulted.errors, unlisted_rel))
		<< "Expected unlisted file error naming " << unlisted_rel << ", got:\n  "
		<< join_errors(faulted.errors);
	EXPECT_TRUE(contains_ErrorSubstring(faulted.errors, mutated_rel))
		<< "Expected SHA-256 mismatch error naming " << mutated_rel << ", got:\n  "
		<< join_errors(faulted.errors);
}

TEST(Corpus, RejectsUnlistedOrModifiedFile)
{
	// Case 1: Only an unlisted file in the corpus directory.
	{
		TempDir temp_corpus;
		copy_live_corpus_to_temp(temp_corpus);
		const std::array<uint8_t, 4> stray{'t', 'e', 's', 't'};
		write_file_bytes(temp_corpus.file_path("seeds/stray-seed.bin"), stray);

		const CorpusValidationResult res = validate_corpus_directory(temp_corpus.path());
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "seeds/stray-seed.bin"))
			<< join_errors(res.errors);
	}

	// Case 2: Only a 1-byte modified file in the corpus directory.
	{
		TempDir temp_corpus;
		copy_live_corpus_to_temp(temp_corpus);
		const std::string target_rel = "convert/tables-and-forms.pdf";
		auto bytes = read_file_bytes(temp_corpus.file_path(target_rel));
		ASSERT_FALSE(bytes.empty());
		bytes.back() ^= 0xFFu;
		write_file_bytes(temp_corpus.file_path(target_rel), bytes);

		const CorpusValidationResult res = validate_corpus_directory(temp_corpus.path());
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, target_rel)) << join_errors(res.errors);
	}

	// Case 3: Missing listed file in the corpus directory.
	{
		TempDir temp_corpus;
		copy_live_corpus_to_temp(temp_corpus);
		const std::string missing_rel = "text/scanned.pdf";
		std::error_code ec;
		ASSERT_TRUE(std::filesystem::remove(to_long_path(temp_corpus.file_path(missing_rel)), ec));

		const CorpusValidationResult res = validate_corpus_directory(temp_corpus.path());
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, missing_rel)) << join_errors(res.errors);
	}
}

TEST(Corpus, ProvenanceParserRejectsMalformedRows)
{
	constexpr std::string_view kValidHeader =
		"# Provenance\n\n"
		"| Path | SHA-256 | License | Origin | Purpose |\n"
		"| ---- | ------- | ------- | ------ | ------- |\n";
	constexpr std::string_view kValidHash =
		"23dcc98e2c084fdb9505d807c37706d4f1f097dbaf6b3b23b401e245f494f4bd";

	// Valid single row passes.
	{
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " +
			std::string(kValidHash) + " | CC0-1.0 | scripts/build-corpus.py | Test fixture |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_TRUE(res.ok) << join_errors(res.errors);
		ASSERT_EQ(res.rows.size(), 1u);
		EXPECT_EQ(res.rows[0].path, "render/simple-1p.pdf");
		EXPECT_EQ(res.rows[0].sha256, kValidHash);
		EXPECT_EQ(res.rows[0].license, "CC0-1.0");
	}

	// Empty license cell is rejected.
	{
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " +
			std::string(kValidHash) + " |   | scripts/build-corpus.py | Test fixture |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "empty license cell"))
			<< join_errors(res.errors);
	}

	// Non-CC0-1.0 license cell is rejected.
	{
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " +
			std::string(kValidHash) + " | Proprietary | scripts/build-corpus.py | Test fixture |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "unapproved license"))
			<< join_errors(res.errors);
	}

	// Short SHA-256 hash (63 chars) is rejected.
	{
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " +
			std::string(kValidHash.substr(0, 63)) +
			" | CC0-1.0 | scripts/build-corpus.py | Test fixture |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "malformed SHA-256 hash"))
			<< join_errors(res.errors);
	}

	// Non-hex or uppercase character in SHA-256 hash is rejected.
	{
		std::string bad_hash(kValidHash);
		bad_hash[0] = 'G';
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " + bad_hash +
			" | CC0-1.0 | scripts/build-corpus.py | Test fixture |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "malformed SHA-256 hash"))
			<< join_errors(res.errors);
	}

	// Duplicate path row and path traversal are rejected.
	{
		const std::string md = std::string(kValidHeader) + "| render/simple-1p.pdf | " +
			std::string(kValidHash) + " | CC0-1.0 | scripts/build-corpus.py | First |\n" +
			"| render/simple-1p.pdf | " + std::string(kValidHash) +
			" | CC0-1.0 | scripts/build-corpus.py | Duplicate |\n" + "| ../outside.pdf | " +
			std::string(kValidHash) + " | CC0-1.0 | scripts/build-corpus.py | Traversal |\n";
		const auto res = parse_provenance_markdown(md);
		EXPECT_FALSE(res.ok);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "duplicate corpus path"))
			<< join_errors(res.errors);
		EXPECT_TRUE(contains_ErrorSubstring(res.errors, "invalid corpus path"))
			<< join_errors(res.errors);
	}
}

} // namespace
} // namespace recto::test
