/*
	Copyright (C) 2005 - 2025
	by Philippe Plantier <ayin@anathas.org>
	Copyright (C) 2005 by Guillaume Melquiond <guillaume.melquiond@gmail.com>
	Copyright (C) 2003 by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 * Read/Write & analyze WML- and config-files.
 */

#include "serialization/parser.hpp"

#include "config.hpp"
#include "gettext.hpp"
#include "log.hpp"
#include "serialization/preprocessor.hpp"
#include "serialization/string_utils.hpp"
#include "serialization/validator.hpp"
#include "utils/charconv.hpp"
#include "wesconfig.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4456)
#pragma warning(disable : 4458)
#endif

#include <boost/iostreams/filter/gzip.hpp>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <stack>

static lg::log_domain log_config("config");
#define ERR_CF LOG_STREAM(err, log_config)
#define WRN_CF LOG_STREAM(warn, log_config)
#define LOG_CF LOG_STREAM(info, log_config)

constexpr std::size_t max_recursion_levels = 1000;

namespace io
{
namespace
{
// ==================================================================================
// PARSER
// ==================================================================================

class parser
{
public:
	parser(std::istream& in, abstract_validator* validator = nullptr)
		: tok_(in)
		, validator_(validator)
		, elements()
	{
	}

	config operator()();

private:
	void parse_element();
	void parse_variable();

	std::string lineno_string(utils::string_map& map,
			const std::string& lineno,
			const std::string& error_string,
			const std::string& hint_string = "",
			const std::string& debug_string = "");

	/** @throws config::error */
	[[noreturn]] void error(const std::string& message, const std::string& pos_format = "");

	struct element
	{
#ifndef __cpp_aggregate_paren_init
		element(config* cfg, const std::string& name, int start_line = 0, const std::string& file = "")
			: cfg(cfg)
			, name(name)
			, start_line(start_line)
			, file(file)
		{
		}
#endif

		config* cfg{};
		std::string name{};
		int start_line{};
		std::string file{};
	};

	tokenizer tok_;
	abstract_validator* validator_;

	std::stack<element> elements;
};

config parser::operator()()
{
	config res;
	elements.emplace(&res, "");

	if(validator_) {
		validator_->open_tag("", res, tok_.get_start_line(), tok_.get_file());
	}

	do {
		tok_.next_token();

		switch(tok_.current_token().type) {
		case token::NEWLINE:
			continue;

		case token::OPEN_BRACKET:
			parse_element();
			break;

		case token::STRING:
			parse_variable();
			break;

		default:
			if(static_cast<unsigned char>(tok_.current_token().value[0]) == 0xEF &&
			   static_cast<unsigned char>(tok_.next_token().value[0])    == 0xBB &&
			   static_cast<unsigned char>(tok_.next_token().value[0])    == 0xBF
			) {
				utils::string_map i18n_symbols;
				std::stringstream ss;
				ss << tok_.get_start_line() << " " << tok_.get_file();
				ERR_CF << lineno_string(i18n_symbols, ss.str(), "Skipping over a utf8 BOM at $pos");
			} else {
				error(_("Unexpected characters at line start"));
			}

			break;

		case token::END:
			break;
		}
	} while(tok_.current_token().type != token::END);

	// The main element should be there. If it is not, this is a parser error.
	assert(!elements.empty());

	if(validator_) {
		element& el = elements.top();
		validator_->validate(*el.cfg, el.name, el.start_line, el.file);
		validator_->close_tag();
	}

	if(elements.size() != 1) {
		utils::string_map i18n_symbols;
		i18n_symbols["tag"] = elements.top().name;

		std::stringstream ss;
		ss << elements.top().start_line << " " << elements.top().file;

		error(lineno_string(i18n_symbols, ss.str(),
			_("Missing closing tag for tag [$tag]"),
			_("expected at $pos")),
			_("opened at $pos")
		);
	}

	return res;
}

void parser::parse_element()
{
	tok_.next_token();

	std::string elname;
	config* current_element = nullptr;
	config* parent = nullptr;

	switch(tok_.current_token().type) {
	case token::STRING: // [element]
		elname = tok_.current_token().value;

		if(tok_.next_token().type != token::CLOSE_BRACKET) {
			error(_("Unterminated [element] tag"));
		}

		// Add the element
		parent = elements.top().cfg;
		current_element = &(parent->add_child(elname));
		elements.emplace(current_element, elname, tok_.get_start_line(), tok_.get_file());

		if(validator_) {
			validator_->open_tag(elname, *parent, tok_.get_start_line(), tok_.get_file());
		}

		break;

	case token::PLUS: // [+element]
		if(tok_.next_token().type != token::STRING) {
			error(_("Invalid tag name"));
		}

		elname = tok_.current_token().value;

		if(tok_.next_token().type != token::CLOSE_BRACKET) {
			error(_("Unterminated [+element] tag"));
		}

		// Find the last child of the current element whose name is element
		parent = elements.top().cfg;
		if(auto c = parent->optional_child(elname, -1)) {
			current_element = c.ptr();

			if(validator_) {
				validator_->open_tag(elname, *parent, tok_.get_start_line(), tok_.get_file(), true);
			}
		} else {
			current_element = &parent->add_child(elname);

			if(validator_) {
				validator_->open_tag(elname, *parent, tok_.get_start_line(), tok_.get_file());
			}
		}

		elements.emplace(current_element, elname, tok_.get_start_line(), tok_.get_file());
		break;

	case token::SLASH: // [/element]
		if(tok_.next_token().type != token::STRING) {
			error(_("Invalid closing tag name"));
		}

		elname = tok_.current_token().value;

		if(tok_.next_token().type != token::CLOSE_BRACKET) {
			error(_("Unterminated closing tag"));
		}

		if(elements.size() <= 1) {
			error(_("Unexpected closing tag"));
		}

		if(elname != elements.top().name) {
			utils::string_map i18n_symbols;
			i18n_symbols["tag1"] = elements.top().name;
			i18n_symbols["tag2"] = elname;

			std::stringstream ss;
			ss << elements.top().start_line << " " << elements.top().file;

			error(lineno_string(i18n_symbols, ss.str(),
				_("Found invalid closing tag [/$tag2] for tag [$tag1]"),
				_("opened at $pos")),
				_("closed at $pos")
			);
		}

		if(validator_) {
			element& el = elements.top();
			validator_->validate(*el.cfg, el.name, el.start_line, el.file);
			validator_->close_tag();
		}

		elements.pop();
		break;

	default:
		error(_("Invalid tag name"));
	}
}

void parser::parse_variable()
{
	config& cfg = *elements.top().cfg;
	std::vector<std::string> variables;
	variables.emplace_back();

	while(tok_.current_token().type != token::EQUALS) {
		switch(tok_.current_token().type) {
		case token::STRING:
			if(!variables.back().empty()) {
				variables.back() += ' ';
			}

			variables.back() += tok_.current_token().value;
			break;

		case token::COMMA:
			if(variables.back().empty()) {
				error(_("Empty variable name"));
			} else {
				variables.emplace_back();
			}

			break;

		default:
			error(_("Unexpected characters after variable name (expected , or =)"));
			break;
		}

		tok_.next_token();
	}

	if(variables.back().empty()) {
		error(_("Empty variable name"));
	}

	t_string_base buffer;

	std::vector<std::string>::const_iterator curvar = variables.begin();

	bool ignore_next_newlines = false, previous_string = false;

	while(true) {
		tok_.next_token();
		assert(curvar != variables.end());

		switch(tok_.current_token().type) {
		case token::COMMA:
			if((curvar + 1) != variables.end()) {
				if(buffer.translatable()) {
					cfg[*curvar] = t_string(buffer);
				} else {
					cfg[*curvar] = buffer.value();
				}

				if(validator_) {
					validator_->validate_key(cfg, *curvar, cfg[*curvar], tok_.get_start_line(), tok_.get_file());
				}

				buffer = t_string_base();
				++curvar;
			} else {
				buffer += ",";
			}

			break;

		case token::UNDERSCORE:
			tok_.next_token();

			switch(tok_.current_token().type) {
			case token::UNTERMINATED_QSTRING:
				error(_("Unterminated quoted string"));
				break;

			case token::QSTRING:
				buffer += t_string_base(tok_.current_token().value, tok_.textdomain());
				break;

			default:
				buffer += "_";
				buffer += tok_.current_token().value;
				break;

			case token::END:
			case token::NEWLINE:
				buffer += "_";
				goto finish;
			}

			break;

		case token::PLUS:
			ignore_next_newlines = true;
			continue;

		case token::STRING:
			if(previous_string) {
				buffer += " ";
			}

			[[fallthrough]];

		default:
			buffer += tok_.current_token().value;
			break;

		case token::QSTRING:
			buffer += tok_.current_token().value;
			break;

		case token::UNTERMINATED_QSTRING:
			error(_("Unterminated quoted string"));
			break;

		case token::NEWLINE:
			if(ignore_next_newlines) {
				continue;
			}

			[[fallthrough]];

		case token::END:
			goto finish;
		}

		previous_string = tok_.current_token().type == token::STRING;
		ignore_next_newlines = false;
	}

finish:

	if(buffer.translatable()) {
		cfg[*curvar] = t_string(buffer);
	} else {
		cfg[*curvar] = buffer.value();
	}

	if(validator_) {
		validator_->validate_key(cfg, *curvar, cfg[*curvar], tok_.get_start_line(), tok_.get_file());
	}

	while(++curvar != variables.end()) {
		cfg[*curvar] = "";
	}
}

/**
 * This function is crap. Don't use it on a string_map with prefixes.
 */
std::string parser::lineno_string(utils::string_map& i18n_symbols,
		const std::string& lineno,
		const std::string& error_string,
		const std::string& hint_string,
		const std::string& debug_string)
{
	i18n_symbols["pos"] = ::lineno_string(lineno);
	std::string result = error_string;

	if(!hint_string.empty()) {
		result += '\n' + hint_string;
	}

	if(!debug_string.empty()) {
		result += '\n' + debug_string;
	}

	for(utils::string_map::value_type& var : i18n_symbols) {
		boost::algorithm::replace_all(result, std::string("$") + var.first, std::string(var.second));
	}

	return result;
}

void parser::error(const std::string& error_type, const std::string& pos_format)
{
	std::string hint_string = pos_format;

	if(hint_string.empty()) {
		hint_string = _("at $pos");
	}

	utils::string_map i18n_symbols;
	i18n_symbols["error"] = error_type;

	std::stringstream ss;
	ss << tok_.get_start_line() << " " << tok_.get_file();

#ifdef DEBUG_TOKENIZER
	i18n_symbols["value"] = tok_.current_token().value;
	i18n_symbols["previous_value"] = tok_.previous_token().value;

	const std::string tok_state = _("Value: ‘$value’ Previous: ‘$previous_value’");
#else
	const std::string tok_state = "";
#endif

	throw config::error(lineno_string(i18n_symbols, ss.str(), "$error", hint_string, tok_state));
}


// ==================================================================================
// HELPERS FOR WRITE_KEY_VAL
// ==================================================================================

/**
 * Copies a string fragment and converts it to a suitable format for WML.
 * (I.e., quotes are doubled.)
 */
std::string escaped_string(const std::string::const_iterator& begin, const std::string::const_iterator& end)
{
	std::string res;
	std::string::const_iterator iter = begin;

	while(iter != end) {
		const char c = *iter;
		res.append(c == '"' ? 2 : 1, c);
		++iter;
	}

	return res;
}

/**
 * Copies a string and converts it to a suitable format for WML.
 * (I.e., quotes are doubled.)
 */
inline std::string escaped_string(const std::string& value)
{
	return escaped_string(value.begin(), value.end());
}

class write_key_val_visitor
#ifdef USING_BOOST_VARIANT
	: public boost::static_visitor<void>
#endif
{
public:
	write_key_val_visitor(std::ostream& out, unsigned level, std::string& textdomain, const std::string& key)
		: out_(out)
		, level_(level)
		, textdomain_(textdomain)
		, key_(key)
	{
	}

	// Generic visitor just streams "key=value".
	template<typename T>
	void operator()(const T& v) const
	{
		indent();
		if constexpr(std::is_arithmetic_v<T>) {
			// for number values, this has to use the same method as in from_string_verify
			auto buf = utils::charconv_buffer(v);
			out_ << key_ << '=' << buf.get_view() << '\n';
		} else {
			out_ << key_ << '=' << v << '\n';
		}
	}

	//
	// Specialized visitors for things that go in quotes:
	//

	void operator()(const utils::monostate&) const
	{
		// Treat blank values as nonexistent which fits better than treating them as empty strings.
	}

	void operator()(const std::string& s) const
	{
		indent();
		out_ << key_ << '=' << '"' << escaped_string(s) << '"' << '\n';
	}

	void operator()(const t_string& s) const;

private:
	void indent() const
	{
		for(unsigned i = 0; i < level_; ++i) {
			out_ << '\t';
		}
	}

	std::ostream& out_;
	const unsigned level_;
	std::string& textdomain_;
	const std::string& key_;
};

/**
 * Writes all the parts of a translatable string.
 *
 * @note If the first part is translatable and in the wrong textdomain,
 *       the textdomain change has to happen before the attribute name.
 *       That is the reason for not outputting the key beforehand and
 *       letting this function do it.
 */
void write_key_val_visitor::operator()(const t_string& value) const
{
	bool first = true;

	for(t_string::walker w(value); !w.eos(); w.next()) {
		if(!first) {
			out_ << " +\n";
		}

		if(w.translatable() && w.textdomain() != textdomain_) {
			textdomain_ = w.textdomain();
			out_ << "#textdomain " << textdomain_ << '\n';
		}

		indent();

		if(first) {
			out_ << key_ << '=';
		} else {
			out_ << '\t';
		}

		if(w.translatable()) {
			out_ << '_';
		}

		out_ << '"' << escaped_string(w.begin(), w.end()) << '"';
		first = false;
	}

	out_ << '\n';
}

} // end anon namespace


// ==================================================================================
// PUBLIC FUNCTION IMPLEMENTATIONS
// ==================================================================================

config read(std::istream& in, abstract_validator* validator)
{
	return parser(in, validator)();
}

config read(const std::string& in, abstract_validator* validator)
{
	std::istringstream ss(in);
	return parser(ss, validator)();
}

template<typename Decompressor>
config read_compressed(std::istream& file, abstract_validator* validator)
{
	// An empty gzip file seems to confuse boost on MSVC, so return early if this is the case.
	if(file.peek() == EOF) {
		return {};
	}

	boost::iostreams::filtering_stream<boost::iostreams::input> filter;
	filter.push(Decompressor());
	filter.push(file);

	/* This causes gzip_error (and the corresponding bz2 error, std::ios_base::failure) to be
	 * thrown here. save_index_class::data expects that and config_cache::read_cache and other
	 * functions are also capable of catching.
	 *
	 * Note that parser(cuff, filter,validator)(); -> tokenizer::tokenizer can throw exceptions
	 * too (meaning this function already threw these exceptions before this patch).
	 *
	 * We try to fix https://svn.boost.org/trac/boost/ticket/5237 by not creating empty gz files.
	 */
	filter.exceptions(filter.exceptions() | std::ios_base::badbit);

	/*
	 * It sometimes seems the file is not empty but still has no real data.
	 * Filter that case here. The previous test might be no longer required but keep it for now.
	 *
	 * On msvc filter.peek() != EOF does not imply filter.good().
	 * We never create empty compressed gzip files because boosts gzip fails at doing that, but
	 * empty compressed bz2 files are possible.
	 */
	if(filter.peek() == EOF) {
		LOG_CF << "Empty compressed file or error at reading a compressed file.";
		return {};
	}

	if(!filter.good()) {
		LOG_CF << " filter.peek() != EOF but !filter.good()."
		       << "This indicates a malformed gz stream and can make Wesnoth crash.";
	}

	return parser(filter, validator)();
}

/** Might throw a std::ios_base::failure especially a gzip_error. */
config read_gz(std::istream& file, abstract_validator* validator)
{
	return read_compressed<boost::iostreams::gzip_decompressor>(file, validator);
}

/** Might throw a std::ios_base::failure especially bzip2_error. */
config read_bz2(std::istream& file, abstract_validator* validator)
{
	return read_compressed<boost::iostreams::bzip2_decompressor>(file, validator);
}

void write_key_val(std::ostream& out,
		const std::string& key,
		const config::attribute_value& value,
		unsigned level,
		std::string& textdomain)
{
	value.apply_visitor(write_key_val_visitor(out, level, textdomain, key));
}

void write_open_child(std::ostream& out, const std::string& child, unsigned int level)
{
	out << std::string(level, '\t') << '[' << child << "]\n";
}

void write_close_child(std::ostream& out, const std::string& child, unsigned int level)
{
	out << std::string(level, '\t') << "[/" << child << "]\n";
}

static void write_internal(const config& cfg, std::ostream& out, std::string& textdomain, std::size_t tab = 0)
{
	if(tab > max_recursion_levels) {
		throw config::error("Too many recursion levels in config write");
	}

	for(const auto& [key, value] : cfg.attribute_range()) {
		if(!config::valid_attribute(key)) {
			ERR_CF << "Config contains invalid attribute name '" << key << "', skipping...";
			continue;
		}

		write_key_val(out, key, value, tab, textdomain);
	}

	for(const auto [key, item_cfg] : cfg.all_children_view()) {
		if(!config::valid_tag(key)) {
			ERR_CF << "Config contains invalid tag name '" << key << "', skipping...";
			continue;
		}

		write_open_child(out, key, tab);
		write_internal(item_cfg, out, textdomain, tab + 1);
		write_close_child(out, key, tab);
	}
}

static void write_internal(const configr_of& cfg, std::ostream& out, std::string& textdomain, std::size_t tab = 0)
{
	if(tab > max_recursion_levels) {
		throw config::error("Too many recursion levels in config write");
	}

	if(cfg.data_) {
		write_internal(*cfg.data_, out, textdomain, tab);
	}

	for(const auto& pair : cfg.subtags_) {
		assert(pair.first && pair.second);

		if(!config::valid_tag(*pair.first)) {
			ERR_CF << "Config contains invalid tag name '" << *pair.first << "', skipping...";
			continue;
		}

		write_open_child(out, *pair.first, tab);
		write_internal(*pair.second, out, textdomain, tab + 1);
		write_close_child(out, *pair.first, tab);
	}
}

void write(std::ostream& out, const configr_of& cfg, unsigned int level)
{
	std::string textdomain = PACKAGE;
	write_internal(cfg, out, textdomain, level);
}

template<typename Compressor>
void write_compressed(std::ostream& out, const configr_of& cfg)
{
	boost::iostreams::filtering_stream<boost::iostreams::output> filter;
	filter.push(Compressor());
	filter.push(out);

	write(filter, cfg);

	// prevent empty gz files because of https://svn.boost.org/trac/boost/ticket/5237
	filter << "\n";
}

void write_gz(std::ostream& out, const configr_of& cfg)
{
	write_compressed<boost::iostreams::gzip_compressor>(out, cfg);
}

void write_bz2(std::ostream& out, const configr_of& cfg)
{
	write_compressed<boost::iostreams::bzip2_compressor>(out, cfg);
}

} // namespace io
