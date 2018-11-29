#include <cassert>
#include <cctype>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <variant>


class table;
using table_ptr = std::shared_ptr<table>;


class table
{
private:
	using values_map = std::map<table, table>;
	using values_type = std::variant<std::string, values_map>;

public:
	table() : m_values(values_map())
	{
	}

	table(char const* value) : m_values(value)
	{
	}

	table(std::string const& value) : m_values(value)
	{
	}

	table(std::initializer_list<values_map::value_type> list) : m_values(list)
	{
	}
	
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

	std::string to_string() const
	{
		if (auto pstr = std::get_if<std::string>(&m_values))
		{
			return (*pstr);
		}

		std::ostringstream out;
		if (operator[]("type") == "lookup-expression")
		{
			out << "(" << operator[]("map").to_string();
			if (operator[]("key") != "lookup-error")
			{
				out << " " << operator[]("key").to_string();
			}
			out << ")";
		}
		else
		{
			out << "{";
			char const* sep = "";
			for (auto kv : std::get<values_map>(m_values))
			{
				out << sep << kv.first.to_string() << " = " << kv.second.to_string();
				sep = ", ";
			}
			out << "}";
		}
		return out.str();
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

private:
	table(values_type values) : m_values(values) {}

private:
	values_type m_values;
};


table const lookup_error{ "lookup-error" };


std::ostream& operator<<(std::ostream& out, table const& t)
{
	out << t.to_string();
	return out;
}


bool issymbol(char c)
{
	return isalnum(c);
}


auto read(std::string const& input) -> table
{
	table expr;

	auto it = cbegin(input);
	auto const last = cend(input);
	while (it != cend(input))
	{
		char const c = (*it);
		if (isspace(c))
		{
			++it;
			continue;
		}
		else if (issymbol(c))
		{
			auto const first = it;
			while (it != last && issymbol(*it))
			{
				++it;
			}

			std::string sym{ first, it };
			if (expr.empty())
			{
				expr = sym;
			}
			else
			{
				expr = table({
					{"type", "lookup-expression"},
					{"map", expr},
					{"key", sym}
				});
			}
			continue;
		}
		else
		{
			return table({
				{"type", "error"},
				{"error-type", "read-error"},
				{"message", std::string("Unknown character '") + c + "'"}
			});
		}
	}

	return expr;
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