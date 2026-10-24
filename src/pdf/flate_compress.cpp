// Native Flate compressor: RFC 1950 zlib streams of RFC 1951 deflate blocks (D01 T02 section 34). LZ77 over a 32 KiB
// window through hash chains, then each block of up to 8,192 symbols written stored, fixed-Huffman or
// dynamic-Huffman, whichever costs fewest bits.
#include "filters.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <new>
#include <optional>

namespace recto::pdf {

namespace {

thread_local bool t_fail_compress_alloc = false;

constexpr size_t kWindow = 32'768u;
constexpr size_t kWindowMask = kWindow - 1u;
constexpr unsigned kHashBits = 15u;
constexpr size_t kHashSize = size_t{1} << kHashBits;
constexpr uint32_t kNoPosition = UINT32_MAX;
constexpr size_t kMinMatch = 3u;
constexpr size_t kMaxMatch = 258u;
constexpr size_t kBlockSymbols = 8'192u;
constexpr size_t kMaxStoredBlock = 65'535u;
constexpr size_t kLitLenCodes = 286u; // 0..255 literals, 256 end of block, 257..285 lengths
constexpr size_t kDistCodes = 30u;
constexpr size_t kCodeLengthCodes = 19u;
constexpr unsigned kMaxCodeBits = 15u;
constexpr unsigned kMaxCodeLengthBits = 7u;
constexpr uint16_t kEndOfBlock = 256u;

struct LevelParams {
	uint32_t chain;  // hash-chain links followed per position, at most
	size_t lazy;     // try the next position while the current match is shorter than this (0: never)
	size_t nice;     // stop searching at a match this long
	size_t good;     // follow a quarter of the chain when the held match is already this long (0: never)
	uint8_t flevel;  // RFC 1950 FLEVEL
};

[[nodiscard]] constexpr LevelParams params_for(FlateLevel level) noexcept
{
	switch (level) {
	case FlateLevel::Fast:
		return {8u, 0u, 16u, 0u, 1u};
	case FlateLevel::Default:
		return {32u, 32u, 128u, 8u, 2u};
	case FlateLevel::Best:
		return {1'024u, 258u, 258u, 32u, 3u};
	}
	return {32u, 32u, 128u, 8u, 2u};
}

// RFC 1951 3.2.5: the base and extra bits of each length code (257..285) and distance code (0..29).
constexpr std::array<uint16_t, 29> kLengthBase = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// RFC 1951 3.2.7: the order code-length code lengths are sent in.
constexpr std::array<uint8_t, kCodeLengthCodes> kCodeLengthOrder = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// The length code index (0..28) of every match length 3..258.
constexpr std::array<uint8_t, kMaxMatch + 1u> kLengthCodeOf = [] {
	std::array<uint8_t, kMaxMatch + 1u> table{};
	size_t code = 0;
	for (size_t len = kMinMatch; len <= kMaxMatch; ++len) {
		while (code + 1u < kLengthBase.size() && kLengthBase[code + 1u] <= len) {
			++code;
		}
		table[len] = static_cast<uint8_t>(code);
	}
	return table;
}();

// The eight-byte match comparison counts equal bytes from the low end.
static_assert(std::endian::native == std::endian::little);

// The distance code of every distance, in two tables as zlib keeps them: distances 1 to 256 directly, and larger
// ones by `(distance - 1) >> 7` (the codes from 16 up cover whole multiples of 128).
constexpr std::array<uint8_t, 512> kDistCodeTable = [] {
	std::array<uint8_t, 512> table{};
	const auto code_of = [](size_t dist) {
		size_t code = 0;
		while (code + 1u < kDistBase.size() && kDistBase[code + 1u] <= dist) {
			++code;
		}
		return static_cast<uint8_t>(code);
	};
	for (size_t d = 1; d <= 256u; ++d) {
		table[d - 1u] = code_of(d);
	}
	for (size_t hi = 2; hi < 256u; ++hi) {
		table[256u + hi] = code_of((hi << 7u) + 1u);
	}
	return table;
}();

[[nodiscard]] uint8_t dist_code_of(size_t dist) noexcept
{
	return dist <= 256u ? kDistCodeTable[dist - 1u] : kDistCodeTable[256u + ((dist - 1u) >> 7u)];
}

// One LZ77 symbol: a literal byte (`dist == 0`) or a match of `len` bytes `dist` back.
struct Symbol {
	uint16_t len_or_literal;
	uint16_t dist;
};

class BitWriter {
public:
	explicit BitWriter(std::vector<uint8_t>& out) noexcept
		: out_(out)
	{
	}

	void put(uint32_t bits, unsigned count)
	{
		buffer_ |= static_cast<uint64_t>(bits) << pending_;
		pending_ += count;
		while (pending_ >= 8u) {
			out_.push_back(static_cast<uint8_t>(buffer_));
			buffer_ >>= 8u;
			pending_ -= 8u;
		}
	}

	void align()
	{
		if (pending_ != 0u) {
			out_.push_back(static_cast<uint8_t>(buffer_));
			buffer_ = 0;
			pending_ = 0;
		}
	}

	[[nodiscard]] unsigned pending_bits() const noexcept
	{
		return pending_;
	}

private:
	std::vector<uint8_t>& out_;
	uint64_t buffer_{0};
	unsigned pending_{0};
};

// Code lengths for `freq` limited to `limit` bits: Huffman lengths by the in-place method of Moffat and Katajainen,
// then the length counts adjusted until the code is complete within the limit (the counts method), lengths handed
// out longest to the least frequent. Symbols of zero frequency get length 0. The caller gives at least two symbols a
// non-zero frequency, so the code is always complete.
void build_lengths(std::span<const uint32_t> freq, unsigned limit, std::span<uint8_t> lengths)
{
	std::fill(lengths.begin(), lengths.end(), uint8_t{0});
	std::array<std::pair<uint32_t, uint16_t>, kLitLenCodes> used{};
	size_t n = 0;
	for (size_t s = 0; s < freq.size(); ++s) {
		if (freq[s] != 0u) {
			used[n++] = {freq[s], static_cast<uint16_t>(s)};
		}
	}
	if (n == 0u) {
		return;
	}
	if (n == 1u) {
		lengths[used[0].second] = 1u;
		return;
	}
	std::sort(used.begin(), used.begin() + static_cast<ptrdiff_t>(n));
	std::array<int64_t, kLitLenCodes> a{};
	for (size_t i = 0; i < n; ++i) {
		a[i] = used[i].first;
	}
	// Moffat and Katajainen, "In-Place Calculation of Minimum-Redundancy Codes" (1995).
	{
		const int64_t count = static_cast<int64_t>(n);
		a[0] += a[1];
		int64_t root = 0;
		int64_t leaf = 2;
		for (int64_t next = 1; next < count - 1; ++next) {
			if (leaf >= count || a[root] < a[leaf]) {
				a[next] = a[root];
				a[root++] = next;
			} else {
				a[next] = a[leaf++];
			}
			if (leaf >= count || (root < next && a[root] < a[leaf])) {
				a[next] += a[root];
				a[root++] = next;
			} else {
				a[next] += a[leaf++];
			}
		}
		a[count - 2] = 0;
		for (int64_t next = count - 3; next >= 0; --next) {
			a[next] = a[a[next]] + 1;
		}
		int64_t avail = 1;
		int64_t used_nodes = 0;
		int64_t depth = 0;
		root = count - 2;
		int64_t next = count - 1;
		while (avail > 0) {
			while (root >= 0 && a[root] == depth) {
				++used_nodes;
				--root;
			}
			while (avail > used_nodes) {
				a[next--] = depth;
				--avail;
			}
			avail = 2 * used_nodes;
			++depth;
			used_nodes = 0;
		}
	}
	// a[i] is now the length of the i-th least frequent symbol. Count the lengths, fold those past the limit into it,
	// and make the code complete again (Kraft sum exactly 1) by lengthening the deepest codes under the limit.
	std::array<uint32_t, 64> counts{};
	for (size_t i = 0; i < n; ++i) {
		counts[static_cast<size_t>(std::min<int64_t>(a[i], 63))]++;
	}
	for (size_t len = limit + 1u; len < counts.size(); ++len) {
		counts[limit] += counts[len];
		counts[len] = 0;
	}
	uint64_t kraft = 0;
	for (size_t len = 1; len <= limit; ++len) {
		kraft += static_cast<uint64_t>(counts[len]) << (limit - len);
	}
	while (kraft != (uint64_t{1} << limit)) {
		--counts[limit];
		for (size_t len = limit - 1u; len > 0u; --len) {
			if (counts[len] != 0u) {
				--counts[len];
				counts[len + 1u] += 2u;
				break;
			}
		}
		--kraft;
	}
	// Longest lengths to the least frequent symbols (`used` is in ascending frequency).
	size_t i = 0;
	for (size_t len = limit; len > 0u; --len) {
		for (uint32_t k = 0; k < counts[len]; ++k) {
			lengths[used[i++].second] = static_cast<uint8_t>(len);
		}
	}
}

// Canonical codes (RFC 1951 3.2.2), bit-reversed for the LSB-first bit order deflate writes.
void build_codes(std::span<const uint8_t> lengths, std::span<uint16_t> codes)
{
	std::array<uint16_t, kMaxCodeBits + 2u> bl_count{};
	for (const uint8_t len : lengths) {
		bl_count[len]++;
	}
	bl_count[0] = 0;
	std::array<uint16_t, kMaxCodeBits + 2u> next_code{};
	uint16_t code = 0;
	for (size_t bits = 1; bits <= kMaxCodeBits; ++bits) {
		code = static_cast<uint16_t>((code + bl_count[bits - 1u]) << 1u);
		next_code[bits] = code;
	}
	for (size_t s = 0; s < lengths.size(); ++s) {
		const uint8_t len = lengths[s];
		if (len == 0u) {
			codes[s] = 0;
			continue;
		}
		uint16_t c = next_code[len]++;
		uint16_t reversed = 0;
		for (uint8_t b = 0; b < len; ++b) {
			reversed = static_cast<uint16_t>((reversed << 1u) | (c & 1u));
			c = static_cast<uint16_t>(c >> 1u);
		}
		codes[s] = reversed;
	}
}

// One symbol of the code-length alphabet (RFC 1951 3.2.7) with its extra bits.
struct CodeLengthSymbol {
	uint8_t symbol;
	uint8_t extra;
};

// A dynamic block's trees and header, built from one block's frequencies.
struct DynamicTrees {
	std::array<uint8_t, kLitLenCodes> lit_lengths{};
	std::array<uint8_t, kDistCodes> dist_lengths{};
	std::array<uint16_t, kLitLenCodes> lit_codes{};
	std::array<uint16_t, kDistCodes> dist_codes{};
	std::array<uint8_t, kCodeLengthCodes> cl_lengths{};
	std::array<uint16_t, kCodeLengthCodes> cl_codes{};
	std::array<CodeLengthSymbol, kLitLenCodes + kDistCodes> cl_symbols{};
	size_t cl_count{0};
	size_t hlit{257};
	size_t hdist{1};
	size_t hclen{4};
	uint64_t header_bits{0};
};

void build_dynamic(const std::array<uint32_t, kLitLenCodes>& lit_freq, const std::array<uint32_t, kDistCodes>& dist_freq,
	DynamicTrees& trees)
{
	build_lengths(lit_freq, kMaxCodeBits, trees.lit_lengths);
	build_lengths(dist_freq, kMaxCodeBits, trees.dist_lengths);
	build_codes(trees.lit_lengths, trees.lit_codes);
	build_codes(trees.dist_lengths, trees.dist_codes);
	trees.hlit = 257u;
	for (size_t s = kLitLenCodes; s > 257u; --s) {
		if (trees.lit_lengths[s - 1u] != 0u) {
			trees.hlit = s;
			break;
		}
	}
	trees.hdist = 1u;
	for (size_t s = kDistCodes; s > 1u; --s) {
		if (trees.dist_lengths[s - 1u] != 0u) {
			trees.hdist = s;
			break;
		}
	}
	// The literal/length and distance lengths, run-length coded with 16 (repeat 3 to 6), 17 (3 to 10 zeros) and 18
	// (11 to 138 zeros).
	std::array<uint8_t, kLitLenCodes + kDistCodes> all{};
	size_t total = 0;
	for (size_t s = 0; s < trees.hlit; ++s) {
		all[total++] = trees.lit_lengths[s];
	}
	for (size_t s = 0; s < trees.hdist; ++s) {
		all[total++] = trees.dist_lengths[s];
	}
	trees.cl_count = 0;
	std::array<uint32_t, kCodeLengthCodes> cl_freq{};
	const auto emit = [&](uint8_t symbol, uint8_t extra) {
		trees.cl_symbols[trees.cl_count++] = {symbol, extra};
		cl_freq[symbol]++;
	};
	for (size_t i = 0; i < total;) {
		const uint8_t value = all[i];
		size_t run = 1;
		while (i + run < total && all[i + run] == value) {
			++run;
		}
		i += run;
		if (value == 0u) {
			while (run >= 11u) {
				const size_t take = std::min<size_t>(run, 138u);
				emit(18u, static_cast<uint8_t>(take - 11u));
				run -= take;
			}
			if (run >= 3u) {
				emit(17u, static_cast<uint8_t>(run - 3u));
				run = 0;
			}
		} else {
			emit(value, 0u);
			--run;
			while (run >= 3u) {
				const size_t take = std::min<size_t>(run, 6u);
				emit(16u, static_cast<uint8_t>(take - 3u));
				run -= take;
			}
		}
		for (; run > 0u; --run) {
			emit(value, 0u);
		}
	}
	// A code needs two symbols to be complete; give an unused one a count if only one is used.
	if (std::count_if(cl_freq.begin(), cl_freq.end(), [](uint32_t f) { return f != 0u; }) < 2) {
		cl_freq[cl_freq[0] == 0u ? 0u : 18u]++;
	}
	build_lengths(cl_freq, kMaxCodeLengthBits, trees.cl_lengths);
	build_codes(trees.cl_lengths, trees.cl_codes);
	trees.hclen = kCodeLengthCodes;
	while (trees.hclen > 4u && trees.cl_lengths[kCodeLengthOrder[trees.hclen - 1u]] == 0u) {
		--trees.hclen;
	}
	uint64_t bits = 5u + 5u + 4u + 3u * trees.hclen;
	for (size_t i = 0; i < trees.cl_count; ++i) {
		const CodeLengthSymbol& c = trees.cl_symbols[i];
		bits += trees.cl_lengths[c.symbol] + (c.symbol == 16u ? 2u : c.symbol == 17u ? 3u : c.symbol == 18u ? 7u : 0u);
	}
	trees.header_bits = bits;
}

// RFC 1951 3.2.6: the fixed literal/length and distance code lengths.
constexpr std::array<uint8_t, kLitLenCodes> kFixedLitLengths = [] {
	std::array<uint8_t, kLitLenCodes> lengths{};
	for (size_t s = 0; s < kLitLenCodes; ++s) {
		lengths[s] = s < 144u ? 8u : s < 256u ? 9u : s < 280u ? 7u : 8u;
	}
	return lengths;
}();
constexpr std::array<uint8_t, kDistCodes> kFixedDistLengths = [] {
	std::array<uint8_t, kDistCodes> lengths{};
	lengths.fill(5u);
	return lengths;
}();

// The bits a block's symbols take with the given lengths, extra bits included.
[[nodiscard]] uint64_t symbol_bits(const std::array<uint32_t, kLitLenCodes>& lit_freq,
	const std::array<uint32_t, kDistCodes>& dist_freq, std::span<const uint8_t> lit_lengths,
	std::span<const uint8_t> dist_lengths) noexcept
{
	uint64_t bits = 0;
	for (size_t s = 0; s < kLitLenCodes; ++s) {
		bits += static_cast<uint64_t>(lit_freq[s]) * (lit_lengths[s] + (s >= 257u ? kLengthExtra[s - 257u] : 0u));
	}
	for (size_t d = 0; d < kDistCodes; ++d) {
		bits += static_cast<uint64_t>(dist_freq[d]) * (dist_lengths[d] + kDistExtra[d]);
	}
	return bits;
}

class Compressor {
public:
	Compressor(std::span<const uint8_t> input, FlateLevel level, std::vector<uint8_t>& out, FlateCompressStats* stats)
		: input_(input)
		, params_(params_for(level))
		, out_(out)
		, bits_(out)
		, stats_(stats)
		, head_(kHashSize, kNoPosition)
		, prev_(kWindow, uint16_t{0})
	{
		symbols_.reserve(kBlockSymbols);
	}

	void run()
	{
		// RFC 1950 2.2: CM 8 with a 32 KiB window, FLEVEL, and FCHECK making CMF*256+FLG a multiple of 31.
		const uint8_t cmf = 0x78u;
		uint8_t flg = static_cast<uint8_t>(params_.flevel << 6u);
		flg = static_cast<uint8_t>(flg + (31u - ((cmf * 256u + flg) % 31u)) % 31u);
		out_.push_back(cmf);
		out_.push_back(flg);
		size_t pos = 0;
		do {
			const size_t start = pos;
			symbols_.clear();
			pos = params_.lazy == 0u ? parse_greedy(pos) : parse_lazy(pos);
			write_block(start, pos, pos == input_.size());
		} while (pos < input_.size());
		const uint32_t adler = adler32();
		for (int shift = 24; shift >= 0; shift -= 8) {
			out_.push_back(static_cast<uint8_t>(adler >> shift));
		}
		if (stats_ != nullptr) {
			stats_->chain_steps = chain_steps_;
			stats_->max_chain_steps = max_chain_steps_;
			stats_->working_bytes = head_.capacity() * sizeof(uint32_t) + prev_.capacity() * sizeof(uint16_t) +
				symbols_.capacity() * sizeof(Symbol) + sizeof(DynamicTrees) +
				2u * (sizeof(uint32_t) * (kLitLenCodes + kDistCodes));
		}
	}

private:
	[[nodiscard]] uint32_t hash_at(size_t p) const noexcept
	{
		const uint32_t v = static_cast<uint32_t>(input_[p]) | (static_cast<uint32_t>(input_[p + 1u]) << 8u) |
			(static_cast<uint32_t>(input_[p + 2u]) << 16u);
		return (v * 2'654'435'761u) >> (32u - kHashBits);
	}

	void insert(size_t p) noexcept
	{
		if (p + kMinMatch <= input_.size()) {
			insert(p, hash_at(p));
		}
	}

	void insert(size_t p, uint32_t h) noexcept
	{
		const uint32_t old = head_[h];
		prev_[p & kWindowMask] = old != kNoPosition && p - old <= kWindow ? static_cast<uint16_t>(p - old) : uint16_t{0};
		head_[h] = static_cast<uint32_t>(p);
	}

	// The longest match at `p` longer than `beat` within the level's chain budget, as (length, distance), or a
	// length of 0.
	// `h` is `hash_at(p)`; the caller inserts `p` with it after the search.
	[[nodiscard]] std::pair<size_t, size_t> longest_match(size_t p, size_t beat, uint32_t h) noexcept
	{
		const size_t max_len = std::min(kMaxMatch, input_.size() - p);
		size_t best = std::max(beat, kMinMatch - 1u);
		size_t best_dist = 0;
		if (best >= max_len) {
			return {0u, 0u};
		}
		const uint8_t* const base = input_.data();
		const uint8_t* const b = base + p;
		const uint16_t* const prev = prev_.data();
		const size_t limit = p > kWindow ? p - kWindow : 0u; // the oldest position a distance can reach
		// A match already good enough is worth a shorter search for a better one (as zlib's good_length).
		const uint32_t chain = params_.good != 0u && beat >= params_.good ? params_.chain / 4u : params_.chain;
		uint8_t end0 = b[best - 1u];
		uint8_t end1 = b[best];
		uint32_t cand = head_[h];
		uint32_t steps = 0;
		while (cand != kNoPosition && cand >= limit && steps < chain) {
			++steps;
			const uint8_t* a = base + cand;
			// The two bytes that would make a longer match first (zlib's scan_end), then the start.
			if (a[best] == end1 && a[best - 1u] == end0 && a[0] == b[0] && a[1] == b[1]) {
				// Eight bytes at a time while eight remain, then byte by byte.
				size_t len = 2;
				while (len + 8u <= max_len) {
					uint64_t x = 0;
					uint64_t y = 0;
					std::memcpy(&x, a + len, 8u);
					std::memcpy(&y, b + len, 8u);
					if (x != y) {
						len += static_cast<size_t>(std::countr_zero(x ^ y)) >> 3u;
						break;
					}
					len += 8u;
				}
				while (len < max_len && a[len] == b[len]) {
					++len;
				}
				if (len > best) {
					best = len;
					best_dist = p - cand;
					if (len >= params_.nice || len == max_len) {
						break;
					}
					end0 = b[best - 1u];
					end1 = b[best];
				}
			}
			const uint16_t delta = prev[cand & kWindowMask];
			if (delta == 0u || delta > cand) {
				break;
			}
			cand -= delta;
		}
		chain_steps_ += steps;
		max_chain_steps_ = std::max(max_chain_steps_, steps);
		return best_dist == 0u ? std::pair<size_t, size_t>{0u, 0u} : std::pair<size_t, size_t>{best, best_dist};
	}

	void literal(size_t p)
	{
		symbols_.push_back({input_[p], 0u});
	}

	void match(size_t len, size_t dist)
	{
		symbols_.push_back({static_cast<uint16_t>(len), static_cast<uint16_t>(dist)});
	}

	// Greedy parsing (`Fast`): take the longest match at each position, inserting the positions it covers.
	[[nodiscard]] size_t parse_greedy(size_t p)
	{
		while (p < input_.size() && symbols_.size() < kBlockSymbols) {
			std::pair<size_t, size_t> found{0u, 0u};
			if (p + kMinMatch <= input_.size()) {
				const uint32_t h = hash_at(p);
				found = longest_match(p, 0u, h);
				insert(p, h);
			}
			const auto [len, dist] = found;
			if (len >= kMinMatch) {
				match(len, dist);
				for (size_t q = p + 1u; q < p + len; ++q) {
					insert(q);
				}
				p += len;
			} else {
				literal(p);
				++p;
			}
		}
		return p;
	}

	// Lazy parsing (`Default`, `Best`): a match is held one position, and a longer match starting there replaces it
	// with a literal. Every position is searched at most once.
	[[nodiscard]] size_t parse_lazy(size_t p)
	{
		bool held = false;    // a literal or match is pending at p - 1
		size_t held_len = 0;  // its match length (0: none)
		size_t held_dist = 0;
		while (p < input_.size() && symbols_.size() + 1u < kBlockSymbols) {
			std::pair<size_t, size_t> found{0u, 0u};
			if (p + kMinMatch <= input_.size()) {
				const uint32_t h = hash_at(p);
				if (!(held && held_len >= params_.lazy)) {
					found = longest_match(p, held ? held_len : 0u, h);
				}
				insert(p, h);
			}
			if (held && held_len >= kMinMatch && found.first <= held_len) {
				// The held match at p - 1 stands: emit it and step over what it covers.
				match(held_len, held_dist);
				const size_t end = p - 1u + held_len;
				for (size_t q = p + 1u; q < end; ++q) {
					insert(q);
				}
				p = end;
				held = false;
				held_len = 0;
				continue;
			}
			if (held) {
				literal(p - 1u);
			}
			held = true;
			held_len = found.first;
			held_dist = found.second;
			++p;
		}
		if (held) {
			if (held_len >= kMinMatch) {
				match(held_len, held_dist);
				const size_t end = p - 1u + held_len;
				for (size_t q = p; q < end; ++q) {
					insert(q);
				}
				p = end;
			} else {
				literal(p - 1u);
			}
		}
		return p;
	}

	// Stored blocks for input [s, e), split at 65,535 bytes; an empty range is one empty block.
	void write_stored(size_t s, size_t e, bool final)
	{
		do {
			const size_t n = std::min(e - s, kMaxStoredBlock);
			const bool last_piece = s + n == e;
			bits_.put(final && last_piece ? 1u : 0u, 1u);
			bits_.put(0u, 2u);
			bits_.align();
			out_.push_back(static_cast<uint8_t>(n));
			out_.push_back(static_cast<uint8_t>(n >> 8u));
			out_.push_back(static_cast<uint8_t>(~n));
			out_.push_back(static_cast<uint8_t>(~n >> 8u));
			out_.insert(out_.end(), input_.begin() + static_cast<ptrdiff_t>(s), input_.begin() + static_cast<ptrdiff_t>(s + n));
			s += n;
			if (stats_ != nullptr) {
				stats_->stored_runs++;
			}
		} while (s < e);
	}

	void flush_stored(bool final)
	{
		if (stored_start_.has_value()) {
			write_stored(*stored_start_, stored_end_, final);
			stored_start_.reset();
		}
	}

	void write_symbols(std::span<const uint16_t> lit_codes, std::span<const uint8_t> lit_lengths,
		std::span<const uint16_t> dist_codes, std::span<const uint8_t> dist_lengths)
	{
		for (const Symbol& s : symbols_) {
			if (s.dist == 0u) {
				bits_.put(lit_codes[s.len_or_literal], lit_lengths[s.len_or_literal]);
				continue;
			}
			const uint8_t lc = kLengthCodeOf[s.len_or_literal];
			bits_.put(lit_codes[257u + lc], lit_lengths[257u + lc]);
			if (kLengthExtra[lc] != 0u) {
				bits_.put(static_cast<uint32_t>(s.len_or_literal - kLengthBase[lc]), kLengthExtra[lc]);
			}
			const uint8_t dc = dist_code_of(s.dist);
			bits_.put(dist_codes[dc], dist_lengths[dc]);
			if (kDistExtra[dc] != 0u) {
				bits_.put(static_cast<uint32_t>(s.dist - kDistBase[dc]), kDistExtra[dc]);
			}
		}
		bits_.put(lit_codes[kEndOfBlock], lit_lengths[kEndOfBlock]);
	}

	// Chooses and writes the block for input [start, end) and the symbols parsed from it.
	void write_block(size_t start, size_t end, bool final)
	{
		std::array<uint32_t, kLitLenCodes> lit_freq{};
		std::array<uint32_t, kDistCodes> dist_freq{};
		for (const Symbol& s : symbols_) {
			if (s.dist == 0u) {
				lit_freq[s.len_or_literal]++;
			} else {
				lit_freq[257u + kLengthCodeOf[s.len_or_literal]]++;
				dist_freq[dist_code_of(s.dist)]++;
			}
		}
		lit_freq[kEndOfBlock] = 1u;
		const std::array<uint32_t, kLitLenCodes> real_lit = lit_freq;
		const std::array<uint32_t, kDistCodes> real_dist = dist_freq;
		// Complete codes for any reader: at least two literal/length and two distance codes (RFC 1951 3.2.7 allows
		// fewer, which some readers refuse).
		if (std::count_if(lit_freq.begin(), lit_freq.end(), [](uint32_t f) { return f != 0u; }) < 2) {
			lit_freq[0]++;
		}
		for (size_t d = 0; std::count_if(dist_freq.begin(), dist_freq.end(), [](uint32_t f) { return f != 0u; }) < 2; ++d) {
			if (dist_freq[d] == 0u) {
				dist_freq[d] = 1u;
			}
		}
		build_dynamic(lit_freq, dist_freq, trees_);

		const uint64_t fixed_bits = 3u + symbol_bits(real_lit, real_dist, kFixedLitLengths, kFixedDistLengths);
		const uint64_t dynamic_bits =
			3u + trees_.header_bits + symbol_bits(real_lit, real_dist, trees_.lit_lengths, trees_.dist_lengths);
		// A stored block costs its bytes plus, per 65,535-byte piece it starts, the 3-bit header, the padding to a
		// byte and LEN and NLEN; consecutive stored blocks join one run, so only pieces a block adds are counted.
		const size_t bytes = end - start;
		uint64_t stored_bits = 8u * static_cast<uint64_t>(bytes);
		const auto pieces = [](size_t n) { return std::max<size_t>(1u, (n + kMaxStoredBlock - 1u) / kMaxStoredBlock); };
		if (stored_start_.has_value()) {
			const size_t before = stored_end_ - *stored_start_;
			stored_bits += 40u * (pieces(before + bytes) - pieces(before));
		} else {
			const unsigned pad = (8u - ((bits_.pending_bits() + 3u) % 8u)) % 8u;
			stored_bits += 3u + pad + 32u + 40u * (pieces(bytes) - 1u);
		}

		FlateBlockType type = FlateBlockType::Stored;
		if (fixed_bits < stored_bits && fixed_bits <= dynamic_bits) {
			type = FlateBlockType::Fixed;
		} else if (dynamic_bits < stored_bits && dynamic_bits < fixed_bits) {
			type = FlateBlockType::Dynamic;
		}
		if (stats_ != nullptr) {
			stats_->blocks.push_back({type, stored_bits, fixed_bits, dynamic_bits, bytes});
		}

		if (type == FlateBlockType::Stored) {
			if (!stored_start_.has_value()) {
				stored_start_ = start;
			}
			stored_end_ = end;
			if (final) {
				flush_stored(true);
			}
			return;
		}
		flush_stored(false);
		bits_.put(final ? 1u : 0u, 1u);
		if (type == FlateBlockType::Fixed) {
			bits_.put(1u, 2u);
			std::array<uint16_t, kLitLenCodes> lit_codes{};
			std::array<uint16_t, kDistCodes> dist_codes{};
			build_codes(kFixedLitLengths, lit_codes);
			build_codes(kFixedDistLengths, dist_codes);
			write_symbols(lit_codes, kFixedLitLengths, dist_codes, kFixedDistLengths);
		} else {
			bits_.put(2u, 2u);
			bits_.put(static_cast<uint32_t>(trees_.hlit - 257u), 5u);
			bits_.put(static_cast<uint32_t>(trees_.hdist - 1u), 5u);
			bits_.put(static_cast<uint32_t>(trees_.hclen - 4u), 4u);
			for (size_t i = 0; i < trees_.hclen; ++i) {
				bits_.put(trees_.cl_lengths[kCodeLengthOrder[i]], 3u);
			}
			for (size_t i = 0; i < trees_.cl_count; ++i) {
				const CodeLengthSymbol& c = trees_.cl_symbols[i];
				bits_.put(trees_.cl_codes[c.symbol], trees_.cl_lengths[c.symbol]);
				if (c.symbol == 16u) {
					bits_.put(c.extra, 2u);
				} else if (c.symbol == 17u) {
					bits_.put(c.extra, 3u);
				} else if (c.symbol == 18u) {
					bits_.put(c.extra, 7u);
				}
			}
			write_symbols(trees_.lit_codes, trees_.lit_lengths, trees_.dist_codes, trees_.dist_lengths);
		}
		if (final) {
			bits_.align();
		}
	}

	[[nodiscard]] uint32_t adler32() const noexcept
	{
		constexpr uint32_t kMod = 65'521u;
		uint32_t a = 1;
		uint32_t b = 0;
		size_t i = 0;
		while (i < input_.size()) {
			// 5,552 bytes keep the sums below 2^32 before the reduction (the zlib bound).
			const size_t chunk = std::min<size_t>(input_.size() - i, 5'552u);
			const uint8_t* d = input_.data() + i;
			size_t k = 0;
			for (; k + 4u <= chunk; k += 4u) {
				a += d[k];
				b += a;
				a += d[k + 1u];
				b += a;
				a += d[k + 2u];
				b += a;
				a += d[k + 3u];
				b += a;
			}
			for (; k < chunk; ++k) {
				a += d[k];
				b += a;
			}
			a %= kMod;
			b %= kMod;
			i += chunk;
		}
		return (b << 16u) | a;
	}

	std::span<const uint8_t> input_;
	LevelParams params_;
	std::vector<uint8_t>& out_;
	BitWriter bits_;
	FlateCompressStats* stats_;
	std::vector<uint32_t> head_;
	std::vector<uint16_t> prev_;
	std::vector<Symbol> symbols_{};
	DynamicTrees trees_{};
	std::optional<size_t> stored_start_{};
	size_t stored_end_{0};
	uint64_t chain_steps_{0};
	uint32_t max_chain_steps_{0};
};

} // namespace

namespace detail {

ScopedFlateCompressAllocFailure::ScopedFlateCompressAllocFailure() noexcept
	: previous_(t_fail_compress_alloc)
{
	t_fail_compress_alloc = true;
}

ScopedFlateCompressAllocFailure::~ScopedFlateCompressAllocFailure() noexcept
{
	t_fail_compress_alloc = previous_;
}

} // namespace detail

PdfResult<std::vector<uint8_t>> flate_compress(std::span<const uint8_t> input, FlateLevel level, FlateCompressStats* stats) noexcept
{
	if (input.size() > kMaxDecodedStreamBytes) {
		return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, input.size(), kMaxDecodedStreamBytes,
			"Flate compressor input is larger than a stream may be");
	}
	try {
		if (t_fail_compress_alloc) {
			throw std::bad_alloc();
		}
		if (stats != nullptr) {
			*stats = FlateCompressStats{};
		}
		std::vector<uint8_t> out;
		// The stored-block bound, so incompressible input never reallocates.
		out.reserve(input.size() + 5u * (input.size() / kMaxStoredBlock + 1u) + 6u);
		Compressor compressor(input, level, out, stats);
		compressor.run();
		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory compressing a Flate stream");
	}
}

} // namespace recto::pdf
