// Copyright 2017-2018 ccls Authors
// SPDX-License-Identifier: Apache-2.0

#include "log.hh"
#include "pipeline.hh"
#include "platform.hh"
#include "serializer.hh"
#include "test.hh"
#include "working_files.hh"

#include <clang/Basic/Version.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/CrashRecoveryContext.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/Signals.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <iostream>
#include <fstream>

using namespace ccls;
using namespace llvm;
using namespace llvm::cl;

namespace ccls {
std::vector<std::string> g_init_options;
}

namespace {
OptionCategory C("ccls options");

opt<bool> opt_help("h", desc("Alias for -help"), cat(C));
opt<int> opt_verbose("v", desc("verbosity, from -3 (fatal) to 2 (verbose)"),
                     init(0), cat(C));
opt<std::string> opt_test_index("test-index", ValueOptional, init("!"),
                                desc("run index tests"), cat(C));

opt<std::string> opt_index("index",
                           desc("standalone mode: index a project and exit"),
                           value_desc("root"), cat(C));
list<std::string> opt_init("init", desc("extra initialization options in JSON"),
                           cat(C));
opt<std::string> opt_log_file("log-file", desc("stderr or log file"),
                              value_desc("file"), init("stderr"), cat(C));
opt<bool> opt_log_file_append("log-file-append", desc("append to log file"),
                              cat(C));
}

std::string unescape(const std::string &path)
{
	std::filesystem::path p(path);
	std::string loc = p.parent_path().filename();
	std::string orig = p.filename();
	/*
	 * Boy this is not true, see escapeFileName().
	 * - if the original contains '@', this is wrong
	 * - if the original contains ':', this is wrong
	 * Thankfully, those are rare enough that I don't care it's wrong.
	 */
	std::replace(orig.begin(), orig.end(), '@', '/');
	std::replace(loc.begin(), loc.end(), '@', '/');
	std::string result = loc + "/" + orig;
	return result.substr(0, result.length() - 5); // remove .blob
}

void index_file(const std::string &fn, std::ostream *output)
{
	std::string content = *readContent(fn);
	auto file = ccls::deserialize(SerializeFormat::Binary, fn, content, "", {});
	std::string orig = unescape(file->path);
	for (auto &[usr, v] : file->usr2var) {
		Maybe<DeclRef> spell = v.def.spell;
		if (spell && !v.def.is_local() && v.def.kind != SymbolKind::Field) {
			std::string basicname(v.def.name(false));
			Pos start = spell->range.start;
			*output << basicname << "\t" << v.def.detailed_name <<
				"\t" << orig << "\t" << start.line << "\t" <<
				(int)v.def.kind << std::endl;
		}
	}
	for (auto &[usr, v] : file->usr2func) {
		Maybe<DeclRef> spell = v.def.spell;
		if (spell) {
			std::string basicname(v.def.name(false));
			Pos start = spell->range.start;
			*output << basicname << "\t" << v.def.detailed_name <<
				"\t" << orig << "\t" << start.line << "\t" <<
				(int)v.def.kind << std::endl;
		}
	}
	for (auto &[usr, v] : file->usr2type) {
		Maybe<DeclRef> spell = v.def.spell;
		if (spell) {
			std::string basicname(v.def.name(false));
			Pos start = spell->range.start;
			*output << basicname << "\t" << v.def.detailed_name <<
				"\t" << orig << "\t" << start.line << "\t" <<
				(int)v.def.kind << std::endl;
		}
	}
}

int main(int argc, char **argv) {
	std::ostream *output = &std::cout;
	std::ofstream outfile;
	int i = 1;

	/* prevent segfault */
	ccls::Config config;
	g_config = &config;

	if (argc <= 1) {
		std::cerr << "Expected at least one argument" << std::endl;
		std::cerr << "usage: " << argv[0] << " SINGLE_BLOB" << std::endl;
		std::cerr << "or: " << argv[0] << " OUTFILE BLOB [BLOB ...]" << std::endl;
		return 1;
	}
	if (argc > 2) {
		std::string fn(argv[1]);
		fn += "/output-";
		fn += std::getenv("PROCESS_ID");
		outfile.open(fn, std::ios_base::app | std::ios_base::out);
		output = &outfile;
		i = 2;
	}
	for ( ; i < argc; i++) {
		std::string filename(argv[i]);
		index_file(filename, output);
	}
	if (outfile.is_open()) {
		outfile.close();
	}
	return 0;
}
