// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "libaegisub/charset.h"

#include "libaegisub/file_mapping.h"
#include "libaegisub/scoped_ptr.h"

#ifdef WITH_UCHARDET
#include <uchardet.h>
#endif

namespace agi::charset {
std::string Detect(agi::fs::path const& file) {
	agi::read_file_mapping fp(file);

	// First check for known magic bytes which identify the file type
	if (fp.size() >= 4) {
		const char* header = fp.read(0, 4);
		if (!strncmp(header, "\xef\xbb\xbf", 3))
			return "utf-8";
		if (!strncmp(header, "\x00\x00\xfe\xff", 4))
			return "utf-32be";
		if (!strncmp(header, "\xff\xfe\x00\x00", 4))
			return "utf-32le";
		if (!strncmp(header, "\xfe\xff", 2))
			return "utf-16be";
		if (!strncmp(header, "\xff\xfe", 2))
			return "utf-16le";
		if (!strncmp(header, "\x1a\x45\xdf\xa3", 4))
			return "binary"; // Actually EBML/Matroska
	}

	// If it's over 100 MB it's either binary or big enough that we won't
	// be able to do anything useful with it anyway
	if (fp.size() > 100 * 1024 * 1024)
		return "binary";

	uint64_t binaryish = 0;
	uint64_t utf8_errors = 0;

#ifdef WITH_UCHARDET
	agi::scoped_holder<uchardet_t> ud(uchardet_new(), uchardet_delete);
	int utf8_cont_bytes = 0;
	for (uint64_t offset = 0; offset < fp.size(); ) {
		auto read = std::min<uint64_t>(4096, fp.size() - offset);
		auto buf = fp.read(offset, read);
		uchardet_handle_data(ud, buf, read);

		offset += read;

		for (size_t i = 0; i < read; ++i) {
			// A dumb heuristic to detect binary files
			if ((unsigned char)buf[i] < 32 && (buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t'))
				++binaryish;

			// Detect utf8 errors
			// We don't check for:
			//  - overlong encodings
			//  - invalid Unicode ranges
			int l_ones = std::countl_one((unsigned char)buf[i]);
			if (utf8_cont_bytes && l_ones == 1) {
				// valid continuation byte
				--utf8_cont_bytes;
			} else {
				if (utf8_cont_bytes) {
					// missing continuation bytes
					utf8_cont_bytes = 0;
					++utf8_errors;
				}
				if (l_ones > 4) {
					// invalid byte
					++utf8_errors;
				} else if (l_ones > 1) {
					// start of multibyte sequence
					utf8_cont_bytes = l_ones - 1;
				} else if (l_ones == 1) {
					// invalid continuation byte
					++utf8_errors;
				}
			}
		}

		if (binaryish > offset / 8)
			return "binary";
	}
	if (utf8_cont_bytes) {
		// missing continuation bytes
		++utf8_errors;
	}
	if (utf8_errors < 5)
		return "utf-8";
	uchardet_data_end(ud);
	return uchardet_get_charset(ud);
#else
	auto read = std::min<uint64_t>(4096, fp.size());
	auto buf = fp.read(0, read);
	for (size_t i = 0; i < read; ++i) {
		if ((unsigned char)buf[i] < 32 && (buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t'))
			++binaryish;
	}

	if (binaryish > read / 8)
		return "binary";
	return "utf-8";
#endif
}
}
