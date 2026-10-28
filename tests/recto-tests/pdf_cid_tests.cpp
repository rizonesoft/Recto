// Unit tests for composite fonts (ISO 32000-1 section 9.7; D01 T02 section 30). The CMaps, maps and width arrays are
// written here, so every expected CID, glyph and advance is computed by hand from them; the fonts are the `§2` and
// `§29` fixtures in `tests/corpus/fonts/`.
#include "error.h"
#include "filters.h"
#include "font.h"
#include "font_cid.h"
#include "object.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace recto::pdf {
namespace {

std::vector<uint8_t> bytes_of(std::string_view text)
{
	return std::vector<uint8_t>(text.begin(), text.end());
}

std::shared_ptr<const std::vector<uint8_t>> corpus_font(std::string_view name)
{
	std::ifstream in(recto::test::corpus_path(std::string("fonts/") + std::string(name)), std::ios::binary);
	EXPECT_TRUE(in.good()) << name;
	return std::make_shared<const std::vector<uint8_t>>(
		std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

PdfValue name(std::string_view n)
{
	return PdfValue(PdfName(n));
}

PdfValue number(int64_t n)
{
	return PdfValue(n);
}

PdfValue ref(uint32_t object)
{
	return PdfValue(IndirectRef(object));
}

PdfValue array(std::initializer_list<PdfValue> items)
{
	return PdfValue(PdfArray(items));
}

PdfValue stream(PdfDict dict, std::vector<uint8_t> data)
{
	PdfStream s;
	s.dict = std::move(dict);
	s.raw_length = data.size();
	s.raw_bytes = std::move(data);
	return PdfValue(std::move(s));
}

PdfDict dict(std::initializer_list<std::pair<std::string_view, PdfValue>> entries)
{
	PdfDict d;
	for (const auto& [key, value] : entries) {
		d.set(key, value);
	}
	return d;
}

// A resolver over numbered objects, as a document's would be.
struct Objects {
	std::map<uint32_t, PdfValue> table;
	IndirectObjectResolver resolver = [this](IndirectRef r) -> PdfResult<PdfValue> {
		const auto it = table.find(r.obj_num);
		if (it == table.end()) {
			return make_pdf_error(ErrorCode::DamagedFormat, "no such object");
		}
		return it->second;
	};
};

// Every code of a string through a CMap: (code, length, cid).
std::vector<CidCode> codes(const CidCMap& cmap, const std::vector<uint8_t>& text)
{
	std::vector<CidCode> out;
	std::span<const uint8_t> rest(text);
	while (!rest.empty()) {
		const CidCode c = cmap.next(rest);
		EXPECT_GE(c.length, 1u);
		EXPECT_LE(c.length, rest.size());
		if (c.length == 0u || c.length > rest.size()) {
			break;
		}
		out.push_back(c);
		rest = rest.subspan(c.length);
	}
	return out;
}

void expect_codes(const std::vector<CidCode>& got, std::initializer_list<std::array<uint32_t, 3>> want, std::string_view label)
{
	ASSERT_EQ(got.size(), want.size()) << label;
	size_t i = 0;
	for (const auto& w : want) {
		EXPECT_EQ(got[i].code, w[0]) << label << " code " << i;
		EXPECT_EQ(got[i].length, w[1]) << label << " code " << i;
		EXPECT_EQ(got[i].cid, w[2]) << label << " code " << i;
		++i;
	}
}

void expect_error(ErrorCode code, ErrorCode want, std::string_view label)
{
	EXPECT_EQ(code, want) << label;
}

// Four codespaces (1 to 4 bytes), ranges and single codes overlapping (the later wins), a notdef range, `/WMode 1`.
constexpr std::string_view kMixedCMap = R"(%!PS-Adobe-3.0 Resource-CMap
/CIDInit /ProcSet findresource begin 12 dict begin begincmap
/CMapName /Recto-Test def
/WMode 1 def
4 begincodespacerange
<00> <80>
<8140> <9FFC>
<A1A1A1> <A2FEFE>
<E0000000> <E0FFFFFF>
endcodespacerange
3 begincidrange
<20> <7E> 1
<8140> <817E> 633
<A1A1A1> <A1A1FF> 2000
endcidrange
4 begincidchar
<41> 500
<E0000001> 9000
<8141> 7000
<FF> 77
endcidchar
2 beginnotdefrange
<8180> <81FC> 3
<81A0> <81A1> 4
endnotdefrange
endcmap CMapName currentdict /CMap defineresource pop end end
)";

TEST(PdfFont, Type0CodeToCid)
{
	// Identity-H and Identity-V: two-byte codes are their CIDs; an odd byte at the end is CID 0; the empty string ends.
	const CidCMap h = CidCMap::identity(false);
	const CidCMap v = CidCMap::identity(true);
	EXPECT_FALSE(h.vertical());
	EXPECT_TRUE(v.vertical());
	for (const CidCMap* cmap : {&h, &v}) {
		expect_codes(codes(*cmap, {0x12u, 0x34u, 0x00u, 0x41u, 0x56u}), {{0x1234u, 2u, 0x1234u}, {0x41u, 2u, 0x41u}, {0x56u, 1u, 0u}},
			"identity");
		EXPECT_EQ(cmap->next({}).length, 0u);
	}

	auto mixed = CidCMap::parse(bytes_of(kMixedCMap));
	ASSERT_TRUE(mixed.has_value()) << mixed.error().message.view();
	EXPECT_TRUE(mixed->vertical());
	expect_codes(codes(*mixed, {0x41u, 0x42u, 0x81u, 0x41u, 0x81u, 0x42u, 0x81u, 0x80u, 0x81u, 0x90u, 0x81u, 0xA0u, 0x81u, 0xB0u, 0x81u, 0xFDu, 0xA1u, 0xA1u, 0xA5u,
								   0xE0u, 0x00u, 0x00u, 0x01u, 0xE0u, 0x00u, 0x00u, 0x02u, 0xFFu}),
		{
			{0x41u, 1u, 500u},         // the later single code over the range
			{0x42u, 1u, 35u},          // the range: 1 + (0x42 - 0x20)
			{0x8141u, 2u, 7000u},      // a later single code
			{0x8142u, 2u, 635u},       // the range
			{0x8180u, 2u, 3u},         // in the codespace, unmapped: the notdef range
			{0x8190u, 2u, 3u},         // every code of a notdef range is its one CID
			{0x81A0u, 2u, 4u},         // a later notdef range splits the first
			{0x81B0u, 2u, 3u},         // and the range's part after it keeps its one CID
			{0x81FDu, 2u, 0u},         // FD is past the codespace's FC: unmatched, two bytes by its first byte, CID 0
			{0xA1A1A5u, 3u, 2004u},    // three bytes
			{0xE0000001u, 4u, 9000u},  // four bytes
			{0xE0000002u, 4u, 0u},     // in the codespace, no mapping: CID 0
			{0xFFu, 1u, 0u},           // no range starts with FF: the shortest length, CID 0 though <FF> is mapped
		},
		"mixed");
	// Truncated strings: a partial three-byte code and an odd byte left take the rest as CID 0.
	expect_codes(codes(*mixed, {0x41u, 0xA1u, 0xA1u}), {{0x41u, 1u, 500u}, {0xA1A1u, 2u, 0u}}, "partial three-byte");
	expect_codes(codes(*mixed, {0x81u}), {{0x81u, 1u, 0u}}, "odd byte");

	// usecmap of an Identity CMap: its codespace and mappings, this CMap's own over them.
	auto over_identity = CidCMap::parse(bytes_of("/Identity-H usecmap 1 begincidchar <0041> 77 endcidchar"));
	ASSERT_TRUE(over_identity.has_value()) << over_identity.error().message.view();
	expect_codes(codes(*over_identity, {0x00u, 0x41u, 0x00u, 0x42u}), {{0x41u, 2u, 77u}, {0x42u, 2u, 0x42u}}, "usecmap");
	// A base CMap passed in (a /UseCMap stream) lies under this one.
	auto derived = CidCMap::parse(bytes_of("1 begincidchar <42> 900 endcidchar"), &*mixed);
	ASSERT_TRUE(derived.has_value()) << derived.error().message.view();
	expect_codes(codes(*derived, {0x41u, 0x42u, 0x43u}), {{0x41u, 1u, 500u}, {0x42u, 1u, 900u}, {0x43u, 1u, 36u}}, "derived");
	EXPECT_TRUE(derived->vertical());

	// Refusals.
	const auto refused = [](std::string_view text, ErrorCode want, std::string_view label) {
		const auto cmap = CidCMap::parse(bytes_of(text));
		ASSERT_FALSE(cmap.has_value()) << label;
		expect_error(cmap.error().code, want, label);
	};
	refused("/UniJIS-UCS2-H usecmap", ErrorCode::UnsupportedFeature, "a predefined usecmap");
	refused("1 begincidchar <41> 5 endcidchar", ErrorCode::DamagedFormat, "no codespace");
	refused("1 begincodespacerange <00> <FF> endcodespacerange 1 begincidrange <00> <FF> 65281 endcidrange",
		ErrorCode::ResourceLimit, "CIDs past 65,535");
	EXPECT_TRUE(CidCMap::parse(bytes_of("1 begincodespacerange <00> <FF> endcodespacerange 1 begincidrange <00> <FF> 65280 "
										"endcidrange"))
					.has_value());
	refused("1 begincodespacerange <00> <FF> endcodespacerange 1 begincidrange <10> <0F> 1 endcidrange",
		ErrorCode::DamagedFormat, "a reversed range");
	refused("1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar <41> endcidchar", ErrorCode::DamagedFormat,
		"a code without its CID");
	{
		std::string many = "1 begincodespacerange <0000> <FFFF> endcodespacerange 100001 begincidchar\n";
		for (uint32_t i = 0; i < 100'001u; ++i) {
			std::array<char, 32> line{};
			std::snprintf(line.data(), line.size(), "<%04X> %u\n", i & 0xFFFFu, i & 0xFFFFu);
			many += line.data();
		}
		refused(many + "endcidchar", ErrorCode::ResourceLimit, "100,001 mappings");
	}
	{
		std::string ranges = "101 begincodespacerange\n";
		for (uint32_t i = 0; i < 101u; ++i) {
			ranges += "<00> <FF>\n";
		}
		refused(ranges + "endcodespacerange", ErrorCode::ResourceLimit, "101 codespace ranges");
	}
	refused(std::string(kMaxCidCMapStreamBytes + 1u, ' '), ErrorCode::ResourceLimit, "a stream past 4 MiB");

	// beginnotdefchar: one code's notdef CID.
	auto notdef_single = CidCMap::parse(
		bytes_of("1 begincodespacerange <00> <FF> endcodespacerange 1 beginnotdefchar <41> 7 endnotdefchar"));
	ASSERT_TRUE(notdef_single.has_value());
	expect_codes(codes(*notdef_single, {0x41u, 0x42u}), {{0x41u, 1u, 7u}, {0x42u, 1u, 0u}}, "notdef single");
	// A code of five bytes is past the four a CMap allows.
	refused("1 begincodespacerange <0000000000> <FFFFFFFFFF> endcodespacerange", ErrorCode::DamagedFormat, "a 5-byte code");
	// Hex strings that are not codes keep the ordinary bound: a /Registry written in hex.
	EXPECT_TRUE(CidCMap::parse(bytes_of("/CIDSystemInfo 3 dict dup begin /Registry <41646F6265> def end def "
										"1 begincodespacerange <00> <FF> endcodespacerange"))
					.has_value());
	// One base: Identity named again is the same base, however often; a second, different base is damage; the base's
	// codespace counts toward the bound.
	{
		std::string repeated;
		for (int i = 0; i < 200; ++i) {
			repeated += "/Identity-H usecmap ";
		}
		auto same = CidCMap::parse(bytes_of(repeated + "1 begincidchar <0041> 77 endcidchar"));
		ASSERT_TRUE(same.has_value()) << same.error().message.view();
		expect_codes(codes(*same, {0x00u, 0x41u, 0x00u, 0x42u}), {{0x41u, 2u, 77u}, {0x42u, 2u, 0x42u}}, "repeated identity");
		const auto second = CidCMap::parse(bytes_of("/Identity-H usecmap"), &*mixed);
		ASSERT_FALSE(second.has_value());
		EXPECT_EQ(second.error().code, ErrorCode::DamagedFormat);
		std::string over = "/Identity-H usecmap 100 begincodespacerange\n";
		for (int i = 0; i < 100; ++i) {
			over += "<00> <FF>\n";
		}
		refused(over + "endcodespacerange", ErrorCode::ResourceLimit, "100 ranges over an Identity base");
		std::string under = over.substr(std::string_view("/Identity-H usecmap ").size());
		refused(under + "endcodespacerange /Identity-H usecmap", ErrorCode::ResourceLimit, "an Identity base after 100 ranges");
	}
}

TEST(PdfFont, CidToGidMap)
{
	const CidToGidMap identity = CidToGidMap::identity();
	EXPECT_EQ(identity.glyph(5u), 5u);
	auto map = CidToGidMap::parse(std::vector<uint8_t>{0u, 3u, 0u, 7u, 1u, 0u});
	ASSERT_TRUE(map.has_value());
	EXPECT_EQ(map->glyph(0u), 3u);
	EXPECT_EQ(map->glyph(1u), 7u);
	EXPECT_EQ(map->glyph(2u), 256u);
	EXPECT_EQ(map->glyph(3u), 0u); // past the map
	const auto odd = CidToGidMap::parse(std::vector<uint8_t>{0u, 3u, 0u});
	ASSERT_FALSE(odd.has_value());
	EXPECT_EQ(odd.error().code, ErrorCode::DamagedFormat);

	// /CIDFontType2 over §2's TrueType fixture (glyphs 0 to 6): through the map, a glyph past the font is glyph 0.
	auto truetype = load_embedded_font(corpus_font("truetype-full-short-loca.ttf"), FontProgramFormat::TrueType);
	ASSERT_TRUE(truetype.has_value());
	const LoadedFont& tt = **truetype;
	ASSERT_EQ(tt.glyph_count(), 7u);
	EXPECT_EQ(cid_glyph(0u, CidFontKind::CIDFontType2, *map, &tt), 3u);
	EXPECT_EQ(cid_glyph(2u, CidFontKind::CIDFontType2, *map, &tt), 0u); // 256 is past the font
	EXPECT_EQ(cid_glyph(9u, CidFontKind::CIDFontType2, *map, &tt), 0u); // past the map
	EXPECT_EQ(cid_glyph(6u, CidFontKind::CIDFontType2, identity, &tt), 6u);
	EXPECT_EQ(cid_glyph(7u, CidFontKind::CIDFontType2, identity, &tt), 0u);

	// /CIDFontType0 over §29's CID-keyed fixture (CIDs 0, 100 to 102 and 500) and its name-keyed one (32 glyphs).
	auto cid_keyed = load_charstring_font(corpus_font("cff-cid.cff"), CharstringFormat::CIDFontType0C);
	auto name_keyed = load_charstring_font(corpus_font("cff-outlines.cff"), CharstringFormat::Type1C);
	ASSERT_TRUE(cid_keyed.has_value() && name_keyed.has_value());
	EXPECT_TRUE((*cid_keyed)->cid_keyed());
	EXPECT_FALSE((*name_keyed)->cid_keyed());
	struct CidGlyph {
		uint32_t cid;
		uint32_t glyph;
	};
	for (const CidGlyph& want : {CidGlyph{0u, 0u}, CidGlyph{100u, 1u}, CidGlyph{101u, 2u}, CidGlyph{102u, 3u},
			 CidGlyph{500u, 4u}, CidGlyph{7u, 0u}, CidGlyph{103u, 0u}}) {
		EXPECT_EQ(cid_glyph(want.cid, CidFontKind::CIDFontType0, identity, cid_keyed->get()), want.glyph) << want.cid;
	}
	EXPECT_EQ(cid_glyph(3u, CidFontKind::CIDFontType0, identity, name_keyed->get()), 3u); // the CID is the glyph
	EXPECT_EQ(cid_glyph(40u, CidFontKind::CIDFontType0, identity, name_keyed->get()), 0u); // past its 32 glyphs
	// Without a font the glyph is unchecked, but never past 16 bits.
	EXPECT_EQ(cid_glyph(40u, CidFontKind::CIDFontType0, identity, nullptr), 40u);
	EXPECT_EQ(cid_glyph(70'000u, CidFontKind::CIDFontType0, identity, nullptr), 0u);
}

TEST(PdfFont, CidFontWidths)
{
	Objects objects;
	objects.table.emplace(5u, array({number(500), number(600), number(700)}));
	objects.table.emplace(6u, array({number(1), ref(5u), number(10), number(20), number(300), number(12), array({number(900)}),
								   name("Bad"), number(30), array({name("x"), number(250)}), number(10), array({number(450)})}));
	const PdfValue w = ref(6u); // /W itself through a reference, its first list too
	const PdfValue dw = number(800);
	const PdfValue dw2 = array({number(900), number(-1100)});
	const PdfValue w2 = array({number(1), array({number(-900), number(250), number(880), number(-800), number(300), number(870)}),
		number(10), number(11), number(-700), number(100), number(800)});
	auto widths = CidWidths::build(&dw, &w, &dw2, &w2, &objects.resolver);
	ASSERT_TRUE(widths.has_value()) << widths.error().message.view();

	// Horizontal: the list form, the range form, a later entry over an earlier one, a skipped malformed entry.
	for (const auto& [cid, advance] : std::initializer_list<std::pair<uint32_t, double>>{{1u, 0.5}, {2u, 0.6}, {3u, 0.7},
			 {10u, 0.45}, {11u, 0.3}, {12u, 0.9}, {20u, 0.3}, {21u, 0.8}, {30u, 0.8}, {31u, 0.25}, {5u, 0.8}}) {
		EXPECT_DOUBLE_EQ(widths->advance(cid), advance) << cid;
	}
	// Vertical: both /W2 forms, and the /DW2 default with vx half the horizontal advance; each part apart.
	const auto vertical = [&](uint32_t cid, double w1y, double vx, double vy) {
		const CidVertical got = widths->vertical(cid);
		EXPECT_DOUBLE_EQ(got.w1y, w1y) << cid;
		EXPECT_DOUBLE_EQ(got.vx, vx) << cid;
		EXPECT_DOUBLE_EQ(got.vy, vy) << cid;
	};
	vertical(1u, -0.9, 0.25, 0.88);
	vertical(2u, -0.8, 0.3, 0.87);
	vertical(10u, -0.7, 0.1, 0.8);
	vertical(11u, -0.7, 0.1, 0.8);
	vertical(5u, -1.1, 0.4, 0.9);   // defaults: vx from /DW's 0.8
	vertical(12u, -1.1, 0.45, 0.9); // defaults: vx from /W's 0.9

	// An Identity-H string: each code's advance.
	const CidCMap identity = CidCMap::identity(false);
	const std::vector<uint8_t> text{0u, 1u, 0u, 12u, 0u, 5u};
	double total = 0.0;
	for (const CidCode& code : codes(identity, text)) {
		total += widths->advance(code.cid);
	}
	EXPECT_DOUBLE_EQ(total, 0.5 + 0.9 + 0.8);

	// The defaults without any entry: 1000 and [880 -1000].
	auto defaults = CidWidths::build(nullptr, nullptr, nullptr, nullptr, nullptr);
	ASSERT_TRUE(defaults.has_value());
	EXPECT_DOUBLE_EQ(defaults->advance(9u), 1.0);
	EXPECT_DOUBLE_EQ(defaults->vertical(9u).w1y, -1.0);
	EXPECT_DOUBLE_EQ(defaults->vertical(9u).vx, 0.5);
	EXPECT_DOUBLE_EQ(defaults->vertical(9u).vy, 0.88);

	// Bounds: a range reaching past CID 65,535, a list reaching past it, an over-long array; the last CID passes.
	const auto refused = [](const PdfValue& bad, std::string_view label) {
		const auto result = CidWidths::build(nullptr, &bad, nullptr, nullptr, nullptr);
		ASSERT_FALSE(result.has_value()) << label;
		EXPECT_EQ(result.error().code, ErrorCode::ResourceLimit) << label;
	};
	refused(array({number(65'500), number(65'536), number(100)}), "a range past CID 65,535");
	refused(array({number(65'530), array({number(1), number(2), number(3), number(4), number(5), number(6), number(7)})}),
		"a list past CID 65,535");
	const PdfValue last = array({number(65'535), array({number(250)})});
	auto at_last = CidWidths::build(nullptr, &last, nullptr, nullptr, nullptr);
	ASSERT_TRUE(at_last.has_value());
	EXPECT_DOUBLE_EQ(at_last->advance(65'535u), 0.25);
	PdfArray long_array(kMaxCidWidthElements + 1u, number(1));
	refused(PdfValue(std::move(long_array)), "65,537 elements");

	// /W2 and its list through references, a later entry over an earlier one, /DW2's members through references.
	{
		Objects v;
		v.table.emplace(40u, array({number(-900), number(250), number(880)}));
		v.table.emplace(41u, array({number(1), ref(40u), number(1), number(1), number(-600), number(200), number(700)}));
		v.table.emplace(42u, number(910));
		v.table.emplace(43u, number(-1200));
		const PdfValue w2_ref = ref(41u);
		const PdfValue dw2_members = array({ref(42u), ref(43u)});
		auto vertical_widths = CidWidths::build(nullptr, nullptr, &dw2_members, &w2_ref, &v.resolver);
		ASSERT_TRUE(vertical_widths.has_value()) << vertical_widths.error().message.view();
		EXPECT_DOUBLE_EQ(vertical_widths->vertical(1u).w1y, -0.6); // the later range over the list
		EXPECT_DOUBLE_EQ(vertical_widths->vertical(1u).vx, 0.2);
		EXPECT_DOUBLE_EQ(vertical_widths->vertical(1u).vy, 0.7);
		EXPECT_DOUBLE_EQ(vertical_widths->vertical(2u).w1y, -1.2); // /DW2 through references
		EXPECT_DOUBLE_EQ(vertical_widths->vertical(2u).vy, 0.91);
		const auto refused_w2 = [](const PdfValue& bad, std::string_view label) {
			const auto result = CidWidths::build(nullptr, nullptr, nullptr, &bad, nullptr);
			ASSERT_FALSE(result.has_value()) << label;
			EXPECT_EQ(result.error().code, ErrorCode::ResourceLimit) << label;
		};
		refused_w2(array({number(65'000), number(65'536), number(-1000), number(500), number(880)}), "a /W2 range past CID 65,535");
		refused_w2(PdfValue(PdfArray(kMaxCidWidthElements + 1u, number(1))), "a /W2 of 65,537 elements");
	}
	// A resolver error other than a missing or damaged object is passed on, never turned into default widths.
	{
		const IndirectObjectResolver limited = [](IndirectRef r) -> PdfResult<PdfValue> {
			return make_pdf_error(r.obj_num == 50u ? ErrorCode::ResourceLimit : ErrorCode::DamagedFormat, "injected");
		};
		const PdfValue w_limited = array({number(1), ref(50u)});
		const auto passed_on = CidWidths::build(nullptr, &w_limited, nullptr, nullptr, &limited);
		ASSERT_FALSE(passed_on.has_value());
		EXPECT_EQ(passed_on.error().code, ErrorCode::ResourceLimit);
		const PdfValue w_damaged = array({number(1), ref(51u), number(2), array({number(450)})});
		const auto skipped = CidWidths::build(nullptr, &w_damaged, nullptr, nullptr, &limited);
		ASSERT_TRUE(skipped.has_value());
		EXPECT_DOUBLE_EQ(skipped->advance(1u), 1.0); // the damaged entry skipped
		EXPECT_DOUBLE_EQ(skipped->advance(2u), 0.45);
	}
}

// A Type 0 font assembled from its object graph, indirect references throughout.
TEST(PdfFont, Type0Assembly)
{
	// /CIDFontType2 over §2's TrueType fixture with a stream /CIDToGIDMap and /W through a reference.
	{
		Objects o;
		o.table.emplace(2u, array({ref(3u)}));
		o.table.emplace(3u, PdfValue(dict({{"Type", name("Font")}, {"Subtype", name("CIDFontType2")}, {"W", ref(4u)},
								{"DW", number(1000)}, {"CIDToGIDMap", ref(5u)}, {"FontDescriptor", ref(6u)}})));
		o.table.emplace(4u, array({number(0), array({number(500), number(600), number(700)})}));
		o.table.emplace(5u, stream(PdfDict{}, {0u, 2u, 0u, 1u, 0u, 9u}));
		o.table.emplace(6u, PdfValue(dict({{"FontFile2", ref(7u)}})));
		o.table.emplace(7u, stream(PdfDict{}, *corpus_font("truetype-full-short-loca.ttf")));
		const PdfDict type0 = dict({{"Type", name("Font")}, {"Subtype", name("Type0")}, {"Encoding", name("Identity-H")},
			{"DescendantFonts", ref(2u)}});
		auto font = load_type0_font(type0, o.resolver);
		ASSERT_TRUE(font.has_value()) << font.error().message.view();
		ASSERT_NE((*font)->font(), nullptr);
		EXPECT_EQ((*font)->kind(), CidFontKind::CIDFontType2);
		// The retained program counts toward the font's memory, which a page's font budget charges.
		ASSERT_NE((*font)->font()->program(), nullptr);
		EXPECT_GE((*font)->memory_bytes(), (*font)->font()->program()->size());
		const std::vector<uint8_t> text{0u, 0u, 0u, 1u, 0u, 2u, 0u, 3u};
		std::span<const uint8_t> rest(text);
		// CID 2's glyph 9 and CID 3 (past the map) are not in the font: each is drawn as CID 0 (no notdef mapping in
		// Identity-H), glyph 2 at CID 0's width (9.7.6.3).
		const std::array<std::array<double, 3>, 4> want = {{{0, 2, 0.5}, {1, 1, 0.6}, {0, 2, 0.5}, {0, 2, 0.5}}};
		for (const auto& [cid, glyph, advance] : want) {
			const Type0Glyph g = (*font)->next(rest);
			ASSERT_EQ(g.length, 2u);
			EXPECT_EQ(g.cid, static_cast<uint32_t>(cid));
			EXPECT_EQ(g.glyph, static_cast<uint16_t>(glyph)) << cid;
			EXPECT_DOUBLE_EQ(g.advance, advance) << cid;
			rest = rest.subspan(g.length);
		}
		EXPECT_EQ((*font)->next(rest).length, 0u);
	}
	// /CIDFontType0 over §29's CID-keyed CFF, its embedded CMap inheriting through /UseCMap from another embedded CMap,
	// which inherits Identity-H.
	Objects o;
	o.table.emplace(10u, stream(dict({{"UseCMap", ref(11u)}}),
							 bytes_of("1 begincodespacerange <0000> <FFFF> endcodespacerange 1 begincidchar <0001> 100 endcidchar")));
	o.table.emplace(11u, stream(dict({{"UseCMap", name("Identity-H")}}), bytes_of("1 begincidrange <0010> <0012> 100 endcidrange")));
	o.table.emplace(2u, array({ref(3u)}));
	o.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}, {"DW", number(500)}, {"FontDescriptor", ref(12u)},
							{"W", array({number(100), array({number(550)}), number(500), number(500), number(700)})}})));
	o.table.emplace(12u, PdfValue(dict({{"FontFile3", ref(13u)}})));
	o.table.emplace(13u, stream(dict({{"Subtype", name("CIDFontType0C")}}), *corpus_font("cff-cid.cff")));
	const PdfDict type0 = dict({{"Subtype", name("Type0")}, {"Encoding", ref(10u)}, {"DescendantFonts", ref(2u)}});
	auto font = load_type0_font(type0, o.resolver);
	ASSERT_TRUE(font.has_value()) << font.error().message.view();
	EXPECT_EQ((*font)->kind(), CidFontKind::CIDFontType0);
	const std::vector<uint8_t> text{0u, 1u, 0u, 0x11u, 0u, 0x12u, 0x01u, 0xF4u, 0u, 5u};
	std::span<const uint8_t> rest(text);
	const std::array<std::array<double, 4>, 5> want = {{
		{1, 100, 1, 0.55},     // the top CMap's own code
		{0x11, 101, 2, 0.5},   // the inherited range, /DW
		{0x12, 102, 3, 0.5},
		{0x1F4, 500, 4, 0.7},  // Identity under both, the /W range
		{5, 0, 0, 0.5},        // Identity: a CID the font's charset lacks is drawn as CID 0
	}};
	for (const auto& [code, cid, glyph, advance] : want) {
		const Type0Glyph g = (*font)->next(rest);
		ASSERT_EQ(g.length, 2u);
		EXPECT_EQ(g.code, static_cast<uint32_t>(code));
		EXPECT_EQ(g.cid, static_cast<uint32_t>(cid)) << code;
		EXPECT_EQ(g.glyph, static_cast<uint16_t>(glyph)) << code;
		EXPECT_DOUBLE_EQ(g.advance, advance) << code;
		rest = rest.subspan(g.length);
	}

	// No program: a font without a LoadedFont, the CID its glyph unchecked, for §17 to substitute.
	{
		Objects bare;
		bare.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}})));
		const PdfDict t = dict({{"Encoding", name("Identity-V")}, {"DescendantFonts", array({ref(3u)})}});
		auto f = load_type0_font(t, bare.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		EXPECT_EQ((*f)->font(), nullptr);
		EXPECT_TRUE((*f)->cmap().vertical());
		const std::vector<uint8_t> one{0x01u, 0x00u};
		EXPECT_EQ((*f)->next(one).glyph, 256u);
	}

	// /FontFile3 /OpenType (§2's CFF-flavoured OpenType fixture, 2 glyphs): the CID is the glyph.
	{
		Objects ot;
		ot.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}, {"FontDescriptor", ref(6u)}})));
		ot.table.emplace(6u, PdfValue(dict({{"FontFile3", ref(7u)}})));
		ot.table.emplace(7u, stream(dict({{"Subtype", name("OpenType")}}), *corpus_font("opentype-cff.otf")));
		auto f = load_type0_font(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({ref(3u)})}}), ot.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		ASSERT_NE((*f)->font(), nullptr);
		EXPECT_EQ((*f)->font()->kind(), LoadedFontKind::OpenType);
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 1u}).glyph, 1u);
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 2u}).glyph, 0u); // past the font's 2 glyphs
	}
	// A refused program refuses the font with the loader's own error.
	{
		Objects bad;
		bad.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType2")}, {"FontDescriptor", ref(6u)}})));
		bad.table.emplace(6u, PdfValue(dict({{"FontFile2", ref(7u)}})));
		bad.table.emplace(7u, stream(PdfDict{}, bytes_of("notafont")));
		const auto f = load_type0_font(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({ref(3u)})}}), bad.resolver);
		ASSERT_FALSE(f.has_value());
		EXPECT_EQ(f.error().code, ErrorCode::DamagedFormat);
	}
	// The same font with its values direct and through references gives the same codes, CIDs, glyphs and metrics.
	{
		const std::vector<uint8_t> base_cmap = bytes_of("1 begincodespacerange <0000> <FFFF> endcodespacerange 1 begincidchar <0001> 100 endcidchar");
		Objects direct;
		direct.table.emplace(10u, stream(dict({{"UseCMap", name("Identity-H")}}), base_cmap));
		direct.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}, {"DW2", array({number(900), number(-1100)})},
								 {"W", array({number(100), array({number(550)})})}})));
		Objects indirect;
		indirect.table.emplace(9u, name("Identity-H"));
		indirect.table.emplace(10u, stream(dict({{"UseCMap", ref(9u)}}), base_cmap));
		indirect.table.emplace(11u, number(900));
		indirect.table.emplace(12u, number(-1100));
		indirect.table.emplace(13u, array({number(550)}));
		indirect.table.emplace(14u, name("CIDFontType0"));
		indirect.table.emplace(3u, PdfValue(dict({{"Subtype", ref(14u)}, {"DW2", array({ref(11u), ref(12u)})},
								   {"W", array({number(100), ref(13u)})}})));
		const PdfDict t = dict({{"Encoding", ref(10u)}, {"DescendantFonts", array({ref(3u)})}});
		auto a = load_type0_font(t, direct.resolver);
		auto b = load_type0_font(t, indirect.resolver);
		ASSERT_TRUE(a.has_value() && b.has_value()) << (b.has_value() ? "" : std::string(b.error().message.view()));
		const std::vector<uint8_t> shown{0u, 1u, 0u, 7u};
		std::span<const uint8_t> ra(shown);
		std::span<const uint8_t> rb(shown);
		while (!ra.empty()) {
			const Type0Glyph ga = (*a)->next(ra);
			const Type0Glyph gb = (*b)->next(rb);
			EXPECT_EQ(ga.cid, gb.cid);
			EXPECT_EQ(ga.glyph, gb.glyph);
			EXPECT_DOUBLE_EQ(ga.advance, gb.advance);
			EXPECT_DOUBLE_EQ(ga.vertical.w1y, gb.vertical.w1y);
			EXPECT_DOUBLE_EQ(ga.vertical.vy, gb.vertical.vy);
			ra = ra.subspan(ga.length);
			rb = rb.subspan(gb.length);
		}
		EXPECT_EQ((*b)->next(std::vector<uint8_t>{0u, 1u}).cid, 100u);
		EXPECT_DOUBLE_EQ((*b)->next(std::vector<uint8_t>{0u, 1u}).advance, 0.55);
		EXPECT_DOUBLE_EQ((*b)->next(std::vector<uint8_t>{0u, 7u}).vertical.w1y, -1.1);
	}

	// A CID the font lacks takes the code's notdef mapping: <41> maps to CID 7, which cff-cid.cff lacks, so its notdef
	// CID 100 (glyph 1) is drawn.
	{
		Objects n;
		n.table.emplace(8u, stream(PdfDict{}, bytes_of("1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar <41> 7 "
													   "endcidchar 1 beginnotdefchar <41> 100 endnotdefchar")));
		n.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}, {"FontDescriptor", ref(6u)},
								{"W", array({number(100), array({number(550)})})}})));
		n.table.emplace(6u, PdfValue(dict({{"FontFile3", ref(7u)}})));
		n.table.emplace(7u, stream(dict({{"Subtype", name("CIDFontType0C")}}), *corpus_font("cff-cid.cff")));
		auto f = load_type0_font(dict({{"Encoding", ref(8u)}, {"DescendantFonts", array({ref(3u)})}}), n.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		const Type0Glyph g = (*f)->next(std::vector<uint8_t>{0x41u});
		EXPECT_EQ(g.cid, 100u);
		EXPECT_EQ(g.glyph, 1u);
		EXPECT_DOUBLE_EQ(g.advance, 0.55);
	}
	// /FontFile3 /OpenType holding a CID-keyed CFF: its charset maps CIDs to glyphs.
	{
		Objects ot;
		ot.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}, {"FontDescriptor", ref(6u)}})));
		ot.table.emplace(6u, PdfValue(dict({{"FontFile3", ref(7u)}})));
		ot.table.emplace(7u, stream(dict({{"Subtype", name("OpenType")}}), *corpus_font("opentype-cff-cid.otf")));
		auto f = load_type0_font(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({ref(3u)})}}), ot.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		ASSERT_NE((*f)->font(), nullptr);
		EXPECT_EQ((*f)->font()->kind(), LoadedFontKind::OpenType);
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 100u}).glyph, 1u);
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0x01u, 0xF4u}).glyph, 4u); // CID 500
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 7u}).glyph, 0u);
	}
	// `usecmap` naming the base its /UseCMap gives is that base; another name is not.
	{
		Objects u;
		u.table.emplace(20u, stream(PdfDict{}, bytes_of("/CMapName /BaseMap def 1 begincodespacerange <0000> <FFFF> "
														"endcodespacerange 1 begincidchar <0002> 101 endcidchar")));
		u.table.emplace(21u, stream(dict({{"UseCMap", ref(20u)}}), bytes_of("/BaseMap usecmap 1 begincidchar <0001> 100 endcidchar")));
		u.table.emplace(22u, stream(dict({{"UseCMap", ref(20u)}}), bytes_of("/OtherMap usecmap")));
		u.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}})));
		auto f = load_type0_font(dict({{"Encoding", ref(21u)}, {"DescendantFonts", array({ref(3u)})}}), u.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 1u}).cid, 100u);
		EXPECT_EQ((*f)->next(std::vector<uint8_t>{0u, 2u}).cid, 101u);
		const auto other = load_type0_font(dict({{"Encoding", ref(22u)}, {"DescendantFonts", array({ref(3u)})}}), u.resolver);
		ASSERT_FALSE(other.has_value());
		EXPECT_EQ(other.error().code, ErrorCode::UnsupportedFeature);
	}

	// /WMode in the CMap stream's dictionary.
	{
		Objects w;
		w.table.emplace(8u, stream(dict({{"WMode", number(1)}}), bytes_of("1 begincodespacerange <0000> <FFFF> endcodespacerange")));
		w.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType0")}})));
		auto f = load_type0_font(dict({{"Encoding", ref(8u)}, {"DescendantFonts", array({ref(3u)})}}), w.resolver);
		ASSERT_TRUE(f.has_value()) << f.error().message.view();
		EXPECT_TRUE((*f)->cmap().vertical());
	}

	// Refusals.
	const auto refused = [](const PdfDict& t, Objects& objs, ErrorCode want, std::string_view label) {
		const auto f = load_type0_font(t, objs.resolver);
		ASSERT_FALSE(f.has_value()) << label;
		EXPECT_EQ(f.error().code, want) << label << ": " << f.error().message.view();
	};
	Objects e;
	e.table.emplace(3u, PdfValue(dict({{"Subtype", name("CIDFontType2")}})));
	e.table.emplace(4u, PdfValue(dict({{"Subtype", name("Type1")}})));
	refused(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({})}}), e, ErrorCode::DamagedFormat, "no descendant");
	refused(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({ref(3u), ref(3u)})}}), e,
		ErrorCode::DamagedFormat, "two descendants");
	refused(dict({{"Encoding", name("Identity-H")}, {"DescendantFonts", array({ref(4u)})}}), e, ErrorCode::DamagedFormat,
		"a Type 1 descendant");
	refused(dict({{"Encoding", name("UniJIS-UCS2-H")}, {"DescendantFonts", array({ref(3u)})}}), e,
		ErrorCode::UnsupportedFeature, "a predefined CMap");
	// A /UseCMap cycle, and chains of 8 CMaps (loaded) and 9 (refused).
	const std::vector<uint8_t> codespace = bytes_of("1 begincodespacerange <0000> <FFFF> endcodespacerange");
	e.table.emplace(20u, stream(dict({{"UseCMap", ref(21u)}}), codespace));
	e.table.emplace(21u, stream(dict({{"UseCMap", ref(20u)}}), codespace));
	refused(dict({{"Encoding", ref(20u)}, {"DescendantFonts", array({ref(3u)})}}), e, ErrorCode::DamagedFormat, "a cycle");
	for (uint32_t level = 0; level < 9u; ++level) {
		PdfDict d = level + 1u < 9u ? dict({{"UseCMap", ref(31u + level)}}) : dict({{"UseCMap", name("Identity-H")}});
		e.table.emplace(30u + level, stream(std::move(d), codespace));
	}
	refused(dict({{"Encoding", ref(30u)}, {"DescendantFonts", array({ref(3u)})}}), e, ErrorCode::ResourceLimit,
		"a chain of 9 CMaps");
	EXPECT_TRUE(load_type0_font(dict({{"Encoding", ref(31u)}, {"DescendantFonts", array({ref(3u)})}}), e.resolver).has_value());
}

} // namespace
} // namespace recto::pdf
