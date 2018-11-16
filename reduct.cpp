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
bool operator< (atom_ptr const&, atom_ptr const&);
bool operator== (atom_ptr const&, atom_ptr const&);

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
	if (lhs->type() != rhs->type())
	{
		return (static_cast<int>(lhs->type()) < static_cast<int>(rhs->type()));
	}

	switch (lhs->type())
	{
		case atom_type::symbol:
		case atom_type::string:
		{
			return lhs->get_value().compare(rhs->get_value()) < 0;
		}

		case atom_type::table:
		{
			auto const& lp = lhs->get_pairs();
			auto const& rp = rhs->get_pairs();
			return std::lexicographical_compare(
				cbegin(lp), cend(lp),
				cbegin(rp), cend(rp)
			);
		}

		default:
		{
			throw std::runtime_error{ "Unknown atom_type" };
		}
	}
}


bool operator== (atom_ptr const& lhs, atom_ptr const& rhs)
{
	if (lhs.get() == rhs.get())
	{
		return true;
	}

	if (lhs->type() != rhs->type())
	{
		return false;
	}

	switch (lhs->type())
	{
	case atom_type::symbol:
	case atom_type::string:
	{
		return lhs->get_value().compare(rhs->get_value()) == 0;
	}

	case atom_type::table:
	{
		return false;
	}

	default:
	{
		throw std::runtime_error{ "Unknown atom_type" };
	}
	}
}


namespace symbols
{
	atom_ptr const type = make_symbol("__type");
	atom_ptr const error = make_symbol("error");
	atom_ptr const statement = make_symbol("statement");

	atom_ptr const error_type = make_symbol("__error-type");
	atom_ptr const read_error = make_symbol("read-error");
	atom_ptr const lookup_error = make_symbol("lookup-error");

	atom_ptr const map = make_symbol("map");
	atom_ptr const key = make_symbol("key");
	atom_ptr const message = make_symbol("message");
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
			"Table lookup failed", 
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


bool is_read_error(atom_ptr const& a)
{
	return lookup_eq(a, symbols::error_type, symbols::read_error);
}


bool is_lookup_error(atom_ptr const& a)
{
	return lookup_eq(a, symbols::error_type, symbols::lookup_error);
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
		if (is_read_error(value))
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
			else if (values.size() == 1)
			{
				// don't wrap in a table if it's not a compound statement
				return { si, values.begin()->second };
			}
			else
			{
				values.emplace(symbols::type, symbols::statement);
				return { si, make_table(values) };
			}
		}
			
		if (is_read_error(result))
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
				int n = 0;
				atom_ptr nsym = make_symbol(std::to_string(n));
				atom_ptr statement_atom = lookup(patom, nsym);
				while (true)
				{
					pretty_print(out, statement_atom);

					nsym = make_symbol(std::to_string(++n));
					statement_atom = lookup(patom, nsym);
					if (is_lookup_error(statement_atom))
					{
						break;
					}
					else
					{
						out << ' ';
					}
				}
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


int main(int argc, char* argv[])
{
	while (true)
	{
		atom_ptr const input = prompt_user();

		atom_ptr const read_result = read(input);
		if (is_read_error(read_result))
		{
			std::cout << "error: " << read_result << '\n';
			std::cout << "Read error: " << lookup(read_result, symbols::message)->get_value() << "\n\n";
			continue;
		}
		std::cout << "read: " << read_result << '\n';

		std::cout << "pretty: ";
		pretty_print(std::cout, read_result);
		std::cout << "\n\n";
	}
}