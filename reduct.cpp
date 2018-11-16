#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <variant>


enum class atom_type
{
	symbol,
	string,
	table
};


class atom;
class symbol;
class string;
class table;

using atom_ptr = std::shared_ptr<atom const>;

// these operators aren't very idiomatically C++, but they do make table_values'
// lookup and the comparison functions work with a lot less fuss.
bool operator< (atom_ptr const&, atom_ptr const&);
bool operator== (atom_ptr const&, atom_ptr const&);

auto operator<< (std::ostream& out, atom_ptr const& patom) -> std::ostream&;
void pretty_print(std::ostream& out, atom_ptr const& patom);

using table_values = std::map<atom_ptr, atom_ptr>;


class atom
{
public:
	atom(atom_type type, std::string value) : 
		m_type(type), 
		m_value(std::move(value))
	{
		assert(m_type == atom_type::symbol || m_type == atom_type::string);
	}

	explicit atom(table_values values) :
		m_type(atom_type::table),
		m_value(std::move(values))
	{
	}

	atom_type type() const { return m_type; }

	std::string const& get_value() const 
	{
		assert(type() == atom_type::symbol || type() == atom_type::string);
		return std::get<std::string>(m_value);
	}

	table_values const& get_pairs() const
	{
		assert(type() == atom_type::table);
		return std::get<table_values>(m_value);
	}

	bool operator==(atom const& rhs) const
	{
		if (this == &rhs)
		{
			return true;
		}
		else
		{
			return (m_type == rhs.m_type) && (m_value == rhs.m_value);
		}
	}

	bool operator<(atom const& rhs) const
	{
		return (m_type < rhs.m_type) 
			|| (m_type == rhs.m_type) && (m_value < rhs.m_value);
	}

private:
	using value_type = std::variant<std::string, table_values>;

	atom_type m_type;
	value_type m_value;
};


auto make_symbol(std::string value) -> atom_ptr
{
	//TODO: pool these?
	return std::make_shared<atom>(atom_type::symbol, std::move(value));
}


auto make_string(std::string value) -> atom_ptr
{
	return std::make_shared<atom>(atom_type::string, std::move(value));
}


auto make_table(table_values values) -> atom_ptr
{
	return std::make_shared<atom>(std::move(values));
}


bool is_symbol(atom_ptr const& a)
{
	return a->type() == atom_type::symbol;
}


bool is_string(atom_ptr const& a)
{
	return a->type() == atom_type::string;
}


bool is_table(atom_ptr const& a)
{
	return a->type() == atom_type::table;
}


bool operator< (atom_ptr const& lhs, atom_ptr const& rhs)
{
	return (*lhs) < (*rhs);
}


bool operator== (atom_ptr const& lhs, atom_ptr const& rhs)
{
	return (*lhs) == (*rhs);
}


namespace symbols
{
	atom_ptr const type = make_symbol("__type");
	atom_ptr const error = make_symbol("error");
	atom_ptr const statement = make_symbol("statement");

	atom_ptr const error_type = make_symbol("__error-type");
	atom_ptr const lookup_error = make_symbol("lookup-error");
	atom_ptr const read_error = make_symbol("read-error");
	atom_ptr const eval_error = make_symbol("eval-error");

	atom_ptr const map = make_symbol("map");
	atom_ptr const key = make_symbol("key");
	atom_ptr const message = make_symbol("message");

	atom_ptr const zero = make_symbol("0");
	atom_ptr const one = make_symbol("1");
}


namespace tables
{
	atom_ptr const empty = make_table({});
}


auto make_error(atom_ptr const& type, std::string const& msg, table_values data = {})
{
	table_values values = std::move(data);
	values.emplace(symbols::type, symbols::error);
	values.emplace(symbols::error_type, type);
	values.emplace(symbols::message, make_string(msg));

	return make_table(std::move(values));
}


auto lookup(atom_ptr const& map, atom_ptr const& key) -> atom_ptr
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

	table_values const& values = map->get_pairs();
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


auto lookup_eq(atom_ptr const& a, atom_ptr const& key, atom_ptr const& rhs)
{
	return is_table(a) && (lookup(a, key) == rhs);
}


bool is_statement(atom_ptr const& a)
{
	return lookup_eq(a, symbols::type, symbols::statement);
}


bool is_error(atom_ptr const& a)
{
	return lookup_eq(a, symbols::type, symbols::error);
}


auto len(atom_ptr const& a) -> size_t
{
	int n = 0;
	atom_ptr nsym = make_symbol(std::to_string(n));
	while (!is_error(lookup(a, nsym)))
	{
		nsym = make_symbol(std::to_string(++n));
	}
	return n;
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
auto read_symbol(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>
{
	auto const sym_first = si;
	while (si != last && is_symbol_char(*si))
	{
		++si;
	}
	return { si, make_symbol(std::string(sym_first, si)) };
}


template <typename StrIt>
auto read_string(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>
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
auto read_atom(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>;

template <typename StrIt>
auto read_statement(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>;


template <typename StrIt>
auto read_table(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>
{
	static auto const eof_error = make_error(symbols::read_error, "Unexpected eof while reading table");

	assert(*si == '{');
	++si; // skip opening paren
	
	table_values values;
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
		auto const [si1, key] = read_atom(si, last);
		si = si1;
		if (!key)
		{
			return { si, make_error(symbols::read_error, std::string() + "Unexpected character '" + (*si) + "'") };
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
auto read_atom(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>
{
	si = skip_ws(si, last);
	if (si == last)
	{
		return { si, nullptr };
	}

	char const c = (*si);

	// symbol?
	if (is_symbol_char(c))
	{
		return read_symbol(si, last);
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
	return { si, nullptr };
}


template <typename StrIt>
auto read_statement(StrIt si, StrIt last) -> std::pair<StrIt, atom_ptr>
{
	table_values values = { 
		{symbols::type, symbols::statement}
	};

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
				return { si, make_table(values) };
			}
		}
			
		if (is_error(result))
		{
			// propogate error
			return { si, result };
		}
		
		values.emplace(make_symbol(std::to_string(n++)), result);
	}
}


auto read(atom_ptr const& input) -> atom_ptr
{
	assert(input);
	if (!is_string(input))
	{
		return input;
	}

	std::string const& str = input->get_value();
	auto si = cbegin(str);
	auto const last = cend(str);
	auto const [new_si, result] = read_statement(si, last);
	si = new_si;

	if (si != last)
	{
		return make_error(symbols::read_error, std::string() + "Unexpected character '" + (*si) + "'");
	}

	return result;
}


auto eval(atom_ptr const& expr) -> atom_ptr
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
	atom_ptr const& map = eval(lookup(expr, symbols::zero));
	atom_ptr const& key = eval(lookup(expr, symbols::one));
	atom_ptr const& result = eval(lookup(map, key));
	if (is_error(result))
	{
		return result;
	}
	
	// Begin a new expression with the result, followed by the rest of the original
	// statement minus the map & key.
	table_values new_expr {
		{symbols::type, symbols::statement},
		{symbols::zero, result}
	};
	
	for (size_t n = 2; n < expr_len; ++n)
	{
		new_expr.emplace(
			make_symbol(std::to_string(n-1)),
			lookup(expr, make_symbol(std::to_string(n)))
		);
	}

	return make_table(std::move(new_expr));
}


template <typename Fn>
void print_table(std::ostream& out, atom_ptr const& table, Fn print_atom)
{
	assert(table->type() == atom_type::table);

	out << "{";

	std::string separator;
	for (auto kv : table->get_pairs())
	{
		out << separator;
		print_atom(out, kv.first);
		out << " = ";
		print_atom(out, kv.second);
		separator = ", ";
	}

	out << "}";
}


auto operator<< (std::ostream& out, atom_ptr const& patom) -> std::ostream&
{
	switch (patom->type())
	{
	case atom_type::symbol: { out << patom->get_value(); break; }
	case atom_type::string: { out << '"' << patom->get_value() << '"'; break; }
	case atom_type::table:  { print_table(out, patom, operator<<); break; }
	default:                { throw std::runtime_error{ "Unknown atom_type" }; }
	}
	return out;
}


void pretty_print(std::ostream& out, atom_ptr const& patom)
{
	switch (patom->type())
	{
		case atom_type::symbol:
		case atom_type::string:
		{
			out << patom;
			break;
		}

		case atom_type::table:
		{
			if (is_statement(patom))
			{
				out << "(";
				size_t const n = len(patom);
				for (size_t i = 0; i < n;)
				{
					atom_ptr statement_atom = lookup(patom, make_symbol(std::to_string(i)));
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
				print_table(out, patom, pretty_print);
			}
			break;
		}

		default:
		{
			throw std::runtime_error{ "Unknown atom_type" };
		}
	}
}


auto prompt_user() -> atom_ptr
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
		atom_ptr const input = prompt_user();

		atom_ptr value = read(input);
		if (is_error(value))
		{
			std::cout << "Read error: " << lookup(value, symbols::message)->get_value() << "\n\n";
			continue;
		}
		
		// eval
		while (is_statement(value))
		{
			value = eval(value);
		}

		// print
		if (is_error(value))
		{
			std::cout << "Eval error: " << lookup(value, symbols::message)->get_value();
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