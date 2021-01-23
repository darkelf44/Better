#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <type_traits>

// Remove non standard macros
#undef isascii

// Namespace for std extensions
namespace ext {

// Forward declare
class Ascii;
class Unicode;

template<typename Char, typename Traits = std::char_traits<Char>, typename Allocator = std::allocator<Char>>
class better_string;
template<typename Char, typename Traits = std::char_traits<Char>>
class better_string_view;

// Encoding list
enum class Encoding : int32_t
{
	Unknown = -1,

	// Uninterpreted characters
	Char8   = 0,
	Char16  = 1,
	Char32  = 2,

	// Unicode encodings
	UTF8    = 8,
	UTF16   = 9,
	UTF32   = 10,

	// Windows codepages
	CodepageStart = 0x10000,
	CodepageEnd   = 0x20000,

	// Windows maps a lot of codepages. Here are a few important ones

	Win1250 = CodepageStart + 1250,	// Windows Central European
	Win1251 = CodepageStart + 1251,	// Windows Cyrillic
	Win1252 = CodepageStart + 1252,	// Windows Latin 1
	Win1253 = CodepageStart + 1253,	// Windows Greek
	Win1254 = CodepageStart + 1254,	// Windows Turkish
	Win1255 = CodepageStart + 1255,	// Windows Hebrew
	Win1256 = CodepageStart + 1256,	// Windows Arabic
	Win1257 = CodepageStart + 1257,	// Windows Baltic
	Win1258 = CodepageStart + 1258,	// Windows Vietnamese

	ISO_8859_1  = CodepageStart + 28591,	// ISO Latin 1
	ISO_8859_2  = CodepageStart + 28592,	// ISO Central European
	ISO_8859_3  = CodepageStart + 28593,	// ISO Latin 3
	ISO_8859_4  = CodepageStart + 28594,	// ISO Baltic
	ISO_8859_5  = CodepageStart + 28595,	// ISO Cyrillic
	ISO_8859_6  = CodepageStart + 28596,	// ISO Arabic
	ISO_8859_7  = CodepageStart + 28597,	// ISO Greek
	ISO_8859_8  = CodepageStart + 28598,	// ISO Hebrew
	ISO_8859_9  = CodepageStart + 28599,	// ISO Turkish
	ISO_8859_10 = CodepageStart + 28600,	// ISO Nordic
	ISO_8859_11 = CodepageStart + 28601,	// ISO Thai
	ISO_8859_13 = CodepageStart + 28603,	// ISO Estonian
	ISO_8859_14 = CodepageStart + 28604,	// ISO Celtic
	ISO_8859_15 = CodepageStart + 28605,	// ISO Latin 9

};

// Formatting proxy
template<typename T> struct format_proxy;

// Encoding traits
template<Encoding E> struct encoding_traits;

// Iterable view
template<typename Iter> class iterable_view;

// Default encoding for various character types
template<typename T>
struct default_encoding
{
	static constexpr Encoding value =
		(sizeof(T) == 1) ? Encoding::UTF8 :
		(sizeof(T) == 2) ? Encoding::UTF16 :
		(sizeof(T) == 4) ? Encoding::UTF32 :
		Encoding::Unknown;
};

// Unsafe (but fast) encoding for various character types. Suitable for known single character data.
template<typename T>
struct unsafe_encoding
{
	static constexpr Encoding value =
		(sizeof(T) == 1) ? Encoding::Char8 :
		(sizeof(T) == 2) ? Encoding::Char16 :
		(sizeof(T) == 4) ? Encoding::Char32 :
		Encoding::Unknown;
};

// Create a templated string literal
template<typename Char, Char... values>
inline auto string_literal() -> const Char *
	{static const Char array[] = { values... }; return array;}


// Namespace for implementation details
namespace impl {

// Returns the first type (used to circumvent two phase name lookup)
template<typename T, typename _> struct first { using type = T; };
template<typename T, typename _> using first_t = typename first<T, _>::type;

// Returns the second type (used to circumvent two phase name lookup)
template<typename _, typename T> struct second { using type = T; };
template<typename _, typename T> using second_t = typename second<_, T>::type;

// Enable if, C++14 edition, renamed
template<bool Condition, typename Type = void>
using enable_when = typename std::enable_if<Condition, Type>::type;

// Enable if, for encodings that can be traversed in reverse direction
template<Encoding E>
using enable_when_reversible = typename std::enable_if<encoding_traits<E>::reversible>::type;

// Enable if, for encodings that cannot be traversed in reverse direction
template<Encoding E>
using enable_when_not_reversible = typename std::enable_if<!encoding_traits<E>::reversible>::type;

// Enable if, for iterables that return references
template<typename Iterable>
using enable_when_container =
	typename std::enable_if<std::is_lvalue_reference<decltype(* std::declval<Iterable>().begin())>::value>::type;

// Enable if, for iterables that return values or temporaries
template<typename Iterable>
using enable_when_not_container =
	typename std::enable_if<!std::is_lvalue_reference<decltype(* std::declval<Iterable>().begin())>::value>::type;

// Error handling options
enum class Errors
{
	Strict,
	Ignore,
	Replace,
};

// Parses format specifier string ([[fill]align][sign][#][0][width][,][.precision][type])
template<typename Char, Encoding E>
class Specifier
{
public:
	// Fields
	char type = 0;
	char sign = 0;
	char align = 0;
	bool alter = false;
	bool comma = false;
	size_t width = size_t(-1);
	size_t precision = size_t(-1);
	better_string_view<Char> fill;
	better_string_view<Char> other;

	// Constructor - parses format specifier language
	Specifier(better_string_view<Char> spec)
	{
		// Size check
		if (spec.size() == 0)
			return;

		// Functions
		static constexpr auto is_align = [] (int32_t ch) -> bool
			{return ch == '<' || ch == '>' || ch == '=' || ch == '^';};
		static constexpr auto is_sign = [] (int32_t ch) -> bool
			{return ch == '+' || ch == '-' || ch == ' ';};
		static constexpr auto is_alpha = [] (int32_t ch) -> bool
			{return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');};
		static constexpr auto is_digit = [] (int32_t ch) -> bool
			{return ch >= '0' && ch <= '9';};
		static constexpr auto next = [] (const Char *iter) -> const Char *
			{return static_cast<const Char *>(++ encoding_traits<E>::iter(iter));};

		// Iterators
		const Char * iter = spec.data();
		const Char * end = iter + spec.size();

		// Check for fill and align
		const Char * second = next(iter);
		if (second < end && is_align(* second))
		{
			fill = better_string_view<Char>(iter, second);
			align = * second;
			iter = second;
			++ iter;
		}
		else if (iter < end && is_align(*iter))
		{
			align = *iter;
			++ iter;
		}

		// Check for sign
		if (iter < end && is_sign(*iter))
		{
			sign = *iter;
			++ iter;
		}

		// Check for alternate output
		if (iter < end && *iter == '#')
		{
			alter = true;
			++ iter;
		}

		// Check for zero padding
		if (iter < end && *iter == '0')
		{
			if (!align)
				align = '=';
			if (fill.empty())
				fill = string_literal<Char, '0'>();
			++ iter;
		}

		// Check for width
		if (iter < end && is_digit(*iter))
		{
			width = *iter - '0';
			for (++ iter; iter < end && is_digit(*iter); ++ iter)
				{width = width * 10 + (*iter) - '0';}
		}

		// Check for comma
		if (iter < end && *iter == ',')
		{
			comma = true;
			++ iter;
		}

		// Check for precision
		if (iter < end && *iter == '.')
		{
			precision = 0;
			for (++ iter; iter < end && is_digit(*iter); ++ iter)
				{precision = precision * 10 + (*iter) - '0';}
		}

		// Check for type (This is the only required field)
		if (iter < end && is_alpha(*iter))
			type = *iter ++;
		else
			return;

		// Everything else goes into other
		if (iter < end)
			other = better_string_view<Char>(iter, end);
	}
};

// Translation table returned by `maketrans` functions
class Translation
{
public:
	// Constructor
	template<typename Char>
	Translation(better_string_view<Char> x, better_string_view<Char> y, better_string_view<Char> z)
	{
		// Build translation table [TODO: Sort translation table]
		size_t n = x.size();
		size_t m = z.size();
		data.reserve(n + m);
		for (size_t i = 0; i < n; ++ i)
			data[i] = Node(x[i], y[i]);
		for (size_t i = 0; i < m; ++ i)
			data[n + i] = Node(z[i], -1);
	}

	// Call operator
	auto operator () (int32_t in) const -> int32_t
	{
		// Lookup translation table [TODO: Use logarithmic search]
		for (auto node : data)
			if (node.key == in)
				return node.value;
		return in;
	}

private:
	// Map node
	struct Node
	{
		int32_t key;
		int32_t value;

		Node(int32_t key, int32_t value)
			: key(key), value(value) {}
	};

	// Translation table
	std::vector<Node> data;
};

// Formatter template for selecting functions
template<Encoding E, typename Char, typename T>
void formatter(const void * value, int32_t func, better_string_view<Char> spec, better_string<Char> & out)
{
	// Use a proxy object, to handle non-class types
	using Type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

	// No conversion
	if (func == 0)
		out.extend(format_proxy<Type>::type::template format__<Char, E>(* static_cast<const Type *>(value), spec));

	// Ascii conversion
	else if (func == 'a')
		out.extend(better_string<Char>::template format__<Char, E>(
			format_proxy<Type>::type::template ascii__<Char, E>(* static_cast<const Type *>(value)), spec));

	// Repr conversion
	else if (func == 'r')
		out.extend(better_string<Char>::template format__<Char, E>(
			format_proxy<Type>::type::template repr__<Char, E>(* static_cast<const Type *>(value)), spec));

	// String conversion
	else if (func == 's')
		out.extend(better_string<Char>::template format__<Char, E>(
			format_proxy<Type>::type::template str__<Char, E>(* static_cast<const Type *>(value)), spec));
}

// Close namespace "impl"
}

// Namespace for string algorithms
namespace algorithm { namespace string {

/**
 * @name String algorithms
 *
 * These are the <i>raw</i> algorithm implementations, for string types. You are expected to use them by directly
 * setting all the template parameters, with the correct constness and reference types. For example `Self` is usually
 * set for `decltype(*this)`.
 */

/// @{

// -------------------- Alignment --------------------

// Algorithm - center
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto center(Self self, size_t width, T fillchar) -> R
{
	// Check that fillchar is a character
	if (fillchar.template length<E>() != 1)
		throw std::invalid_argument("center(): fillchar");

	// See if any padding is needed
	if (width < self.template length<E>())
		return self;
	size_t diff = width - self.template length<E>();

	// Create result
	R result(self.size() + fillchar.size() * diff, 0);
	char * data = &result[0];

	// Add padding
	size_t l = diff / 2;
	size_t r = diff - l;
	for (size_t i = 0; i < l; (++ i, data += fillchar.size()))
		Traits::copy(data, fillchar.data(), fillchar.size());
	Traits::copy(data, self.data(), self.size());
	data += self.size();
	for (size_t i = 0; i < r; (++ i, data += fillchar.size()))
		Traits::copy(data, fillchar.data(), fillchar.size());

	// Return result
	return result;
}

// Algorithm - ljust
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto ljust(Self self, size_t width, T fillchar) -> R
{
	// Check that fillchar is a character
	if (fillchar.template length<E>() != 1)
		throw std::invalid_argument("ljust(): fillchar");

	// See if any padding is needed
	if (width < self.template length<E>())
		return self;
	size_t diff = width - self.template length<E>();

	// Create result
	R result(self.size() + fillchar.size() * diff, 0);
	auto * data = &result[0];

	// Add padding
	Traits::copy(data, self.data(), self.size());
	data += self.size();
	for (size_t i = 0; i < diff; (++ i, data += fillchar.size()))
		Traits::copy(data, fillchar.data(), fillchar.size());

	// Return result
	return result;
}

// Algorithm - rjust
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto rjust(Self self, size_t width, T fillchar) -> R
{
	// Check that fillchar is a character
	if (fillchar.template length<E>() != 1)
		throw std::invalid_argument("rjust(): fillchar");

	// See if any padding is needed
	if (width < self.template length<E>())
		return self;
	size_t diff = width - self.template length<E>();

	// Create result
	R result(self.size() + fillchar.size() * diff, 0);
	auto * data = &result[0];

	// Add padding
	for (size_t i = 0; i < diff; (++ i, data += fillchar.size()))
		Traits::copy(data, fillchar.data(), fillchar.size());
	Traits::copy(data, self.data(), self.size());

	// Return result
	return result;
}

// Algorithm - zfill
template<typename Self, typename Traits, Encoding E, typename R>
auto zfill(Self self, size_t width) -> R
{
	// See if any padding is needed
	if (width < self.template length<E>())
		return self;
	size_t diff = width - self.template length<E>();

	// Create result
	R result(self.size() + diff, 0);
	auto * data = &result[0];

	// Check first character
	auto first = self.data()[0];
	if (first == '+' || first == '-')
	{
		data[0] = first;
		++ data;
		for (size_t i = 0; i < diff; ++ i)
			data[i] = '0';
		Traits::copy(data + diff, self.data() + 1, self.size() - 1);
	}
	else
	{
		for (size_t i = 0; i < diff; ++ i)
			data[i] = '0';
		Traits::copy(data + diff, self.data(), self.size());
	}

	// Return result
	return result;
}

// -------------------- Search --------------------

// Algorithm - find
template<typename Self, typename Traits, Encoding E, typename T>
auto find(Self self, T sub, size_t start, size_t end) -> size_t
{
	// Check length
	if (self.size() < sub.size())
		return 0;

	// Create iterators
	end = std::min(end, self.size());
	auto iter = encoding_traits<E>::iter(self.data() + start);
	auto done = encoding_traits<E>::iter(self.data() + end - sub.size() + 1);

	// Find match
	for (; iter != done; ++ iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (Traits::compare(ptr, sub.data(), sub.size()) == 0)
			return ptr - self.data();
	}

	// Not found
	return size_t(-1);
}

// Algorithm - rfind (reversible)
template<typename Self, typename Traits, Encoding E, typename T,
	impl::enable_when_reversible<E> * = nullptr>
auto rfind(Self self, T sub, size_t start, size_t end) -> size_t
{
	// Check length
	if (self.size() < sub.size())
		return 0;

	// Create iterators
	end = std::min(end, self.size());
	auto done = encoding_traits<E>::iter(self.data() + start + sub.size() - 1);
	auto iter = encoding_traits<E>::iter(self.data() + end);

	// Find match
	for (; done != iter; -- iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter) - sub.size();
		if (Traits::compare(ptr, sub.data(), sub.size()) == 0)
			return ptr - self.data();
	}

	// Not found
	return size_t(-1);
}

// Algorithm - index
template<typename Self, typename Traits, Encoding E, typename T>
auto index(Self self, T sub, size_t start, size_t end) -> size_t
{
	// Check length
	if (self.size() < sub.size())
		return 0;

	// Create iterators
	end = std::min(end, self.size());
	auto iter = encoding_traits<E>::iter(self.data() + start);
	auto done = encoding_traits<E>::iter(self.data() + end - sub.size() + 1);

	// Find match
	for (; iter != done; ++ iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (Traits::compare(ptr, sub.data(), sub.size()) == 0)
			return ptr - self.data();
	}

	// Not found
	throw std::invalid_argument("index(): sub");
}

// Algorithm - rindex (reversible)
template<typename Self, typename Traits, Encoding E, typename T,
	impl::enable_when_reversible<E> * = nullptr>
auto rindex(Self self, T sub, size_t start, size_t end) -> size_t
{
	// Check length
	if (self.size() < sub.size())
		return 0;

	// Create iterators
	end = std::min(end, self.size());
	auto done = encoding_traits<E>::iter(self.data() + start + sub.size() - 1);
	auto iter = encoding_traits<E>::iter(self.data() + end);

	// Find match
	for (; done != iter; -- iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter) - sub.size();
		if (Traits::compare(ptr, sub.data(), sub.size()) == 0)
			return ptr - self.data();
	}

	// Not found
	throw std::invalid_argument("rindex(): sub");
}

// Algorithm - count
template<typename Self, typename Traits, Encoding E, typename T>
auto count(Self self, T sub, size_t start, size_t end) -> size_t
{
	// Check length
	if (self.size() < sub.size())
		return 0;

	// Create iterators
	end = std::min(end, self.size());
	auto iter = encoding_traits<E>::iter(self.data() + start);
	auto done = encoding_traits<E>::iter(self.data() + end - sub.size() + 1);

	// Count non-overlapping occurances
	size_t result = 0;
	while (iter != done)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (Traits::compare(ptr, sub.data(), sub.size()) == 0)
		{
			iter = ptr + sub.size();
			++ result;
		}
		else
			++ iter;
	}

	// Return result
	return result;
}

// -------------------- Replace --------------------

// Algorithm - replace
template<typename Self, typename Traits, Encoding E, typename T, typename U, typename R>
auto replace(Self self, T old, U str, size_t count) -> R
{
	// Check length
	if (self.size() < old.size())
		return 0;

	// Allocate result
	R result;

	// Create iterators
	auto prev = self.data();
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size() - old.size() + 1);

	// Build result
	while (count != 0 && iter != done)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (Traits::compare(ptr, old.data(), old.size()) == 0)
		{
			// Resize string
			size_t end = result.size();
			size_t size = ptr - prev;
			result.resize(end + size + str.size());
			auto data = &result[end];

			// Copy before
			Traits::copy(data, prev, size);
			data += size;

			// Copy replacement
			Traits::copy(data, str.data(), str.size());
			data += str.size();

			// Next
			iter = prev = ptr + old.size();
			-- count;
		}
		else
			++ iter;
	}

	// Resize string
	size_t end = result.size();
	size_t size = self.data() + self.size() - prev;
	result.resize(end + size);
	auto data = &result[end];

	// Copy last part
	Traits::copy(data, prev, size);

	// Return result
	return result;
}

// Algorithm - translate
template<typename Self, typename Traits, Encoding E, typename Function, typename R>
auto translate(Self self, Function table, impl::Errors mode) -> R
{
	// Create result
	R result;

	// Iterators
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size());

	// Translate characters
	if (mode == impl::Errors::Strict)
	{
		// Throw errors as exceptions
		for (;iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp < 0)
				throw std::invalid_argument("translate(): input: Decoding error!");
			cp = table(cp);
			if (cp != -1 && !encoding_traits<E>::append(result, cp))
				throw std::invalid_argument("translate(): input: Encoding error!");
		}
	}
	else if (mode == impl::Errors::Replace)
	{
		// Replace errors
		for (;iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp < 0)
				cp = encoding_traits<E>::replacement;
			cp = table(cp);
			if (cp != -1 && !encoding_traits<E>::append(result, cp))
				encoding_traits<E>::append(result, encoding_traits<E>::replacement);
		}
	}
	else if (mode == impl::Errors::Ignore)
	{
		// Ignore errors
		for (;iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp < 0)
				continue;
			cp = table(cp);
			if (cp != -1)
				encoding_traits<E>::append(result, cp);
		}
	}
	else
		throw std::invalid_argument("translate(): mode");

	// Return result
	return result;
};

// Algorithm - expandtabs
template<typename Self, typename Traits, Encoding E, typename R>
auto expandtabs(Self self, size_t tabsize) -> R
{
	// Result
	R result;

	// Iterators
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size());

	// Build result
	size_t count = 0;
	for (; iter != done; ++ iter)
	{
		int32_t cp = *iter;
		if (cp == '\t')
		{
			for (; count < tabsize; ++ count)
				result.push_back(' ');
			count = 0;
		}
		else
		{
			encoding_traits<E>::append(result, cp);
			++ count;
			if (count == tabsize || cp == '\r' || cp == '\n')
				count = 0;
		}
	}

	// Return result
	return result;
}

// -------------------- Split and join --------------------

// Algorithm - join (container)
template<typename Self, typename Traits, typename Iterable, typename R,
	impl::enable_when_container<Iterable> * = nullptr>
auto join(Self self, Iterable iterable) -> R
{
	// Calculate size
	size_t size = 0;
	bool first = true;
	for (const auto & item : iterable)
	{
		if (!first)
			size += self.size();
		first = false;
		size += item.size();
	}

	// Allocate result
	R result(size, 0);
	auto * data = &result[0];

	// Copy items and separators
	first = true;
	for (const auto & item : iterable)
	{
		// Copy separator
		if (!first)
		{
			Traits::copy(data, self.data(), self.size());
			data += self.size();
		}

		// Copy item
		first = false;
		Traits::copy(data, item.data(), item.size());
		data += item.size();
	}

	// Return the result
	return result;
}

// Algorithm - join (iterable)
template<typename Self, typename Traits, typename Iterable, typename R,
	impl::enable_when_not_container<Iterable> * = nullptr>
auto join(Self self, Iterable iterable) -> R
{
	// Allocate result
	R result;

	// Copy items and separators
	bool first = true;
	for (const auto & item : iterable)
	{
		// Resize string
		size_t end = result.size();
		result.resize(end + (first ? item.size() : self.size() + item.size()));
		auto * data = & result[end];

		// Copy separator
		if (!first)
		{
			Traits::copy(data, self.data(), self.size());
			data += self.size();
		}

		// Copy item
		first = false;
		Traits::copy(data, item.data(), item.size());
	}

	// Return the result
	return result;
}

// Algorithm - split (whitespace)
template<typename Self, typename Traits, Encoding E, typename R>
auto split(Self self, size_t maxsplit) -> R
{
	// Create iterators
	auto prev = self.data();
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size());

	// Split string
	R result;
	while (maxsplit != 0 && iter != done)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (* ptr == ' ' || (* ptr >= 0x9 && * ptr <= 0xD))
		{
			result.emplace_back(prev, ptr);
			while (* ptr == ' ' || (* ptr >= 0x9 && * ptr <= 0xD))
				++ ptr;
			iter = prev = ptr;
			-- maxsplit;
		}
		else
			++ iter;
	}
	result.emplace_back(prev, self.data() + self.size());

	// Return result
	return result;
}

// Algorithm - split (separator)
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto split(Self self, T sep, size_t maxsplit) -> R
{
	// Check separator
	if (sep.size() == 0)
		throw std::invalid_argument("split(): sep");

	// Check length
	if (self.size() < sep.size())
		maxsplit = 0;

	// Create iterators
	auto prev = self.data();
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size() - sep.size() + 1);

	// Split string
	R result;
	while (maxsplit != 0 && iter != done)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter);
		if (Traits::compare(ptr, sep.data(), sep.size()) == 0)
		{
			result.emplace_back(prev, ptr);
			iter = prev = ptr + sep.size();
			-- maxsplit;
		}
		else
			++ iter;
	}
	result.emplace_back(prev, self.data() + self.size());

	// Return result
	return result;
}

// Algorithm - rsplit (whitespace)
template<typename Self, typename Traits, Encoding E, typename R>
auto rsplit(Self self, size_t maxsplit) -> R
{
	// Create iterators
	auto prev = self.data() + self.size();
	auto done = encoding_traits<E>::iter(self.data());
	auto iter = encoding_traits<E>::iter(self.data() + self.size());

	// Split string
	R result;
	while (maxsplit != 0 && done != iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter) - 1;
		if (* ptr == ' ' || (* ptr >= 0x9 && * ptr <= 0xD))
		{
			result.emplace_back(ptr + 1, prev);
			while (* ptr == ' ' || (* ptr >= 0x9 && * ptr <= 0xD))
				-- ptr;
			iter = ptr;
			prev = ptr + 1;
			-- maxsplit;
		}
		else
			-- iter;
	}
	result.emplace_back(self.data(), prev);

	// Reverse vector
	using std::swap;
	for (size_t i = 0, n = result.size(); i < (n / 2); ++ i)
		swap(result[i], result[n - i - 1]);

	// Return result
	return result;
}

// Algorithm - rsplit (separator)
template<typename Self, typename Traits, Encoding E, typename T, typename R,
	impl::enable_when_reversible<E> * = nullptr>
auto rsplit(Self self, T sep, size_t maxsplit) -> R
{
	// Check separator
	if (sep.size() == 0)
		throw std::invalid_argument("rsplit(): sep");

	// Check length
	if (self.size() < sep.size())
		maxsplit = 0;

	// Create iterators
	auto prev = self.data() + self.size();
	auto done = encoding_traits<E>::iter(self.data() + sep.size() - 1);
	auto iter = encoding_traits<E>::iter(self.data() + self.size());

	// Split string
	R result;
	while (maxsplit != 0 && done != iter)
	{
		auto ptr = static_cast<const typename Traits::char_type *>(iter) - sep.size();
		if (Traits::compare(ptr, sep.data(), sep.size()) == 0)
		{
			result.emplace_back(ptr + sep.size(), prev);
			iter = prev = ptr;
			-- maxsplit;
		}
		else
			-- iter;
	}
	result.emplace_back(self.data(), prev);

	// Reverse vector
	using std::swap;
	for (size_t i = 0, n = result.size(); i < (n / 2); ++ i)
		swap(result[i], result[n - i - 1]);

	// Return result
	return result;
}

// -------------------- Prefix and suffix --------------------

// Algorithm - startswith
template<typename Self, typename Traits, typename T>
auto startswith(Self self, T prefix, size_t start, size_t end) -> bool
{
	// Update end
	end = std::min(end, self.size());

	// Check length
	if (end - start < prefix.size())
		return false;

	// Compare with substring
	return Traits::compare(self.data() + start, prefix.data(), prefix.size()) == 0;
}

// Algorithm - endswith
template<typename Self, typename Traits, typename T>
auto endswith(Self self, T suffix, size_t start, size_t end) -> bool
{
	// Update end
	end = std::min(end, self.size());

	// Check length
	if (end - start < suffix.size())
		return false;

	// Compare with substring
	return Traits::compare(self.data() + end - suffix.size(), suffix.data(), suffix.size()) == 0;
}

// Algorithm - removeprefix
template<typename Self, typename Traits, typename T, typename R>
auto removeprefix(Self self, T prefix) -> R
{
	if (startswith<Self, Traits, T>(self, prefix, 0, self.size()))
		return R(self.data() + prefix.size(), self.size() - prefix.size());
	else
		return self;
}

// Algorithm - removesuffix
template<typename Self, typename Traits, typename T, typename R>
auto removesuffix(Self self, T suffix) -> R
{
	if (endswith<Self, Traits, T>(self, suffix, 0, self.size()))
		return R(self.data(), self.size() - suffix.size());
	else
		return self;
}

// Algorithm - strip
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto strip(Self self, T chars) -> R;

// Algorithm - lstrip
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto lstrip(Self self, T chars) -> R;

// Algorithm - rstrip
template<typename Self, typename Traits, Encoding E, typename T, typename R>
auto rstrip(Self self, T chars) -> R;

// -------------------- Transcoding --------------------

// Algorithm - transcode
template<typename Input, typename, Encoding From, typename Output, typename, Encoding To,
	impl::enable_when<From == To> * = nullptr>
void transcode(Input input, Output output, impl::Errors)
{
	// Encodings match, copy the characters
	output.resize(input.size());
	for (size_t i = 0; i < input.size(); ++ i)
		output[i] = input[i];
}

template<typename Input, typename, Encoding From, typename Output, typename, Encoding To,
	impl::enable_when<From != To> * = nullptr>
void transcode(Input input, Output output, impl::Errors mode)
{
	// Iterators
	auto iter = encoding_traits<From>::iter(input.data());
	auto done = encoding_traits<From>::iter(input.data() + input.size());

	if (mode == impl::Errors::Strict)
	{
		// Throw errors as exceptions
		for (; iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp < 0)
				throw std::invalid_argument("transcode(): input: Decoding error!");
			else if (!encoding_traits<To>::append(input, cp))
				throw std::invalid_argument("transcode(): input: Encoding error!");
		}
	}
	else if (mode == impl::Errors::Replace)
	{
		// Replace errors
		for (; iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp < 0 || !encoding_traits<To>::append(input, cp))
				encoding_traits<To>::append(input, encoding_traits<To>::replacement);
		}
	}
	else if (mode == impl::Errors::Ignore)
	{
		// Ignore errors
		for (; iter != done; ++ iter)
		{
			int32_t cp = *iter;
			if (cp > 0)
				encoding_traits<To>::append(input, cp);
		}
	}
	else
		throw std::invalid_argument("transcode(): mode");
}

// -------------------- Formatting --------------------

// Algorithm - format (implementation)
template<typename Self, typename Traits, Encoding E, typename Input, typename Formatter, typename R>
auto format1(Self self, Input * inputs, Formatter * fomatters, size_t count, size_t & position) -> R
{
	// Aliases
	using Char = typename Traits::char_type;

	// Functions
	static constexpr auto is_digit = [] (char ch) -> bool
		{return ch >= '0' && ch <= '9';};
	static constexpr auto is_conv = [] (char ch) -> bool
		{return ch == 'a' || ch == 'r' || ch == 's';};
	static constexpr auto next = [] (const Char *iter) -> const Char *
		{return static_cast<const Char *>(++ encoding_traits<E>::iter(iter));};

	// Result string
	R result;

	// Process format string
	auto iter = self.data();
	auto from = iter;
	auto end  = iter + self.size();
	while (iter < end)
	{
		if (*iter == '{')
		{
			// Copy string until this point
			result.extend(from, iter);

			// Handle formatting
			++ iter;
			if (*iter == '{')
			{
				// Skip one '{'
				from = iter;
				++ iter;
			}
			else
			{
				// Process index
				size_t index;
				if (is_digit(*iter))
				{
					// Manual index
					if (position == 0)
						position = size_t(-1);
					else if (position != size_t(-1))
						throw std::invalid_argument("format(): format - Switching from automatic to manual indexing");
					index = *iter - '0';
					for (++ iter; iter < end && is_digit(*iter); ++ iter)
						{index = index * 10 + (*iter) - '0';}
				}
				else
				{
					// Automatic index
					if (position == size_t(-1))
						throw std::invalid_argument("format(): format - Switching from manual to automatic indexing");
					index = position ++;
				}

				// Check index
				if (index >= count)
					throw std::out_of_range("format(): Argument index out of range");

				// Process index operators
				while (*iter == '[')
				{
					throw std::logic_error("Not implemented!");
				}

				// Process conversion
				int32_t conv = 0;
				if (*iter == '!')
				{
					++ iter;
					if (is_conv(*iter))
						conv = *iter;
					else
						throw std::invalid_argument("format(): format - Invalid conversion");
				}

				// Process format specification
				better_string_view<Char> spec;
				if (*iter == ':')
				{
					// Find the end
					size_t level = 1;
					auto start = ++ iter;
					for (; iter < end; iter = next(iter))
					{
						if (*iter == '{')
							++ level;
						else if (*iter == '}' && (-- level == 0))
							break;
					}

					// Check for end
					if (level > 0)
						throw std::invalid_argument("format(): format - Unterminated format sequence");

					// TODO: Recursively apply formatting
					spec = better_string_view<Char>(start, iter);
				}

				// End of sequence
				if (*iter != '}')
					throw std::invalid_argument("format(): format - Unterminated format sequence");

				// Call formatter
				fomatters[index](inputs[index], conv, spec, result);

				// Skip full sequence
				++ iter;
				from = iter;
			}
		}
		else if (*iter == '}')
		{
			// Copy string until this point
			result.extend(from, iter);

			// Expect another '}'
			++ iter;
			if (*iter != '}')
				throw std::invalid_argument("format(): format - Single '}' in format string");

			// Skip one '}'
			from = iter;
			++ iter;
		}
		else
			iter = next(iter);
	}

	// Copy the rest of the string, and return
	return result.extend(from, end);
}

// Algorithm - format
template<typename Self, typename Traits, Encoding E, typename R, typename... Types>
auto format(Self self, Types... values) -> R
{
	// Formatter type
	using Char = typename Traits::char_type;
	using Input = const void *;
	using Formatter = void (*) (const void *, int32_t, better_string_view<Char>, better_string<Char> &);

	// Value and formatter pointers for arguments
	Input inputs[] = { static_cast<const void *>(& values) ... };
	Formatter formatters[] = { & impl::formatter<E, Char, Types> ... };

	// Call implementation
	size_t index = 0;
	return format1<Self, Traits, E, Input, Formatter, R>(self, inputs, formatters, sizeof...(Types), index);
}

// Algorithm - truncate
template<typename Self, typename Traits, Encoding E, typename R>
auto truncate(Self self, size_t width) -> R
{
	// Iterators
	auto size = 0;
	auto iter = encoding_traits<E>::iter(self.data());
	auto done = encoding_traits<E>::iter(self.data() + self.size());

	// Count codepoints
	for (; iter != done; (++ iter, ++ size))
	{
		if (size == width)
			return R(self.data(), static_cast<const typename Traits::char_type *>(iter));
	}
	return self;
}

// Algorithm - quote (repr/ascii - also does transcoding)
template<typename Self, typename Traits, Encoding From, Encoding To, typename R, bool Ascii = false>
auto quote(Self self) -> R
{
	// Create result
	R result;

	// Iterators
	auto iter = encoding_traits<From>::iter(self.data());
	auto done = encoding_traits<From>::iter(self.data() + self.size());

	// Quote and escape string
	result.push_back('"');
	for (; iter != done; ++ iter)
	{
		uint32_t ch = *iter;
		switch (ch)
		{
			case '\'':
			case '\"':
			case '\\':
				result.push_back('\\');
				result.push_back(char(ch));
				continue;
			case '\0':
				result.push_back('\\');
				result.push_back('0');
			case '\a':
				result.push_back('\\');
				result.push_back('a');
				continue;
			case '\b':
				result.push_back('\\');
				result.push_back('b');
				continue;
			case '\f':
				result.push_back('\\');
				result.push_back('f');
				continue;
			case '\n':
				result.push_back('\\');
				result.push_back('n');
				continue;
			case '\r':
				result.push_back('\\');
				result.push_back('r');
				continue;
			case '\t':
				result.push_back('\\');
				result.push_back('t');
				continue;
			case '\v':
				result.push_back('\\');
				result.push_back('v');
				continue;
		}
		if (ch < 0x20 || (Ascii && ch >= 0x80))
		{
			const char * digits = "0123456789abcdef";
			if (ch < 0x10000)
			{
				size_t n = result.size();
				result.resize(n + 6);
				result[n] = '\\';
				result[n + 1] = 'u';
				result[n + 2] = digits[(ch >> 12) & 0xF];
				result[n + 3] = digits[(ch >> 8) & 0xF];
				result[n + 4] = digits[(ch >> 4) & 0xF];
				result[n + 5] = digits[ch & 0xF];
			}
			else if (ch < 0x110000)
			{
				size_t n = result.size();
				result.resize(n + 10);
				result[n] = '\\';
				result[n + 1] = 'U';
				result[n + 2] = digits[(ch >> 28) & 0xF];
				result[n + 3] = digits[(ch >> 24) & 0xF];
				result[n + 4] = digits[(ch >> 20) & 0xF];
				result[n + 5] = digits[(ch >> 16) & 0xF];
				result[n + 6] = digits[(ch >> 12) & 0xF];
				result[n + 7] = digits[(ch >> 8) & 0xF];
				result[n + 8] = digits[(ch >> 4) & 0xF];
				result[n + 9] = digits[ch & 0xF];
			}
			else if (Ascii)
				result.push_back('?');
			else
				encoding_traits<To>::append(result, encoding_traits<To>::replacement);
		}
		else if (Ascii)
			result.push_back(ch);
		else if (!encoding_traits<To>::append(result, ch))
		{
			encoding_traits<To>::append(result, encoding_traits<To>::replacement);
		}
	}
	result.push_back('"');

	// Return result
	return result;
}

/// @}

// Close namespace "ext::algorithm::string"
}}

// Using basic_string from std
template<typename C, typename T, typename A>
using basic_string = std::basic_string<C, T, A>;

// Using basic_string_view from std, when available
#if __cplusplus >= 201703
template<typename C, typename T>
using basic_string_view = std::basic_string_view<C, T>
#endif

#if __cplusplus < 201703

/************************************************************
 * @brief String view - for C++14 and below
 *
 * String views are part of the standard library, starting from C++17. This
 * class is used for C++11 and C++14, as the base class of better_string_view.
 */
template<typename Char, typename Traits = std::char_traits<Char>>
class basic_string_view
{
public:
	// Aliases
	using traits_type = Traits;
	using value_type = Char;
	using pointer = Char *;
	using const_pointer = const Char *;
	using reference = Char &;
	using const_reference = const Char &;
	using iterator = const Char *;
	using const_iterator = const Char *;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;
	using size_type = size_t;
	using difference_type = ptrdiff_t;

	// Constants
	static constexpr size_type npos = size_type(-1);

	// Constructors
	constexpr basic_string_view() noexcept = default;
	constexpr basic_string_view(const basic_string_view &) noexcept = default;
	constexpr basic_string_view(const Char * str) noexcept
		: _str(str), _len(Traits::length(str)) {}
	constexpr basic_string_view(const Char * str, size_type len) noexcept
		: _str(str), _len(len) {}

	// Size
	constexpr auto size() const noexcept -> size_type
		{return _len;}
	constexpr auto length() const noexcept -> size_type
		{return _len;}
	constexpr auto max_size() const noexcept -> size_type
		{return (size_type) -1;}
	constexpr bool empty() const noexcept
		{return !_len;}

	// Data
	constexpr auto data() const noexcept -> const Char *
		{return _str;}

	// Copy
	auto operator = (const basic_string_view &) noexcept -> basic_string_view & = default;

	// Getters
	constexpr auto operator [] (size_type pos) const noexcept -> const Char &
		{return _str[pos];}
	constexpr auto at(size_type pos) const -> const Char &
	{
		if (pos < _len)
			return _str[pos];
		else
			throw std::out_of_range("basic_string_view::at(): pos");
	}
	constexpr auto front() const -> const Char &
		{return _str[0];}
	constexpr auto back() const -> const Char &
		{return _str[_len - 1];}

	// Operations
	void remove_prefix(size_type n)
		{_str += n; _len -= n;}
	void remove_suffix(size_type n)
		{_len -= n;}

	// Iteration
	constexpr auto begin() const noexcept -> iterator
		{return _str;}
	constexpr auto cbegin() const noexcept -> const_iterator
		{return _str;}
	constexpr auto end() const noexcept -> iterator
		{return _str + _len;}
	constexpr auto cend() const noexcept -> const_iterator
		{return _str + _len;}
	constexpr auto rbegin() const noexcept -> reverse_iterator
		{ return reverse_iterator(_str + _len); }
	constexpr auto crbegin() const noexcept -> const_reverse_iterator
		{ return const_reverse_iterator(_str + _len); }
	constexpr auto rend() const noexcept -> reverse_iterator
		{ return reverse_iterator(_str); }
	constexpr auto crend() const noexcept -> const_reverse_iterator
		{ return const_reverse_iterator(_str); }

	// Comparison
	constexpr int compare(basic_string_view str) const noexcept
	{
		auto r = Traits::compare(_str, str._str, std::min(_len, str._len));
		return r != 0 ? r : (str._len < _len) - (_len < str._len);
	}
	constexpr int compare(const Char * str) const
		{return compare(basic_string_view(str));}

	// Substring
	constexpr auto substr(size_type pos = 0, size_type len = npos) const -> basic_string_view
		{return basic_string_view(_str + pos, _str + pos + std::min(_len - pos, len));}

private:
	const Char * _str = nullptr;
	size_type    _len = 0;
};

// String view operators
template<typename Char, typename Traits>
bool operator == (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) == 0;}
template<typename Char, typename Traits>
bool operator != (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) != 0;}

template<typename Char, typename Traits>
bool operator < (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) < 0;}
template<typename Char, typename Traits>
bool operator > (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) > 0;}
template<typename Char, typename Traits>
bool operator <= (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) <= 0;}
template<typename Char, typename Traits>
bool operator >= (basic_string_view<Char, Traits> left, basic_string_view<Char, Traits> right)
	{return left.compare(right) >= 0;}

#endif

//	------------------------------------------------------------
//		Better string
//	------------------------------------------------------------

/************************************************************
 * @brief Better string - a better std::string
 *
 * This class augments the standard string class, to make it more usable, by
 * adding much needed utility methods, and replacing existing ones.
 *
 * Where applicable, the methods now take character encoding into account. By
 * default, each string type uses a Unicode character encoding (UTF-8, UTF-16,
 * UTF-32), but you can specify the encoding used for the methods directly.
 */
template<typename Char, typename Traits, typename Allocator>
class better_string : public basic_string<Char, Traits, Allocator>
{
	// Private aliases
	using base__ = basic_string<Char, Traits, Allocator>;

	// Default encoding
	static constexpr Encoding default_encoding__ = default_encoding<Char>::value;

public:
	// Aliases
	using errors = impl::Errors;

	// Constructors
	using base__::basic_string;
	constexpr better_string(const Char * ptr, const Char * end)
		: base__(ptr, end - ptr) {}

	// Core functions

	/**
	 * @brief The number of <i>Unicode codepoints</i> in the string.
	 *
	 * In all of the better string types this function is redefined to return
	 * the actual length of the text, rather than the number of characters used
	 * to store that text. If you want the later, @ref size is still available
	 * for that.
	 */
	template<Encoding E = default_encoding__>
	auto length() const -> size_t
		{return encoding_traits<E>::iter(base__::data() + base__::size()) - encoding_traits<E>::iter(base__::data());}

	/**
	 * @brief A view of the <i>Unicode codepoints</i> in the string.
	 *
	 * Iterating over this view is the same as converting the string to UTF-32,
	 * and iterating over that, but it converts the characters on the fly.
	 */
	template<Encoding E = default_encoding__>
	auto codepoints() const -> iterable_view<typename encoding_traits<E>::iterator>
		{return {encoding_traits<E>::iter(base__::data()), encoding_traits<E>::iter(base__::data() + base__::size())};}

	// Assignment
	using base__::operator =;

	/// Assignment operator for string view
	auto operator = (better_string_view<Char> view) -> better_string
		{return static_cast<better_string &>(base__::assign(view.data(), view.size()));}

	/// Move assignment operator for string
	auto operator = (basic_string<Char, Traits, Allocator> && str) -> better_string
		{return static_cast<better_string &>(base__::assign(std::move(str)));}

	/// Copy assignment operator for strings
	auto operator = (const basic_string<Char, Traits, Allocator> & str) -> better_string
		{return static_cast<better_string &>(base__::assign(str));}

	/// Copy assignment operator for strings
	template<typename T, typename A>
	auto operator = (const basic_string<Char, T, A> & str) -> better_string
		{return static_cast<better_string &>(base__::assign(str.data(), str.size()));}

	// Appending the string

	/**
	 * @brief Encode a Unicode codepoint, and append it to the string.
	 *
	 * @tparam E The encoding of the string.
	 * @param codepoint The codepoint to append.
	 */
	template<Encoding E = default_encoding__>
	auto append(uint32_t codepoint) -> better_string &
		{encoding_traits<E>::append(*this, codepoint); return * this;}

	// Extending the string

	/**
	 * @brief Extend the string with an other string.
	 *
	 * @note Previously called "append" in the @ref std::basic_string.
	 * @param str The string to append.
	 */
	auto extend(better_string_view<Char> str) -> better_string &
		{return static_cast<better_string &>(base__::append(str.data(), str.size()));}

	/**
	 * @brief Extend the string with an other string.
	 *
	 * @note Previously called "append" in the @ref std::basic_string
	 * @param str The string to append.
	 */
	auto extend(const basic_string<Char, Traits, Allocator> & str) -> better_string &
		{return static_cast<better_string &>(base__::append(str));}

	/**
	 * @brief Extend the string with a raw C string.
	 *
	 * @note Previously called "append" in the @ref std::basic_string
	 *
	 * @param data The start of the string.
	 * @param size The size of the string.
	 */
	auto extend(const char * data, size_t size) -> better_string &
		{return static_cast<better_string &>(base__::append(data, size));}

	/**
	 * @brief Extend the string with raw C string.
	 *
	 * @note Previously called "append" in the @ref std::basic_string
	 *
	 * @param start The start of the string.
	 * @param end The end of the string (exclusive).
	 */
	auto extend(const char * start, const char * end) -> better_string &
		{return static_cast<better_string &>(base__::append(start, end - start));}

	/// Operator to extend the string with a string view.
	auto operator += (better_string_view<Char> view) -> better_string
		{return static_cast<better_string &>(base__::append(view.data(), view.size()));}

	/// Operator to extend the string with an other string.
	auto operator += (const basic_string<Char, Traits, Allocator> & str) -> better_string
		{return static_cast<better_string &>(base__::append(str));}

	// Alignment functions

	/**
	 * @brief Pad the the string to the specified width, while also centering it.
	 *
	 * @tparam E The encoding of the string.
	 * @param width The desired length of the new string.
	 * @param fillchar The fill character used for padding the string. Defaults to the space character(0x20).
	 */
	template<Encoding E = default_encoding__>
	auto center(size_t width, better_string_view<Char> fillchar = string_literal<Char, ' '>()) const -> better_string
		{return algorithm::string::center<decltype(*this), Traits, E, better_string_view<Char>, better_string>(*this, width, fillchar);}

	/**
	 * @brief Pad the the string to the specified width, while aligning it to the left.
	 *
	 * @tparam E The encoding of the string.
	 * @param width The desired length of the new string.
	 * @param fillchar The fill character used for padding the string. Defaults to the space character(0x20).
	 */
	template<Encoding E = default_encoding__>
	auto ljust(size_t width, better_string_view<Char> fillchar = string_literal<Char, ' '>()) const -> better_string
		{return algorithm::string::ljust<decltype(*this), Traits, E, better_string_view<Char>, better_string>(*this, width, fillchar);}

	/**
	 * @brief Pad the the string to the specified width, while aligning it to the right.
	 *
	 * @tparam E The encoding of the string.
	 * @param width The desired length of the new string.
	 * @param fillchar The fill character used for padding the string. Defaults to the space character(0x20).
	 */
	template<Encoding E = default_encoding__>
	auto rjust(size_t width, better_string_view<Char> fillchar = string_literal<Char, ' '>()) const -> better_string
		{return algorithm::string::rjust<decltype(*this), Traits, E, better_string_view<Char>, better_string>(*this, width, fillchar);}

	/**
	 * @brief Pad the the string to the specified width with zeros, while aligning it to the right. If the first
	 * character is a sign (+/-), it is left alone.
	 *
	 * @param width The desired length of the new string.
	 */
	template<Encoding E = default_encoding__>
	auto zfill(size_t width) const -> better_string
		{return algorithm::string::zfill<decltype(*this), Traits, E, better_string>(*this, width);}

	// Search functions

	/**
	 * @brief Find the first occurrence of a substring and return its position.
	 * If the string is not found @ref npos is returned.
	 *
	 * @tparam E The encoding of the string.
	 * @param str The substring to find.
	 */
	template<Encoding E = default_encoding__>
	auto find(better_string_view<Char> str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::find<decltype(*this), Traits, E, better_string_view<Char>>(*this, str, start, end);}

	/**
	 * @brief Find the last occurrence of a substring and return its position.
	 * If the string is not found @ref npos is returned.
	 *
	 * @tparam E The encoding of the string.
	 * @param str The substring to find.
	 */
	template<Encoding E = default_encoding__>
	auto rfind(better_string_view<Char> str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::rfind<decltype(*this), Traits, E, better_string_view<Char>>(*this, str, start, end);}

	/**
	 * @brief Find the first occurrence of a substring and return its position.
	 * If the string is not found std::invalid_argument is thrown.
	 *
	 * @tparam E The encoding of the string.
	 * @param str The substring to find.
	 */
	template<Encoding E = default_encoding__>
	auto index(better_string_view<Char> str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::index<decltype(*this), Traits, E, better_string_view<Char>>(*this, str, start, end);}

	/**
	 * @brief Find the last occurrence of a substring and returns its position.
	 * If the string is not found std::invalid_argument is thrown.
	 *
	 * @tparam E The encoding of the string.
	 * @param str The substring to find.
	 */
	template<Encoding E = default_encoding__>
	auto rindex(better_string_view<Char> str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::rindex<decltype(*this), Traits, E, better_string_view<Char>>(*this, str, start, end);}

	/**
	 * @brief Count the number of non-overlapping occurrences of a substring.
	 *
	 * @tparam E The encoding of the string.
	 * @param str The substring to find.
	 */
	template<Encoding E = default_encoding__>
	auto count(better_string_view<Char> str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::count<decltype(*this), Traits, E, better_string_view<Char>>(*this, str, start, end);}

	// Replace functions

	/**
	 * @brief Replace the first @p count number of non-overlapping occurrences of @p old with @p str.
	 *
	 * @tparam E The encoding of the string.
	 * @param old The substring to replace.
	 * @param str The substring to replace it with.
	 * @param count The maximum number or substrings to replace.
	 */
	template<Encoding E = default_encoding__>
	auto replace(better_string_view<Char> old, better_string_view<Char> str, size_t count = base__::npos) const -> better_string
		{return algorithm::string::replace<decltype(*this), Traits, E, better_string_view<Char>, better_string_view<Char>, better_string>(*this, old, str, count);}

	/**
	 * @brief Replace every character in a string, using a translation table. The translation table is usually
	 * obtained from from the @ref maketrans function.
	 *
	 * The translation table is a function (or function like object), that takes a unicode codepoint, and returns its
	 * replacement, or -1 to remove the character without replacing it.
	 *
	 * @note In Python the translate method uses a dictionary instead of a function.
	 *
	 * @tparam E The encoding of the string.
	 * @param table The translation table.
	 */
	template<Encoding E = default_encoding__, typename Function>
	auto translate(Function && table, errors mode = errors::Replace) const -> better_string
		{return algorithm::string::translate<decltype(*this), Traits, E, Function &&, better_string>(*this, static_cast<Function &&>(table), mode);}

	/**
	 * @brief Create a translation table to be used with the @ref translate function. It takes two strings of equal
	 * length. The characters in the first strings will be translated to the characters that have the same position in
	 * the second string. The third string is optional, the characters in that will be removed from the string.
	 *
	 * @tparam E The encoding of the string.
	 * @param from The characters to replace.
	 * @param to The replacement characters.
	 * @param skip The characters to remove.
	 */
	template<Encoding E = default_encoding__>
	static auto maketrans(better_string_view<Char> from, better_string_view<Char> to, better_string_view<Char> skip = "") -> impl::Translation
		{return impl::Translation(from, to, skip);}

	/**
	 * @brief Convert tabs to spaces correctly. Each tab will be replaced with enough spaces to align its end with the
	 * next tabulator position. The spacing of the tabulator positions are given by @p tabsize.
	 *
	 * @note In Python the default tabulator size is 8. Here, it's 4.
	 *
	 * @tparam E The encoding of the string.
	 * @param from The size of the tabulator positions. Defaults to 4.
	 */
	template<Encoding E = default_encoding__>
	auto expandtabs(size_t tabsize = 4) const -> better_string
		{return algorithm::string::expandtabs<decltype(*this), Traits, E, better_string>(*this, tabsize);}

	// Split and join functions

	/**
	 * @brief Concatenate the strings in the list, using this string as a
	 * separator between the items.
	 *
	 * @param iterable The list of items to join together.
	 */
	template<typename Iterable>
	auto join(const Iterable & iterable) const -> better_string
		{return algorithm::string::join<decltype(*this), Traits, const Iterable &, better_string>(*this, iterable);}

	/**
	 * @brief Split the string along sequences of whitespace characters.
	 *
	 * @note Only the ASCII whitespace characters (space, \\t, \\v, \\n, \\r,
	 * \\f) are recognized. Unicode whitespace characters are not.
	 *
	 * @tparam E The encoding of the string.
	 * @param maxsplit The maximum number of splits to do. The default (-1) means no limit.
	 */
	template<Encoding E = default_encoding__>
	auto split(size_t maxsplit = -1) const -> std::vector<better_string>
		{return algorithm::string::split<decltype(*this), Traits, E, std::vector<better_string>>(*this, maxsplit);}

	/**
	 * @brief Split the string along a predefined separator string.
	 *
	 * @tparam E The encoding of the string.
	 * @param sep The separator string to use.
	 * @param maxsplit The maximum number of splits to do. The default (-1) means no limit.
	 */
	template<Encoding E = default_encoding__>
	auto split(better_string_view<Char> sep, size_t maxsplit = -1) const -> std::vector<better_string>
		{return algorithm::string::split<decltype(*this), Traits, E, better_string_view<Char>, std::vector<better_string>>(*this, sep, maxsplit);}

	/**
	 * @brief Split the string along sequences of whitespace characters.
	 *
	 * Same as @ref split, but searches in reverse. @p maxsplit is also counted from the back of the string.
	 *
	 * @note Requires a reversible encoding.
	 *
	 * @tparam E The encoding of the string.
	 * @param maxsplit The maximum number of splits to do. The default (-1) means no limit.
	 */
	template<Encoding E = default_encoding__>
	auto rsplit(size_t maxsplit = -1) const -> std::vector<better_string>
		{return algorithm::string::rsplit<decltype(*this), Traits, E, std::vector<better_string>>(*this, maxsplit);}

	/**
	 * @brief Split the string along the non-overlapping occurrences of a separator string.
	 *
	 * Same as @ref split, but searches in reverse. @p maxsplit is also counted from the back of the string.
	 *
	 * @note Requires a reversible encoding.
	 *
	 * @tparam E The encoding of the string.
	 * @param sep The separator string to use.
	 * @param maxsplit The maximum number of splits to do. The default (-1) means no limit.
	 */
	template<Encoding E = default_encoding__>
	auto rsplit(better_string_view<Char> sep, size_t maxsplit = -1) const -> std::vector<better_string>
		{return algorithm::string::rsplit<decltype(*this), Traits, E, better_string_view<Char>, std::vector<better_string>>(*this, sep, maxsplit);}

	/**
	 * @brief Split the string into a list of lines.
	 *
	 * @tparam E The encoding of the string.
	 * @param keepends When true, the line separator characters at the end of the line are part of the result
	 */
	template<Encoding E = default_encoding__>
	auto splitlines(bool keepends = false) const -> std::vector<better_string>;

	/**
	 * @brief Split the string into three parts: the part before the separator, the separator string, and the part
	 * after. If the separator string is not found in the string, the first part is the entire string, and the other
	 * two are empty.
	 *
	 * @tparam E The encoding of the string.
	 * @param sep The separator string to use.
	 */
	template<Encoding E = default_encoding__>
	auto partition(better_string_view<Char> sep) const -> std::vector<better_string>;

	/**
	 * @brief Same as @ref partition, but searches for the separator in reverse order.
	 *
	 * @tparam E The encoding of the string.
	 * @param sep The separator string to use.
	 */
	template<Encoding E = default_encoding__>
	auto rpartition(better_string_view<Char> sep) const -> std::vector<better_string>;

	// Prefix and suffix functions

	/**
	 * @brief Chesk if the string starts with @p prefix.
	 *
	 * @param prefix The string to compare with.
	 */
	auto startswith(better_string_view<Char> prefix, size_t start = 0, size_t end = base__::npos) const -> bool
		{return algorithm::string::startswith<decltype(*this), Traits, better_string_view<Char>>(*this, prefix, start, end);}

	/**
	 * @brief Chesk if the string ends with @p suffix.
	 *
	 * @note Even though @ref endswith can be used without an encoding, using it
	 * with a non-reversible one can result in partial matches.
	 *
	 * @param suffix The string to compare with.
	 */
	auto endswith(better_string_view<Char> suffix, size_t start = 0, size_t end = base__::npos) const -> bool
		{return algorithm::string::endswith<decltype(*this), Traits, better_string_view<Char>>(*this, suffix, start, end);}

	/**
	 * @brief If the string starts with @p prefix, remove it. Otherwise return the original string.
	 *
	 * @param prefix The prefix to remove.
	 */
	auto removeprefix(better_string_view<Char> prefix) const -> better_string
		{return algorithm::string::removeprefix<decltype(*this), Traits, better_string_view<Char>, better_string>(*this, prefix);}

	/**
	 * @brief If the string ends with @p suffix, remove it. Otherwise return the original string.
	 *
	 * @param suffix The suffix to remove.
	 */
	auto removesuffix(better_string_view<Char> suffix) const -> better_string
		{return algorithm::string::removesuffix<decltype(*this), Traits, better_string_view<Char>, better_string>(*this, suffix);}

	auto strip() const -> better_string;

	template<Encoding E = default_encoding__>
	auto strip(better_string_view<Char> chars) const -> better_string;

	auto lstrip() const -> better_string;

	template<Encoding E = default_encoding__>
	auto lstrip(better_string_view<Char> chars) const -> better_string;

	auto rstrip() const -> better_string;

	template<Encoding E = default_encoding__>
	auto rstrip(better_string_view<Char> chars) const -> better_string;

	// Character functions

	template<Encoding E = default_encoding__>
	auto isascii() const -> bool;

	template<Encoding E = default_encoding__>
	auto isspace() const -> bool;

	template<Encoding E = default_encoding__>
	auto isaplha() const -> bool;

	template<Encoding E = default_encoding__>
	auto isalnum() const -> bool;

	template<Encoding E = default_encoding__>
	auto isdigit() const -> bool;

	template<Encoding E = default_encoding__>
	auto isdecimal() const -> bool;

	template<Encoding E = default_encoding__>
	auto isnumeric() const -> bool;

	template<Encoding E = default_encoding__>
	auto isprintable() const -> bool;

	template<Encoding E = default_encoding__>
	auto isidentifier() const -> bool;

	// Character case functions

	template<Encoding E = default_encoding__>
	auto upper() const -> better_string;

	template<Encoding E = default_encoding__>
	auto lower() const -> better_string;

	template<Encoding E = default_encoding__>
	auto title() const -> better_string;

	template<Encoding E = default_encoding__>
	auto isupper() const -> bool;

	template<Encoding E = default_encoding__>
	auto islower() const -> bool;

	template<Encoding E = default_encoding__>
	auto istitle() const -> bool;

	template<Encoding E = default_encoding__>
	auto capitalize() const -> better_string;

	template<Encoding E = default_encoding__>
	auto casefold() const -> better_string;

	template<Encoding E = default_encoding__>
	auto swapcase() const -> better_string;

	// Transcoding functions

	/**
	 * @brief This is a version of @ref transcode(), that transcodes to one of the default encodings (usually UTF-8).
	 * Transcoding to a default encoding is required often enough to make this shorthand usefult
	 *
	 * @tparam To The desired encoding of the result.
	 * @result Returns the string, encoded with the desired encoding.
	 */
	template<Encoding From, typename CharTo = char>
	auto decode(errors mode = errors::Strict) const -> better_string<CharTo, std::char_traits<CharTo>, Allocator>
	{
		better_string<CharTo, std::char_traits<CharTo>, Allocator> result;
		algorithm::string::transcode<decltype(*this), Traits, From, decltype(result) &, std::char_traits<CharTo>, default_encoding<CharTo>::value>(*this, result, mode);
		return result;
	}

	/**
	 * @brief Transcodes the string into a different encoding.
	 *
	 * @tparam From The encoding of the source string.
	 * @tparam To The desired encoding of the result.
	 * @result Returns the string, encoded with the desired encoding.
	 */
	template<Encoding From, Encoding To, typename CharTo = typename encoding_traits<To>::char_type>
	auto transcode(errors mode = errors::Strict) const -> better_string<CharTo, std::char_traits<CharTo>, Allocator>
	{
		better_string<CharTo, std::char_traits<CharTo>, Allocator> result;
		algorithm::string::transcode<decltype(*this), Traits, From, decltype(result) &, std::char_traits<CharTo>, To>(*this, result, mode);
		return result;
	}

	// Formatting functions

	/**
	 * @brief Formats the list of values, using the string as the format template.
	 *
	 * @param values The list of values used during by the format sequences.
	 */
	template<Encoding E = default_encoding__, typename... Types>
	auto format(Types && ... values) const -> better_string
		{return algorithm::string::format<decltype(*this), Traits, E, better_string, const Types & ...>(*this, static_cast<const Types &>(values) ...);}

	// Magic functions

	/// Used by @ref str() to convert this type to a string.
	template<typename CharTo, Encoding To>
	static auto str__(const better_string & str) -> better_string<CharTo>
		{return transcode<default_encoding__, To, CharTo>(errors::Replace);}

	/// Used by @ref repr() to create a string representation of this type.
	template<typename CharTo, Encoding To>
	static auto repr__(const better_string & str) -> better_string<CharTo>
		{return algorithm::string::quote<const better_string &, Traits, default_encoding__, To, better_string<CharTo>, false>();}

	/// Used by @ref ascii() to create a ASCII only representation of this type.
	template<typename CharTo, Encoding To>
	static auto ascii__(const better_string & str) -> better_string<CharTo>
		{return algorithm::string::quote<const better_string &, Traits, default_encoding__, To, better_string<CharTo>, true>();}

	/// Used by @ref format() to format the appearance of this type.
	template<typename CharTo, Encoding To>
	static auto format__(const better_string & str, impl::Specifier<CharTo, To> && spec) -> better_string<CharTo>
		{return better_string_view<Char>::template format__<CharTo, To>(str, std::move(spec));}
};

//	------------------------------------------------------------
//		Better string view
//	------------------------------------------------------------

template<typename Char, typename Traits>
class better_string_view : public basic_string_view<Char, Traits>
{
	// Private alias
	using base__ = basic_string_view<Char, Traits>;

	// Default encoding
	static constexpr Encoding default_encoding__ = default_encoding<Char>::value;

public:
	// Aliases
	using errors = impl::Errors;

	// Constructors
	using base__::basic_string_view;
	constexpr better_string_view(const Char * ptr, const Char * end)
		: base__(ptr, end - ptr) {}
	template<typename T, typename A>
	constexpr better_string_view(const basic_string<Char, T, A> & str) noexcept
		: base__(str.data(), str.size()) {}

	/// @see better_string::length()
	template<Encoding E = default_encoding__>
	auto length() const -> size_t
		{return encoding_traits<E>::iter(base__::data() + base__::size()) - encoding_traits<E>::iter(base__::data());}

	// Alignment functions

	/// @see better_string::center()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto center(size_t width, better_string_view fillchar = " ") const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::center<decltype(*this), Traits, E, better_string_view, better_string<Char, Traits, Allocator>>(*this, width, fillchar);}

	/// @see better_string::ljust()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto ljust(size_t width, better_string_view fillchar = " ") const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::ljust<decltype(*this), Traits, E, better_string_view, better_string<Char, Traits, Allocator>>(*this, width, fillchar);}

	/// @see better_string::rjust()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto rjust(size_t width, better_string_view fillchar = " ") const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::rjust<decltype(*this), Traits, E, better_string_view, better_string<Char, Traits, Allocator>>(*this, width, fillchar);}

	/// @see better_string::zfill()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto zfill(size_t width) const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::zfill<decltype(*this), Traits, E, better_string<Char, Traits, Allocator>>(*this, width);}

	// Find functions

	/// @see better_string::find()
	template<Encoding E = default_encoding__>
	auto find(better_string_view str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::find<decltype(*this), Traits, E, better_string_view>(*this, str, start, end);}

	/// @see better_string::rfind()
	template<Encoding E = default_encoding__>
	auto rfind(better_string_view str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::rfind<decltype(*this), Traits, E, better_string_view>(*this, str, start, end);}

	/// @see better_string::index()
	template<Encoding E = default_encoding__>
	auto index(better_string_view str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::index<decltype(*this), Traits, E, better_string_view>(*this, str, start, end);}

	/// @see better_string::rindex()
	template<Encoding E = default_encoding__>
	auto rindex(better_string_view str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::rindex<decltype(*this), Traits, E, better_string_view>(*this, str, start, end);}

	/// @see better_string::count()
	template<Encoding E = default_encoding__>
	auto count(better_string_view str, size_t start = 0, size_t end = base__::npos) const -> size_t
		{return algorithm::string::count<decltype(*this), Traits, E, better_string_view>(*this, str, start, end);}

	// Replace functions

	/// @see better_string::replace()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto replace(better_string_view old, better_string_view str, size_t count = base__::npos) const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::replace<decltype(*this), Traits, E, better_string_view, better_string_view, better_string<Char, Traits, Allocator>>(*this, old, str, count);}

	/// @see better_string::translate()
	template<Encoding E = default_encoding__, typename Function, typename Allocator = std::allocator<Char>>
	auto translate(Function && table, errors mode = errors::Replace) const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::translate<decltype(*this), Traits, E, Function &&, better_string<Char, Traits, Allocator>>(*this, static_cast<Function &&>(table), mode);}

	/// @see better_string::maketrans()
	template<Encoding E = default_encoding__>
	static auto maketrans(better_string_view from, better_string_view to, better_string_view skip = "") -> impl::Translation
		{return impl::Translation(from, to, skip);}

	/// @see better_string::expandtabs()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto expandtabs(size_t tabsize = 4) const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::expandtabs<decltype(*this), Traits, E, better_string<Char, Traits, Allocator>>(*this, tabsize);}

	// Split and join functions

	// Prefix and suffix functions

	// Character functions

	template<Encoding E = default_encoding__>
	auto isascii() const -> bool;

	template<Encoding E = default_encoding__>
	auto isspace() const -> bool;

	template<Encoding E = default_encoding__>
	auto isaplha() const -> bool;

	template<Encoding E = default_encoding__>
	auto isalnum() const -> bool;

	template<Encoding E = default_encoding__>
	auto isdigit() const -> bool;

	template<Encoding E = default_encoding__>
	auto isdecimal() const -> bool;

	template<Encoding E = default_encoding__>
	auto isnumeric() const -> bool;

	template<Encoding E = default_encoding__>
	auto isprintable() const -> bool;

	template<Encoding E = default_encoding__>
	auto isidentifier() const -> bool;

	// Character case functions

	/// @see better_string::upper()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto upper() const -> better_string<Char, Traits, Allocator>;

	/// @see better_string::lower()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto lower() const -> better_string<Char, Traits, Allocator>;

	/// @see better_string::title()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto title() const -> better_string<Char, Traits, Allocator>;

	/// @see better_string::isupper()
	template<Encoding E = default_encoding__>
	auto isupper() const -> bool;

	/// @see better_string::islower()
	template<Encoding E = default_encoding__>
	auto islower() const -> bool;

	/// @see better_string::istitle()
	template<Encoding E = default_encoding__>
	auto istitle() const -> bool;

	/// @see better_string::capitalize()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto capitalize() const -> better_string<Char, Traits, Allocator>;

	/// @see better_string::casefold()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto casefold() const -> better_string<Char, Traits, Allocator>;

	/// @see better_string::swapcase()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>>
	auto swapcase() const -> better_string<Char, Traits, Allocator>;

	// Transcoding functions

	/// @see better_string::decode()
	template<Encoding From, typename CharTo = char, typename Allocator = std::allocator<Char>>
	auto decode(errors mode = errors::Strict) const -> better_string<CharTo, std::char_traits<CharTo>, Allocator>
	{
		better_string<CharTo, std::char_traits<CharTo>, Allocator> result;
		algorithm::string::transcode<decltype(*this), Traits, From, decltype(result) &, std::char_traits<CharTo>, default_encoding<CharTo>::value>(*this, result, mode);
		return result;
	}

	/// @see better_string::format()
	template<Encoding From, Encoding To, typename CharTo = typename encoding_traits<To>::char_type, typename Allocator = std::allocator<Char>>
	auto transcode(errors mode = errors::Strict) const -> better_string<CharTo, std::char_traits<CharTo>, Allocator>
	{
		better_string<CharTo, std::char_traits<CharTo>, Allocator> result;
		algorithm::string::transcode<decltype(*this), Traits, From, decltype(result) &, std::char_traits<CharTo>, To>(*this, result, mode);
		return result;
	}

	// Formatting functions

	/// @see better_string::format()
	template<Encoding E = default_encoding__, typename Allocator = std::allocator<Char>, typename... Types>
	auto format(Types && ... values) const -> better_string<Char, Traits, Allocator>
		{return algorithm::string::format<decltype(*this), Traits, E, better_string<Char, Traits, Allocator>, const Types & ...>(*this, static_cast<const Types &>(values) ...);}

	// Magic functions

	/// Used by @ref str() to convert this type to a string.
	template<typename CharTo, Encoding To>
	static auto str__(better_string_view str) -> better_string<CharTo>
		{return str.transcode<default_encoding__, To, CharTo>(errors::Replace);}

	/// Used by @ref repr() to create a string representation of this type.
	template<typename CharTo, Encoding To>
	static auto repr__(better_string_view str) -> better_string<CharTo>
		{return algorithm::string::quote<better_string_view, Traits, default_encoding__, To, better_string<CharTo>, false>(str);}

	/// Used by @ref ascii() to create a ASCII only representation of this type.
	template<typename CharTo, Encoding To>
	static auto ascii__(better_string_view str) -> better_string<CharTo>
		{return algorithm::string::quote<better_string_view, Traits, default_encoding__, To, better_string<CharTo>, true>(str);}

	/// Used by @ref format() to format the appearance of this type.
	template<typename CharTo, Encoding To>
	static auto format__(better_string_view str, impl::Specifier<CharTo, To> && spec) -> better_string<CharTo>
	{
		// Transcode string
		auto result = str.transcode<default_encoding__, To, CharTo>(errors::Replace);

		// Process format specification
		if (spec.type && spec.type != 's')
			throw std::invalid_argument("string::format__(): spec: Invalid format code!");
		if (spec.sign)
			throw std::invalid_argument("string::format__(): spec: Sign is not allowed!");
		if (spec.align == '=')
			throw std::invalid_argument("string::format__(): spec: Numeric alignment (=) is not allowed!");
		if (spec.alter)
			throw std::invalid_argument("string::format__(): spec: Alternate form (#) is not allowed!");
		if (spec.comma)
			throw std::invalid_argument("string::format__(): spec: Comma separator (,) is not allowed!");
		if (spec.other.size())
			throw std::invalid_argument("string::format__(): spec: Invalid format specification!");

		if (spec.precision != size_t(-1))
			result = algorithm::string::truncate<decltype((result)), std::char_traits<CharTo>, To, better_string<CharTo>>(result, spec.precision);
		if (spec.width != size_t(-1))
		{
			if (spec.align == 0)
				spec.align = '<';
			if (spec.fill.empty())
				spec.fill = string_literal<Char, ' '>();

			if (spec.align == '<')
				result = result.template ljust<To>(spec.width, spec.fill);
			else if (spec.align == '>')
				result = result.template rjust<To>(spec.width, spec.fill);
			else if (spec.align == '^')
				result = result.template center<To>(spec.width, spec.fill);
			else
				throw std::invalid_argument("string::format__(): spec: Invalid alignment!");
		}

		// Return result
		return result;
	}
};

// Create better strings from literals

inline auto better(const char * str) -> better_string_view<char>
	{return better_string_view<char>(str);}

inline auto better(const char16_t * str) -> better_string_view<char16_t>
	{return better_string_view<char16_t>(str);}

inline auto better(const char32_t * str) -> better_string_view<char32_t>
	{return better_string_view<char32_t>(str);}

// Promote standard strings to better strings

template<typename Char, typename Traits, typename Allocator>
auto better(basic_string<Char, Traits, Allocator> & str) -> better_string<Char, Traits, Allocator> &
	{return static_cast<better_string<Char, Traits, Allocator> &>(str);}

template<typename Char, typename Traits, typename Allocator>
auto better(const basic_string<Char, Traits, Allocator> & str) -> const better_string<Char, Traits, Allocator> &
	{return static_cast<const better_string<Char, Traits, Allocator> &>(str);}

template<typename Char, typename Traits, typename Allocator>
auto better(basic_string<Char, Traits, Allocator> && str) -> better_string<Char, Traits, Allocator> &&
	{return static_cast<better_string<Char, Traits, Allocator> &&>(str);}

template<typename Char, typename Traits, typename Allocator>
auto better(const basic_string<Char, Traits, Allocator> && str) -> const better_string<Char, Traits, Allocator> &&
	{return static_cast<const better_string<Char, Traits, Allocator> &&>(str);}

template<typename Char, typename Traits>
auto better(basic_string_view<Char, Traits> & str) -> better_string_view<Char, Traits> &
	{return static_cast<better_string_view<Char, Traits> &>(str);}

template<typename Char, typename Traits>
auto better(const basic_string_view<Char, Traits> & str) -> const better_string_view<Char, Traits> &
	{return static_cast<const better_string_view<Char, Traits> &>(str);}

template<typename Char, typename Traits>
auto better(basic_string_view<Char, Traits> && str) -> better_string_view<Char, Traits> &&
	{return static_cast<better_string_view<Char, Traits> &&>(str);}

template<typename Char, typename Traits>
auto better(const basic_string_view<Char, Traits> && str) -> const better_string_view<Char, Traits> &&
	{return static_cast<const better_string_view<Char, Traits> &&>(str);}


// Promote standard string views to better string views


//	------------------------------------------------------------
//		Iterable view
//	------------------------------------------------------------

template<typename Iter>
class iterable_view
{
public:
	// Aliases
	using iterator = Iter;
	using const_iterator = Iter;

	// Constructors
	constexpr iterable_view() = default;
	constexpr iterable_view(Iter begin, Iter end)
		: _begin(begin), _end(end) {}

	// Iterators
	Iter begin() const
		{return _begin;}
	Iter end() const
		{return _end;}

private:
	// Fields
	Iter _begin = {};
	Iter _end = {};
};

//	------------------------------------------------------------
//		Free functions
//	------------------------------------------------------------

template<typename Char = char, Encoding E = default_encoding<Char>::value, typename T>
auto str(const T & value) -> better_string<Char>
	{return format_proxy<T>::type::template str__<Char, E>(value);}

template<typename Char = char, Encoding E = default_encoding<Char>::value, typename T>
auto repr(const T & value) -> better_string<Char>
	{return format_proxy<T>::type::template repr__<Char, E>(value);}

template<typename Char = char, Encoding E = default_encoding<Char>::value, typename T>
auto ascii(const T & value) -> better_string<Char>
	{return format_proxy<T>::type::template ascii__<Char, E>(value);}

template<Encoding E = default_encoding<char>::value, typename... Types>
auto format(basic_string_view<char> format, Types && ... values) -> better_string<char>
	{return better(format).format<E>(static_cast<Types &&>(values) ...);}

template<Encoding E = default_encoding<char16_t>::value, typename... Types>
auto format(basic_string_view<char16_t> format, Types && ... values) -> better_string<char16_t>
	{return better(format).format<E>(static_cast<Types &&>(values) ...);}

template<Encoding E = default_encoding<char32_t>::value, typename... Types>
auto format(basic_string_view<char32_t> format, Types && ... values) -> better_string<char32_t>
	{return better(format).format<E>(static_cast<Types &&>(values) ...);}

//	------------------------------------------------------------
//		Character info
//	------------------------------------------------------------

/************************************************************
 * @brief Information about ASCII characters.
 *
 */
class Ascii
{
public:
	// Character class functions
	static auto isascii(uint32_t ch) -> bool
		{return ch < 0x80;}
	static auto isalpha(uint32_t ch) -> bool
		{return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');}
	static auto isalnum(uint32_t ch) -> bool
		{return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');}
	static auto isdigit(uint32_t ch) -> bool
		{return ch >= '0' && ch <= '9';}
};

/************************************************************
 * @brief Information about Unicode characters.
 *
 * Unicode functions are not headers only (because putting the Unicode character database into a header file would be
 * stupid). Some functions can use them, if they are available, by replacing the default @ref Ascii with @ref Unicode
 * in the template parameters.
 */
class Unicode
{
public:
	// Get unicode character information
	static auto chartype(uint32_t ch) -> uint32_t;

	// ASCII character class functions
	static auto isascii(uint32_t ch) -> bool
		{return ch < 0x80;}
	static auto isalpha(uint32_t ch) -> bool
		{uint8_t type = chartype(ch); return type == 'L';}
	static auto isalnum(uint32_t ch) -> bool
		{uint8_t type = chartype(ch); return (type == 'L') || (type == 'N');}

	// Unicode character class functions
	static auto isletter(uint32_t ch) -> bool
		{uint8_t type = chartype(ch); return type == 'L';}
	static auto ismark(uint32_t ch) -> bool
		{uint8_t type = chartype(ch); return type == 'M';}
};

//	------------------------------------------------------------
//		Codecs
//	------------------------------------------------------------


/************************************************************
 * @brief Encoder for UTF-8 strings.
 *
 */
class UTF8Encoder
{
public:
	// General encoder
	template<typename Write, decltype(std::declval<Write>()(char()), void()) * = nullptr>
	static bool encode(Write write, uint32_t cp)
	{
		if (cp < 0x80)
		{
			write(char(cp));
			return true;
		}
		else if (cp < 0x800)
		{
			write(char(0xC0 | (cp >> 6)));
			write(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else if (cp < 0x10000)
		{
			if (cp & 0xF800 == 0xD8)
				return false;
			write(char(0xE0 | (cp >> 12)));
			write(char(0x80 | ((cp >> 6) & 0x3F)));
			write(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else if (cp < 0x110000)
		{
			write(char(0xF0 | (cp >> 18)));
			write(char(0x80 | ((cp >> 12) & 0x3F)));
			write(char(0x80 | ((cp >> 6) & 0x3F)));
			write(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else
			return false;
	}

	// String encoder
	template<typename Char>
	static bool append(std::basic_string<Char> & str, uint32_t cp)
	{
		if (cp < 0x80)
		{
			str.push_back(char(cp));
			return true;
		}
		else if (cp < 0x800)
		{
			str.push_back(char(0xC0 | (cp >> 6)));
			str.push_back(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else if (cp < 0x10000)
		{
			if (cp & 0xF800 == 0xD8)
				return false;
			str.push_back(char(0xE0 | (cp >> 12)));
			str.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
			str.push_back(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else if (cp < 0x110000)
		{
			str.push_back(char(0xF0 | (cp >> 18)));
			str.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
			str.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
			str.push_back(char(0x80 | (cp & 0x3F)));
			return true;
		}
		else
			return false;
	}
};

/************************************************************
 * @brief Encoder for UTF-16 strings.
 *
 */
class UTF16Encoder
{
public:
	// General encoder
	template<typename Write, decltype(std::declval<Write>()(char16_t()), void()) * = nullptr>
	static bool encode(Write write, uint32_t cp)
	{
		if (cp < 0x10000)
		{
			if (cp & 0xF800 == 0xD8)
				return false;
			write(char16_t(cp));
			return true;
		}
		else if (cp < 0x110000)
		{
			cp -= 10000;
			write(char16_t(0xD800 | (cp >> 10)));
			write(char16_t(0xDC00 | (cp & 0x3FF)));
			return true;
		}
		return false;
	}

	// String encoder
	template<typename Char>
	static bool append(std::basic_string<Char> & str, uint32_t cp)
	{
		if (cp < 0x10000)
		{
			if (cp & 0xF800 == 0xD8)
				return false;
			str.push_back(char16_t(cp));
			return true;
		}
		else if (cp < 0x110000)
		{
			cp -= 10000;
			str.push_back(char16_t(0xD800 | (cp >> 10)));
			str.push_back(char16_t(0xDC00 | (cp & 0x3FF)));
			return true;
		}
		return false;
	}
};

/************************************************************
 * @brief Encoder for UTF-32 strings.
 *
 */
class UTF32Encoder
{
public:
	// General encoder
	template<typename Write, decltype(std::declval<Write>()(char32_t()), void()) * = nullptr>
	static bool encode(Write write, uint32_t cp)
	{
		if ((cp & 0xF800 != 0xD8) && (cp < 0x110000))
		{
			write(char32_t(cp));
			return true;
		}

		return false;
	}

	// String encoder
	template<typename Char>
	static bool append(std::basic_string<Char> & str, uint32_t cp)
	{
		if ((cp & 0xF800 != 0xD8) && (cp < 0x110000))
		{
			str.push_back(char32_t(cp));
			return true;
		}

		return false;
	}
};

//	------------------------------------------------------------
//		Iterators
//	------------------------------------------------------------

/************************************************************
 * @brief Iterator for UTF-8 strings.
 *
 */
template<typename Char>
class UTF8Iterator
{
public:
	// Constructors
	constexpr UTF8Iterator() {}
	constexpr UTF8Iterator(const Char * ptr)
		: ptr(ptr) {}

	// Conversions
	explicit constexpr operator const Char * ()
		{return ptr;}

	// Helpers
	static constexpr bool ascii(uint8_t ch)
		{return ch < 0x80;}
	static constexpr bool head(uint8_t ch)
		{return ch > 0xC0;}
	static constexpr bool head2(uint8_t ch)
		{return ch < 0xE0;}
	static constexpr bool head3(uint8_t ch)
		{return ch < 0xF0;}
	static constexpr bool head4(uint8_t ch)
		{return ch < 0xF8;}
	static constexpr bool tail(uint8_t ch)
		{return (ch & 0xC0) == 0x80;}

	// Interface
	auto operator ++ () -> UTF8Iterator &
	{
		uint8_t ch = ptr[0];

		if (!head(ch))
			++ ptr;
		else if (head2(ch))
		{
			if (tail(ptr[1]))
				ptr += 2;
		}
		else if (head3(ch))
		{
			if (tail(ptr[1]) && tail(ptr[2]))
				ptr += 3;
		}
		else if (head4(ch))
		{
			if (tail(ptr[1]) && tail(ptr[2]) && tail(ptr[3]))
				ptr += 4;;
		}
		else
			++ ptr;

		return * this;
	}

	auto operator ++ (int) -> UTF8Iterator
		{auto prev = * this; ++ * this; return prev;}

	auto operator -- () -> UTF8Iterator &
	{
		if (tail(ptr[-1]))
		{
			if (head2(ptr[-2]) && head(ptr[-2]))
				ptr -= 2;
			else if (tail(ptr[-2]))
			{
				if (head3(ptr[-3]) && !head2(ptr[-3]))
					ptr -= 3;
				else if (tail(ptr[-3]))
				{
					if (head4(ptr[-3]) && !head3(ptr[-3]))
						ptr -= 4;
					else
						-- ptr;
				}
				else
					-- ptr;
			}
			else
				-- ptr;
		}
		else
			-- ptr;
	}

	auto operator * () const -> int32_t
	{
			int32_t ch = uint8_t(ptr[0]);

			if (ascii(ch))
				return ch;
			if (!head(ch))
				return - ch;
			if (head2(ch))
			{
				if (!tail(ptr[1]))
					return - ch;
				int32_t codepoint = (ch & 0x1F) << 6 | (ptr[1] & 0x3F);
				if (codepoint < 0x80)
					return -1;
				return codepoint;
			}
			if (head3(ch))
			{
				if (!tail(ptr[1]) && !tail(ptr[2]))
					return - ch;
				int32_t codepoint = (ch & 0x0F) << 12 | (ptr[1] & 0x3F) << 6 | (ptr[2] & 0x3F);
				if (codepoint < 0x800 || (codepoint & 0xF800) == 0xD800)
					return -1;
				return codepoint;
			}
			if (head4(ch))
			{
				if (!tail(ptr[1]) && !tail(ptr[2]) && !tail(ptr[3]))
					return - ch;
				int32_t codepoint = (ch & 0x07) << 18 | (ptr[1] & 0x3F) << 12 | (ptr[2] & 0x3F) << 6 | (ptr[3] & 0x3F);
				if (codepoint < 0x10000 || codepoint > 0x10FFFF)
					return -1;
				return codepoint;
			}
			return - ch;
	}

	// Binary operators
	friend auto operator != (UTF8Iterator left, UTF8Iterator right) -> bool
		{return left.ptr < right.ptr; /* This is not a bug, UTF8Iterator needs "<" not "!=" */}
	friend auto operator - (UTF8Iterator left, UTF8Iterator right) -> size_t
	{
		size_t len = 0;
		while (right.ptr < left.ptr)
			{++ len; ++ right;}
		return len;
	}

private:
	// Fields
	const Char * ptr = nullptr;
};


/************************************************************
 * @brief Iterator for UTF-16 strings.
 *
 */
template<typename Char>
class UTF16Iterator
{
public:
	// Constructors
	constexpr UTF16Iterator() {};
	constexpr UTF16Iterator(const Char * ptr)
		: ptr(ptr) {}

	// Conversions
	explicit constexpr operator const Char * ()
		{return ptr;}

	// Helpers
	static constexpr bool is_surrogate_first(uint16_t ch)
		{return (ch & 0xFC00) == 0xD800;}
	static constexpr bool is_surrogate_last(uint16_t ch)
		{return (ch & 0xFC00) == 0xDC00;}

	// Interface
	auto operator ++ () -> UTF16Iterator &
	{
		++ ptr;
		if (is_surrogate_first(ptr[-1]) && is_surrogate_last(ptr[0]))
			++ ptr;
		return * this;
	}
	auto operator ++ (int) -> UTF16Iterator
		{auto prev = * this; ++ * this; return prev;}

	auto operator -- () -> UTF16Iterator &
	{
		-- ptr;
		if (is_surrogate_last(ptr[0]) && is_surrogate_first(ptr[-1]))
			-- ptr;
		return * this;
	}
	auto operator -- (int) -> UTF16Iterator
		{auto prev = * this; -- * this; return prev;}

	auto operator * () const -> int32_t
	{
		if (is_surrogate_first(ptr[0]) && is_surrogate_last(ptr[1]))
			return ((ptr[0] & 0x3FF) << 10) + (ptr[1] & 0x3FF) + 0x10000;
		return ptr[0];
	}

	// Binary operators
	friend auto operator != (UTF16Iterator left, UTF16Iterator right) -> bool
		{return left.ptr < right.ptr; /* This is not a bug, UTF16Iterator needs "<" not "!=" */}
	friend auto operator - (UTF16Iterator left, UTF16Iterator right) -> size_t
	{
		size_t len = 0;
		while (right.ptr < left.ptr)
			{++ len; ++ right;}
		return len;
	}

private:
	// Fields
	const Char * ptr = nullptr;
};

/************************************************************
 * @brief Iterator for UTF-32 strings.
 *
 */
template<typename Char>
class UTF32Iterator
{
public:
	// Constructors
	constexpr UTF32Iterator() {};
	constexpr UTF32Iterator(const Char * ptr)
		: ptr(ptr) {}

	// Conversions
	explicit constexpr operator const Char * ()
		{return ptr;}

	// Interface
	auto operator ++ () -> UTF32Iterator &
		{++ ptr; return * this;}
	auto operator ++ (int) -> UTF32Iterator
		{return UTF32Iterator(ptr ++);}
	auto operator -- () -> UTF32Iterator &
		{-- ptr; return * this;}
	auto operator -- (int) -> UTF32Iterator
		{return UTF32Iterator(ptr --);}
	auto operator * () const -> int32_t
		{return (*ptr & 0xF800 != 0xD8) && (*ptr < 0x110000) ? *ptr : -1;}

	// Binary operators
	friend auto operator != (UTF32Iterator left, UTF32Iterator right) -> bool
		{return left.ptr != right.ptr;}
	friend auto operator - (UTF32Iterator left, UTF32Iterator right) -> size_t
		{return left.ptr - right.ptr; }

private:
	// Fields
	const Char * ptr = nullptr;
};

//	------------------------------------------------------------
//		Formatting proxies
//	------------------------------------------------------------

// Default formatting proxy - passthrough

template<typename T> struct format_proxy
	{using type = T;};

// Pointer formatting proxies

template<typename T> struct format_proxy<const T *>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const T * value) -> better_string<Char>;

		template<typename Char, Encoding E>
		static auto repr__(const T * value) -> better_string<Char>;

		template<typename Char, Encoding E>
		static auto ascii__(const T * value) -> better_string<Char>;

		template<typename Char, Encoding E>
		static auto format__(const T * value, better_string_view<Char> spec) -> better_string<Char>;
	};
};

template<typename T> struct format_proxy<T *> : format_proxy<const T *> {};

// Boolean

template<> struct format_proxy<bool>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(bool value) -> better_string<Char>
			{return better(value ? "true" : "false").transcode<Encoding::Char8, unsafe_encoding<Char>::value, Char>(better_string<char>::errors::Replace);}

		template<typename Char, Encoding E>
		static auto repr__(bool value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto ascii__(bool value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto format__(bool value, impl::Specifier<Char, E> && spec) -> better_string<Char>
		{
			// Convert as integer
			if (spec.type)
				return format_proxy<impl::first_t<int32_t, Char>>::type::template format__<Char, E>(value, std::move(spec));

			// Convert as string
			spec.type = 's';
			return better_string_view<char>::template format__<Char, E>(value ? "true" : "false", std::move(spec));
		}
	};
};

// Integers

template<> struct format_proxy<int64_t>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(int64_t value) -> better_string<Char>
		{
			// Create result
			better_string<Char> result;
			better_string<Char> number;

			// Value
			uint64_t n = value < 0 ? -value : value;
			do
			{
				number.push_back('0' + n % 10);
				n /= 10;
			}
			while (n > 0);

			// Sign
			if (value < 0)
				number.push_back('-');

			// Reverse number
			for (size_t i = number.size(); i > 0;)
				result.push_back(number[-- i]);

			// return result
			return result;
		}

		template<typename Char, Encoding E>
		static auto repr__(int64_t value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto ascii__(int64_t value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto format__(int64_t value, impl::Specifier<Char, E> && spec) -> better_string<Char>
		{
			// Handle character
			if (spec.type == 'c')
			{
				spec.type = 's';
				better_string<Char> temp;
				if (!encoding_traits<E>::append(temp, value))
					encoding_traits<E>::append(temp, encoding_traits<E>::replacement);

				return better_string_view<Char>::template format__<Char, E>(temp, std::move(spec));
			}

			// Process format specification
			int8_t base = 0;
			switch (spec.type)
			{
				// Integers
				case 'b': base = 2; break;
				case 'o': base = 8; break;
				case 0: case 'd': case 'n': base = 10; break;
				case 'x': case 'X': base = 16; break;

				// Floats
				case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case '%':
					return format_proxy<impl::first_t<double, Char>>::type::template format__<Char, E>(value, std::move(spec));

				// Invalid
				default:
					throw std::invalid_argument("int64_t::format__(): spec: Invalid format code!");
			}
			if (spec.comma)
				throw std::invalid_argument("int64_t::format__(): spec: Comma separator (,) is not allowed!");
			if (spec.precision != size_t(-1))
				throw std::invalid_argument("int64_t::format__(): spec: Precision (.) is not allowed!");
			if (!spec.other.empty())
				throw std::invalid_argument("int64_t::format__(): spec: Invalid format specification!");

			// Create result
			better_string<Char> result;
			better_string<Char> number;

			// Sign
			if (value < 0)
				result.push_back('-');
			else if (spec.sign)
				result.push_back('+');

			// Alter
			if (spec.alter && base != 10)
			{
				result.push_back('0');
				result.push_back(spec.type);
			}

			// Value
			{
				const char * digits = (spec.type == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
				uint64_t n = value < 0 ? -value : value;
				do
				{
					number.push_back(digits[n % base]);
					n /= base;
				}
				while (n > 0);
			}

			// Width
			if (spec.width != size_t(-1))
			{
				if (spec.fill.empty())
					spec.fill = string_literal<Char, ' '>();
				if (spec.align == 0)
					spec.align = '>';

				// Numeric align
				if (spec.align == '=' && spec.width > result.size() + number.size())
				{
					for (size_t i = spec.width - result.size() - number.size(); i > 0; -- i)
						result.extend(spec.fill);
				}

				// Reverse number
				for (size_t i = number.size(); i > 0;)
					result.push_back(number[-- i]);

				// Normal align
				if (spec.align == '<')
					result = result.template ljust<E>(spec.width, spec.fill);
				else if (spec.align == '>')
					result = result.template rjust<E>(spec.width, spec.fill);
				else if (spec.align == '^')
					result = result.template center<E>(spec.width, spec.fill);
				else
					std::invalid_argument("int64_t::format__(): spec: Invalid format specification!");
			}
			else
			{
				// Reverse number
				for (size_t i = number.size(); i > 0;)
					result.push_back(number[-- i]);
			}

			// return result
			return result;
		}
	};
};

template<> struct format_proxy<uint64_t>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(uint64_t value) -> better_string<Char>
		{
			// Create result
			better_string<Char> result;
			better_string<Char> number;

			// Value
			do
			{
				number.push_back('0' + value % 10);
				value /= 10;
			}
			while (value > 0);

			// Reverse number
			for (size_t i = number.size(); i > 0;)
				result.push_back(number[-- i]);

			// return result
			return result;
		}

		template<typename Char, Encoding E>
		static auto repr__(uint64_t value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto ascii__(uint64_t value) -> better_string<Char>
			{return str__<Char, E>(value);}

		template<typename Char, Encoding E>
		static auto format__(uint64_t value, impl::Specifier<Char, E> && spec) -> better_string<Char>
		{
			// Handle character
			if (spec.type == 'c')
			{
				spec.type = 's';
				better_string<Char> temp;
				if (!encoding_traits<E>::append(temp, value))
					encoding_traits<E>::append(temp, encoding_traits<E>::replacement);

				return better_string_view<Char>::template format__<Char, E>(temp, std::move(spec));
			}

			// Process format specification
			int8_t base = 0;
			switch (spec.type)
			{
				// Integers
				case 'b': base = 2; break;
				case 'o': base = 8; break;
				case 0: case 'd': case 'n': base = 10; break;
				case 'x': case 'X': base = 16; break;

				// Floats
				case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case '%':
					return format_proxy<impl::first_t<double, Char>>::type::template format__<Char, E>(value, std::move(spec));

				// Invalid
				default:
					throw std::invalid_argument("uint64_t::format__(): spec: Invalid format code!");
			}
			if (spec.comma)
				throw std::invalid_argument("uint64_t::format__(): spec: Comma separator (,) is not allowed!");
			if (spec.precision != size_t(-1))
				throw std::invalid_argument("uint64_t::format__(): spec: Precision (.) is not allowed!");
			if (!spec.other.empty())
				throw std::invalid_argument("uint64_t::format__(): spec: Invalid format specification!");

			// Create result
			better_string<Char> result;
			better_string<Char> number;

			// Sign
			if (spec.sign)
				result.push_back('+');

			// Alter
			if (spec.alter && base != 10)
			{
				result.push_back('0');
				result.push_back(spec.type);
			}

			// Value
			{
				const char * digits = (spec.type == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
				do
				{
					number.push_back(digits[value % base]);
					value /= base;
				}
				while (value > 0);
			}

			// Width
			if (spec.width != size_t(-1))
			{
				if (spec.fill.empty())
					spec.fill = string_literal<Char, ' '>();
				if (spec.align == 0)
					spec.align = '>';

				// Numeric align
				if (spec.align == '=' && spec.width > result.size() + number.size())
				{
					for (size_t i = spec.width - result.size() - number.size(); i > 0; -- i)
						result.extend(spec.fill);
				}

				// Reverse number
				for (size_t i = number.size(); i > 0;)
					result.push_back(number[-- i]);

				// Normal align
				if (spec.align == '<')
					result = result.template ljust<E>(spec.width, spec.fill);
				else if (spec.align == '>')
					result = result.template rjust<E>(spec.width, spec.fill);
				else if (spec.align == '^')
					result = result.template center<E>(spec.width, spec.fill);
				else
					std::invalid_argument("uint64_t::format__(): spec: Invalid format specification!");
			}
			else
			{
				// Reverse number
				for (size_t i = number.size(); i > 0;)
					result.push_back(number[-- i]);
			}

			// return result
			return result;
		}
	};
};

template<> struct format_proxy<int8_t> : format_proxy<int64_t> {};
template<> struct format_proxy<int16_t> : format_proxy<int64_t> {};
template<> struct format_proxy<int32_t> : format_proxy<int64_t> {};

template<> struct format_proxy<uint8_t> : format_proxy<uint64_t> {};
template<> struct format_proxy<uint16_t> : format_proxy<uint64_t> {};
template<> struct format_proxy<uint32_t> : format_proxy<uint64_t> {};


// Floating points

template<> struct format_proxy<double>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(double value) -> better_string<Char>
			{throw std::logic_error("Not implemented!");}

		template<typename Char, Encoding E>
		static auto repr__(double value) -> better_string<Char>
			{throw std::logic_error("Not implemented!");}

		template<typename Char, Encoding E>
		static auto ascii__(double value) -> better_string<Char>
			{throw std::logic_error("Not implemented!");}

		template<typename Char, Encoding E>
		static auto format__(double value, impl::Specifier<Char, E> && spec) -> better_string<Char>
			{throw std::logic_error("Not implemented!");}
	};
};

template<> struct format_proxy<float> : format_proxy<double> {};

// String literals

template<size_t N> struct format_proxy<char[N]>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template str__<Char, E>(better_string_view<char>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto repr__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template repr__<Char, E>(better_string_view<char>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto ascii__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template ascii__<Char, E>(better_string_view<char>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto format__(const char * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char>::template format__<Char, E>(better_string_view<char>(value, N - 1), spec);}
	};
};

template<size_t N> struct format_proxy<char16_t[N]>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char16_t>::template str__<Char, E>(better_string_view<char16_t>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto repr__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char16_t>::template repr__<Char, E>(better_string_view<char16_t>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto ascii__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char16_t>::template ascii__<Char, E>(better_string_view<char16_t>(value, N - 1));}

		template<typename Char, Encoding E>
		static auto format__(const char16_t * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char16_t>::template format__<Char, E>(better_string_view<char16_t>(value, N - 1), spec);}
	};
};

template<size_t N> struct format_proxy<char32_t[N]>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char32_t>::template str__<Char, E>(better_string_view<char32_t>(value, N));}

		template<typename Char, Encoding E>
		static auto repr__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char32_t>::template repr__<Char, E>(better_string_view<char32_t>(value, N));}

		template<typename Char, Encoding E>
		static auto ascii__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char32_t>::template ascii__<Char, E>(better_string_view<char32_t>(value, N));}

		template<typename Char, Encoding E>
		static auto format__(const char32_t * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char32_t>::template format__<Char, E>(better_string_view<char32_t>(value, N), spec);}
	};
};

// String values

template<> struct format_proxy<const char *>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template str__<Char, E>(better_string_view<char>(value));}

		template<typename Char, Encoding E>
		static auto repr__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template repr__<Char, E>(better_string_view<char>(value));}

		template<typename Char, Encoding E>
		static auto ascii__(const char * value) -> better_string<Char>
			{return better_string_view<char>::template ascii__<Char, E>(better_string_view<char>(value));}

		template<typename Char, Encoding E>
		static auto format__(const char * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char>::template format__<Char, E>(better_string_view<char>(value), spec);}
	};
};

template<> struct format_proxy<const char16_t *>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char>::template str__<Char, E>(better_string_view<char16_t>(value));}

		template<typename Char, Encoding E>
		static auto repr__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char>::template repr__<Char, E>(better_string_view<char16_t>(value));}

		template<typename Char, Encoding E>
		static auto ascii__(const char16_t * value) -> better_string<Char>
			{return better_string_view<char>::template ascii__<Char, E>(better_string_view<char16_t>(value));}

		template<typename Char, Encoding E>
		static auto format__(const char16_t * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char>::template format__<Char, E>(better_string_view<char16_t>(value), spec);}
	};
};

template<> struct format_proxy<const char32_t *>
{
	struct type
	{
		template<typename Char, Encoding E>
		static auto str__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char>::template str__<Char, E>(better_string_view<char32_t>(value));}

		template<typename Char, Encoding E>
		static auto repr__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char>::template repr__<Char, E>(better_string_view<char32_t>(value));}

		template<typename Char, Encoding E>
		static auto ascii__(const char32_t * value) -> better_string<Char>
			{return better_string_view<char>::template ascii__<Char, E>(better_string_view<char32_t>(value));}

		template<typename Char, Encoding E>
		static auto format__(const char32_t * value, better_string_view<Char> spec) -> better_string<Char>
			{return better_string_view<char>::template format__<Char, E>(better_string_view<char32_t>(value), spec);}
	};
};

//	------------------------------------------------------------
//		Encoding Traits
//	------------------------------------------------------------

// Traits for unknown 8-bit character
template<> struct encoding_traits<Encoding::Char8>
{
	// Aliases
	using char_type = char;
	using traits_type = std::char_traits<char>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = const Char *;

	// Boolean traits
	static constexpr bool multichar = false;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = '?';

	// Function traits

	template<typename Char>
	static constexpr auto iter(const Char * iter) -> iterator<Char>
		{return iter;}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{string.push_back(char(cp)); return true;}
};

// Traits for unknown 16-bit character
template<> struct encoding_traits<Encoding::Char16>
{
	// Aliases
	using char_type = char16_t;
	using traits_type = std::char_traits<char16_t>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = const Char *;

	// Boolean traits
	static constexpr bool multichar = false;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = 0xFFFD;

	// Function traits

	template<typename Char>
	static constexpr auto iter(const Char *iter) -> iterator<Char>
		{return iter;}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{string.push_back(char(cp)); return true;}
};

// Traits for unknown 32-bit character
template<> struct encoding_traits<Encoding::Char32>
{
	// Aliases
	using char_type = char32_t;
	using traits_type = std::char_traits<char32_t>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = const Char *;

	// Boolean traits
	static constexpr bool multichar = false;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = 0xFFFD;

	// Function traits

	template<typename Char>
	static constexpr auto iter(const Char *iter) -> iterator<Char>
		{return iter;}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{string.push_back(char(cp)); return true;}
};

// Traits for UTF-8
template<> struct encoding_traits<Encoding::UTF8>
{
	// Aliases
	using char_type = char;
	using traits_type = std::char_traits<char>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = UTF8Iterator<Char>;

	// Boolean traits
	static constexpr bool multichar = true;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = 0xFFFD;

	// Function traits

	template<typename Char>
	static constexpr auto iter(const Char *iter) -> iterator<Char>
		{return iterator<Char>(iter);}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{return UTF8Encoder::append(string, cp);}
};

template<> struct encoding_traits<Encoding::UTF16>
{
	// Aliases
	using char_type = char16_t;
	using traits_type = std::char_traits<char16_t>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = UTF16Iterator<Char>;

	// Boolean traits
	static constexpr bool multichar = true;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = 0xFFFD;

	// Function traits
	template<typename Char>
	static constexpr auto iter(const Char *iter) -> iterator<Char>
		{return iterator<Char>(iter);}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{return UTF16Encoder::append(string, cp);}
};

template<> struct encoding_traits<Encoding::UTF32>
{
	// Aliases
	using char_type = char32_t;
	using traits_type = std::char_traits<char32_t>;
	template<typename Char> using pointer = const Char *;
	template<typename Char> using iterator = UTF32Iterator<Char>;

	// Boolean traits
	static constexpr bool multichar = false;
	static constexpr bool reversible = true;

	// Character traits
	static constexpr int32_t replacement = 0xFFFD;

	// Function traits

	template<typename Char>
	static constexpr auto iter(const Char *iter) -> iterator<Char>
		{return iterator<Char>(iter);}

	template<typename String>
	static constexpr auto append(String & string, uint32_t cp) -> bool
		{return UTF32Encoder::append(string, cp);}
};

// Close namespace "ext"
}
