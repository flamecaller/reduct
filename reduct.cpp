#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stack>
#include <string>
#include <variant>


class table;
using table_ptr = std::shared_ptr<table>;


class table
{
public:
	using values_map = std::map<table, table>;
	using values_type = std::variant<std::string, values_map>;

	table() : m_values(values_map()) {}
	table(char const* value) : m_values(value) {}
	table(std::string const& value) : m_values(value) {}
	table(std::initializer_list<values_map::value_type> list) : m_values(list) {}
	
	bool operator== (table const& rhs) const
	{
		return m_values == rhs.m_values;
	}

	bool operator!= (table const& rhs) const
	{
		return !operator==(rhs);
	}

	bool operator< (table const& rhs) const
	{
		return m_values < rhs.m_values;
	}

	table operator[](table const& key) const
	{
		if (auto pstr = std::get_if<std::string>(&m_values))
		{
			return "type-error";
		}

		auto const& values = std::get<values_map>(m_values);
		auto const it = values.find(key);
		if (it == cend(values))
		{
			return "lookup-error";
		}
		return it->second;
	}

	table with(table key, table value) const
	{
		assert(!std::holds_alternative<std::string>(m_values));
		values_type new_values = m_values;
		std::get<values_map>(new_values).insert_or_assign( key, value );
		return new_values;
	}

	bool empty() const
	{
		if (auto pvals = std::get_if<values_map>(&m_values))
		{
			return pvals->empty();
		}
		return false;
	}

	std::optional<std::string> as_string() const
	{
		if (auto pstr = std::get_if<std::string>(&m_values))
		{
			return (*pstr);
		}
		return std::nullopt;
	}

	std::optional<values_map> as_values() const
	{
		if (auto pvals = std::get_if<values_map>(&m_values))
		{
			return (*pvals);
		}
		return std::nullopt;
	}

private:
	table(values_type values) : m_values(values) {}

private:
	values_type m_values;
};


table const lookup_error{ "lookup-error" };
table const read_error{ "read-error" };


bool issymbol(char c)
{
	return isalnum(c);
}


auto make_error(table const& type, table const& message) -> table
{
	return table({
		{"type", "error"},
		{"error-type", type},
		{"message", message}
	});
}


auto make_lookup_expr(table const& map, table const& key) -> table
{
	return table({
		{"type", "lookup-expression"},
		{"map", map},
		{"key", key}
	});
}


auto read(std::string const& input) -> table
{
	table expr;
	std::stack<table> expr_stack;

	auto it = cbegin(input);
	auto const last = cend(input);
	while (it != cend(input))
	{
		char const c = (*it);
		if (isspace(c))
		{
			++it;
		}
		else if (issymbol(c))
		{
			auto const first = it;
			while (it != last && issymbol(*it))
			{
				++it;
			}

			std::string sym{ first, it };
			expr = (expr.empty()) ? sym : make_lookup_expr(expr, sym);
		}
		else if (c == '(')
		{
			++it;
			expr_stack.push(expr);
			expr = table();
		}
		else if (c == ')' && !expr_stack.empty())
		{
			++it;
			table parent = expr_stack.top();
			expr_stack.pop();
			if (expr.empty())
			{
				// () is essentially whitespace, it is not an evaluated lookup. 
				//TODO: Should this be an error?
				expr = parent;
			}
			else if (!parent.empty())
			{
				// Only make a lookup expression if the parent should be considered a 
				// map, otherwise read("(1)") becomes the lookup '({} 1)'.
				expr = make_lookup_expr(parent, expr);
			}
		}
		else if (c == '"')
		{
			++it; // opening quote
			std::ostringstream buf;
			while ((*it) != '"')
			{
				if (*it == '\\')
				{
					++it;	
				}
				buf << (*it);
				++it;
			}
			++it; // closing quote

			auto const str = buf.str();
			expr = (expr.empty()) ? str : make_lookup_expr(expr, str);
		}
		else
		{
			return make_error(read_error, std::string("Unexpected '") + c + "'");
		}
	}

	if (!expr_stack.empty())
	{
		return make_error(read_error, "Missing ')'");
	}
	return expr;
}


std::string pretty(table const& tab)
{
	if (auto pstr = tab.as_string())
	{
		if (std::any_of(pstr->begin(), pstr->end(), isspace))
		{
			return '"' + (*pstr) + '"';
		}
		else
		{
			return (*pstr);
		}
	}

	std::ostringstream out;
	table const type = tab["type"];
	if (type == "lookup-expression")
	{
		out << "(" << pretty(tab["map"]);
		if (tab["key"] != "lookup-error")
		{
			out << " " << pretty(tab["key"]);
		}
		out << ")";
	}
	else if (type == "error")
	{
		out << *(tab["error-type"].as_string()) << ": " << *(tab["message"].as_string());
	}
	else
	{
		out << "{";
		char const* sep = "";
		auto const vals = (*tab.as_values());
		for (auto kv : vals)
		{
			out << sep << pretty(kv.first) << " = " << pretty(kv.second);
			sep = ", ";
		}
		out << "}";
	}
	return out.str();
}


std::ostream& operator<<(std::ostream& out, table const& t)
{
	out << pretty(t);
	return out;
}


int main(int argc, char* argv[])
{
	table const empty;
	assert(empty == empty);
	assert(empty == table());
	assert(empty != "test");
	
	table const test = "test";
	assert(test == table("test"));
	assert(test != table());

	std::cout << "empty: '" << empty << "'\n";
	std::cout << "symbol: '" << test << "'\n";

	while (true)
	{
		std::cout << "> ";
		std::string input;
		std::getline(std::cin, input);
		table const value = read(input);
		std::cout << value << "\n";
	}
}