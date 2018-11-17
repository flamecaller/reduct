#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <variant>


class atom;

// these operators aren't very idiomatically C++, but they do make table_values'
// lookup and the comparison functions work with a lot less fuss.
bool operator< (atom const&, atom const&);
bool operator== (atom const&, atom const&);

auto operator<< (std::ostream& out, atom const& atom) -> std::ostream&;
void pretty_print(std::ostream& out, atom const& atom);

using table_values = std::map<atom, atom>;


enum class atom_type
{
	symbol,
	string,
	table,
	substitution // last so that it always appears last in printed tables, which makes more semantic sense
};


class atom
{
public:
	atom(atom_type type, std::string value) : 
		m_type(type), 
		m_value(std::move(value))
	{
		assert(is_string_type());
	}

	explicit atom(table_values values) :
		m_type(atom_type::table),
		m_value(std::move(values))
	{
	}

	atom_type type() const { return m_type; }

	std::string const& get_value() const 
	{
		assert(is_string_type());
		return std::get<std::string>(m_value);
	}

	table_values const& get_pairs() const
	{
		assert(is_table_type());
		return std::get<table_values>(m_value);
	}

	auto get_universal_lookup_pair() const -> std::optional<std::pair<atom, atom>>
	{
		assert(is_table_type());
		table_values const& pairs = get_pairs();
		auto it = std::find_if(
			cbegin(pairs), cend(pairs),
			[](auto const& kv) { return kv.first.type() == atom_type::substitution; }
		);

		if (it == cend(pairs))
		{
			return std::nullopt;
		}

		return (*it);
	}

	friend bool operator==(atom const& lhs, atom const& rhs)
	{
		if (&lhs == &rhs)
		{
			return true;
		}
		else
		{
			return (lhs.m_type == rhs.m_type) && (lhs.m_value == rhs.m_value);
		}
	}

	friend bool operator<(atom const& lhs, atom const& rhs)
	{
		return (lhs.m_type < rhs.m_type) 
			|| (lhs.m_type == rhs.m_type) && (lhs.m_value < rhs.m_value);
	}

private:
	bool is_string_type() const
	{
		return m_type == atom_type::symbol
			|| m_type == atom_type::string
			|| m_type == atom_type::substitution;
	}

	bool is_table_type() const
	{
		return m_type == atom_type::table;
	}

private:
	using value_type = std::variant<std::string, table_values>;

	atom_type m_type;
	value_type m_value;
};


bool operator!= (atom const& lhs, atom const& rhs)
{
	return !(lhs == rhs);
}


auto make_symbol(std::string value) -> atom
{
	//TODO: pool these?
	return atom(atom_type::symbol, std::move(value));
}


auto make_substitution(std::string value) -> atom
{
	return atom(atom_type::substitution, std::move(value));
}


auto make_string(std::string value) -> atom
{
	return atom(atom_type::string, std::move(value));
}


auto make_table(table_values values) -> atom
{
	return atom(std::move(values));
}


auto is_symbol(atom const& a) -> bool
{
	return a.type() == atom_type::symbol;
}


auto is_substitution(atom const& a) -> bool
{
	return a.type() == atom_type::substitution;
}


auto is_string(atom const& a) -> bool
{
	return a.type() == atom_type::string;
}


auto is_table(atom const& a) -> bool
{
	return a.type() == atom_type::table;
}


namespace symbols
{
	atom const type = make_symbol("__type");
	atom const error = make_symbol("error");
	atom const statement = make_symbol("statement");

	atom const error_type = make_symbol("__error-type");
	atom const lookup_error = make_symbol("lookup-error");
	atom const read_error = make_symbol("read-error");
	atom const eval_error = make_symbol("eval-error");

	atom const map = make_symbol("map");
	atom const key = make_symbol("key");
	atom const message = make_symbol("message");

	atom const universal_lookup_key = make_symbol("universal_lookup_key");
	atom const universal_lookup_expr = make_symbol("universal_lookup_expr");
	atom const value = make_symbol("value");

	atom const zero = make_symbol("0");
	atom const one = make_symbol("1");
}


namespace tables
{
	atom const empty = make_table({});
}


auto make_error(atom const& type, std::string const& msg, table_values data = {})
{
	table_values values = std::move(data);
	values.emplace(symbols::type, symbols::error);
	values.emplace(symbols::error_type, type);
	values.emplace(symbols::message, make_string(msg));

	return make_table(std::move(values));
}


auto make_statement(table_values atoms) -> atom
{
	table_values values = std::move(atoms);
	values.emplace(symbols::type, symbols::statement);

	return make_table(std::move(values));
}


auto lookup(atom const& map, atom const& key) -> atom
{
	if (!is_table(map))
	{
		return make_error(
			symbols::lookup_error,
			"Expected a table for lookup",
			{
				{symbols::map, map},
				{symbols::key, key}
			}
		);
	}

	table_values const& values = map.get_pairs();
	auto const it = values.find(key);
	if (it != cend(values))
	{
		return it->second;
	}
	else
	{
		return make_error(
			symbols::lookup_error, 
			"Could not find key in table", 
			{
				{symbols::map, map},
				{symbols::key, key}
			}
		);
	}
}


auto lookup_eq(atom const& a, atom const& key, atom const& rhs)
{
	return is_table(a) && (lookup(a, key) == rhs);
}


bool is_statement(atom const& a)
{
	return lookup_eq(a, symbols::type, symbols::statement);
}


bool is_error(atom const& a)
{
	return lookup_eq(a, symbols::type, symbols::error);
}


auto len(atom const& a) -> size_t
{
	int n = 0;
	atom nsym = make_symbol(std::to_string(n));
	while (!is_error(lookup(a, nsym)))
	{
		nsym = make_symbol(std::to_string(++n));
	}
	return n;
}


auto universal_lookup(atom const& map, atom const& key) -> atom
{
	auto const pul = map.get_universal_lookup_pair();
	if (!pul)
	{
		return make_error(
			symbols::lookup_error,
			"Could not find key in table",
			{
				{symbols::map, map},
				{symbols::key, key}
			}
		);
	}

	// perform substitution
	atom const& ul_key = pul->first;
	atom const& ul_expr = pul->second;

	auto fn_substitute = 
		[&map, &key, &ul_key](atom const& a)
		{
			if (is_substitution(a) && (ul_key != a))
			{
				//TODO: make a read-time error?
				return make_error(
					symbols::lookup_error,
					"Mismatch between substitution key and expression",
					{
						{symbols::map, map},
						{symbols::key, key},
						{symbols::universal_lookup_key, ul_key},
						{symbols::value, a}
					}
				);
			}

			return (a == ul_key) ? key : a;
		};
		

	// Single value
	if (!is_statement(ul_expr))
	{
		return fn_substitute(ul_expr);
	}

	// Statement
	table_values new_expr;
	size_t const ul_expr_len = len(ul_expr);
	for (size_t i = 0; i < ul_expr_len; ++i)
	{
		atom const isym = make_symbol(std::to_string(i));
		atom const& v = lookup(ul_expr, isym);
		new_expr.emplace(isym, fn_substitute(v));
	}
	return make_statement(new_expr);
}


bool is_symbol_char(char c)
{
	return isalnum(c)
		|| c == '_'
		|| c == '!'
		|| c == '?'
		|| c == '+'
		|| c == '-'
		|| c == '*'
		|| c == '/'
		|| c == '%';
}


template <typename StrIt>
auto skip_ws(StrIt si, StrIt last) -> StrIt
{
	while (si != last && isspace(*si))
	{
		++si;
	}
	return si;
}


template <typename StrIt>
auto read_symbol(StrIt si, StrIt last) -> std::pair<StrIt, atom>
{
	auto const sym_first = si;
	while (si != last && is_symbol_char(*si))
	{
		++si;
	}
	return { si, make_symbol(std::string(sym_first, si)) };
}


template <typename StrIt>
auto read_substitution(StrIt si, StrIt last) -> std::pair<StrIt, atom>
{
	assert(*si == '$');
	++si; // skip $

	auto const sub_first = si;
	while (si != last && is_symbol_char(*si))
	{
		++si;
	}
	return { si, make_substitution(std::string(sub_first, si)) };
}


template <typename StrIt>
auto read_string(StrIt si, StrIt last) -> std::pair<StrIt, atom>
{
	char const quote_char = (*si);

	++si; // skip start of string character

	std::ostringstream ss;
	while (true)
	{
		if (si == last)
		{
			return { si, make_error(symbols::read_error, "Unexpected eof while reading string") };
		}

		char const c = (*si);
		if (c == '\\')
		{
			++si;
			if (si != last)
			{
				ss << (*si++);
			}
		}
		else if (c == quote_char)
		{
			break; // done
		}
		else
		{
			ss << (*si++);
		}
	};

	++si; // skip end of string character

	return { si, make_string(ss.str()) };
}


template <typename StrIt>
auto read_atom(StrIt si, StrIt last) -> std::pair<StrIt, std::optional<atom>>;

template <typename StrIt>
auto read_statement(StrIt si, StrIt last) -> std::pair<StrIt, atom>;


template <typename StrIt>
auto read_table(StrIt si, StrIt last) -> std::pair<StrIt, atom>
{
	static auto const eof_error = make_error(symbols::read_error, "Unexpected eof while reading table");

	assert(*si == '{');
	++si; // skip opening paren
	
	table_values values;
	bool has_sub = false;
	while (true)
	{
		si = skip_ws(si, last);
		if (si == last)
		{
			return{ si, eof_error };
		}

		// done?
		if (*si == '}')
		{
			break;
		}

		// key
		auto const [si1, pkey] = read_atom(si, last);
		si = si1;
		if (!pkey)
		{
			return { si, make_error(symbols::read_error, std::string() + "Unexpected character '" + (*si) + "'") };
		}
		atom const& key = (*pkey);

		if (is_substitution(key))
		{
			if (has_sub)
			{
				return { si, make_error(symbols::read_error, "Table has more than one universal substitution.") };
			}
			has_sub = true;
		}

		// =
		si = skip_ws(si, last);
		if (si == last)
		{
			return{ si, eof_error };
		}
		else if ((*si) != '=')
		{
			return { si, make_error(symbols::read_error, std::string() + "Unexpected character '" + (*si) + "' (expected '=')") };
		}
		else
		{
			++si;
		}
		
		// value
		auto const [si2, value] = read_statement(si, last);
		si = si2;
		if (is_error(value))
		{
			return { si, value };
		}

		values.emplace(key, value);

		// ,
		si = skip_ws(si, last);
		if (si == last)
		{
			return{ si, eof_error };
		}
		else if ((*si) == ',')
		{
			++si;
		}
	}
	++si; // skip closing paren

	return { si, make_table(values) };
}


template <typename StrIt>
auto read_atom(StrIt si, StrIt last) -> std::pair<StrIt, std::optional<atom>>
{
	si = skip_ws(si, last);
	if (si == last)
	{
		return { si, std::nullopt };
	}

	char const c = (*si);

	// symbol?
	if (is_symbol_char(c))
	{
		return read_symbol(si, last);
	}

	// subtitution?
	if (c == '$')
	{
		return read_substitution(si, last);
	}

	// string?
	if (c == '"' || c == '\'')
	{
		return read_string(si, last);
	}

	// table?
	if (c == '{')
	{
		return read_table(si, last);
	}

	// not an atom
	return { si, std::nullopt };
}


template <typename StrIt>
auto read_statement(StrIt si, StrIt last) -> std::pair<StrIt, atom>
{
	table_values values;

	int n = 0;
	while (true)
	{
		auto [new_si, result] = read_atom(si, last);
		si = new_si;

		if (!result)
		{
			// we read as many atoms as we can, so we're done
			if (values.empty())
			{
				return { si, make_error(symbols::read_error, "Expected a statement") };
			}
			else if (n == 1)
			{
				// if only a single value, unwrap it from the statement for cleaner 
				// reading of value types
				return { si, values.find(symbols::zero)->second };
			}
			else
			{
				return { si, make_statement(values) };
			}
		}
		
		atom const& a = (*result);
		if (is_error(a))
		{
			// propogate error
			return { si, a };
		}
		
		values.emplace(make_symbol(std::to_string(n++)), a);
	}
}


auto read(atom const& input) -> atom
{
	if (!is_string(input))
	{
		return input;
	}

	std::string const& str = input.get_value();
	auto si = cbegin(str);
	auto const last = cend(str);
	auto const [new_si, result] = read_statement(si, last);
	si = new_si;

	if (!is_error(result) && si != last)
	{
		return make_error(symbols::read_error, std::string() + "Unexpected character '" + (*si) + "'");
	}

	return result;
}


auto eval(atom const& expr) -> atom
{
	// non-statement evals to itself
	if (!is_statement(expr))
	{
		return expr;
	}

	// statement of length one returns eval of its item
	size_t const expr_len = len(expr);
	if (expr_len == 1)
	{
		return eval(lookup(expr, symbols::zero));
	}

	// statement of more than one item performs a lookup
	atom const& map = eval(lookup(expr, symbols::zero));
	atom const& key = eval(lookup(expr, symbols::one));
	atom result = lookup(map, key);
	if (is_error(result))
	{
		// specific lookup failed, try universal lookup
		if (is_table(map))
		{
			result = universal_lookup(map, key);
		}
	}

	if (expr_len == 2 || is_error(result))
	{
		return result;
	}
	
	// Begin a new expression with the result, followed by the rest of the original
	// statement minus the map & key.
	table_values new_expr {
		{symbols::zero, result}
	};
	
	for (size_t n = 2; n < expr_len; ++n)
	{
		new_expr.emplace(
			make_symbol(std::to_string(n-1)),
			lookup(expr, make_symbol(std::to_string(n)))
		);
	}

	return make_statement(std::move(new_expr));
}


template <typename Fn>
void print_table(std::ostream& out, atom const& table, Fn print_atom)
{
	assert(is_table(table));

	out << "{";

	std::string separator;
	for (auto kv : table.get_pairs())
	{
		out << separator;
		print_atom(out, kv.first);
		out << " = ";
		print_atom(out, kv.second);
		separator = ", ";
	}

	out << "}";
}


auto operator<< (std::ostream& out, atom const& a) -> std::ostream&
{
	switch (a.type())
	{
	case atom_type::symbol:       { out << a.get_value(); break; }
	case atom_type::substitution: { out << '$' << a.get_value(); break; }
	case atom_type::string:       { out << '"' << a.get_value() << '"'; break; }
	case atom_type::table:        { print_table(out, a, operator<<); break; }
	default:                      { throw std::runtime_error{ "Unknown atom_type" }; }
	}
	return out;
}


void pretty_print(std::ostream& out, atom const& a)
{
	switch (a.type())
	{
		case atom_type::symbol:
		case atom_type::substitution:
		case atom_type::string:
		{
			out << a;
			break;
		}

		case atom_type::table:
		{
			if (is_statement(a))
			{
				out << "(";
				size_t const n = len(a);
				for (size_t i = 0; i < n;)
				{
					atom statement_atom = lookup(a, make_symbol(std::to_string(i)));
					pretty_print(out, statement_atom);
					if (++i < n)
					{
						out << ' ';
					}
				}
				out << ")";
			}
			else
			{
				print_table(out, a, pretty_print);
			}
			break;
		}

		default:
		{
			throw std::runtime_error{ "Unknown atom_type" };
		}
	}
}


auto prompt_user() -> atom
{
	char const* const prompt = "> ";
	auto const prompt_length = strlen(prompt);

	std::string line;
	std::cout << prompt;
	std::getline(std::cin, line);

	return make_string(std::move(line));
}


void repl()
{
	while (true)
	{
		// read
		atom const input = prompt_user();

		atom value = read(input);
		if (is_error(value))
		{
			std::cout << "Read error: " << lookup(value, symbols::message).get_value() << "\n\n";
			continue;
		}
		
		// eval
		std::set<atom> known_states;
		bool infinite_loop = false;
		while (is_statement(value))
		{
			std::cout << "=> ";
			pretty_print(std::cout, value);
			std::cout << "\n";
			value = eval(value);

			if (known_states.find(value) != known_states.end())
			{
				infinite_loop = true;
				break;
			}
			else
			{
				known_states.emplace(value);
			}
		}

		// print
		if (infinite_loop)
		{
			std::cout << "Infinite loop detected, bailing";
		}
		else if (is_error(value))
		{
			std::cout << "Eval error: " << lookup(value, symbols::message).get_value();
		}
		else
		{
			pretty_print(std::cout, value);
		}
		std::cout << "\n\n";
	}
}


int main(int argc, char* argv[])
{
	// quick tests
	//TODO: more comprehensive tests
	assert( make_symbol("test") == make_symbol("test") );
	assert( make_symbol("test") != make_symbol("two") );
	assert( make_symbol("test") != make_string("test") );
	assert( tables::empty == make_table({}) );
	assert( tables::empty != make_table({ {symbols::zero, symbols::one} }));
	assert( make_table({ {make_symbol("0"), make_symbol("1")} }) == make_table({ {make_symbol("0"), make_symbol("1")} }) );

	assert(is_error( lookup(tables::empty, symbols::type) ));
	assert(is_error( lookup(tables::empty, tables::empty) ));
	assert(is_error( lookup(tables::empty, make_table({})) ));
	assert(lookup(make_table({ {make_symbol("foo"), make_symbol("bar")} }), make_symbol("foo")) == make_symbol("bar"));

	// repl
	repl();
}