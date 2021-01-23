#include "better-string.hh"

#include <stdio.h>

// Helper functions

#define ASSERT(c) assert(c, #c, __FILE__, __LINE__)

inline void assert(bool condition, const char * message, const char * file, long line)
{
	if (!condition)
	{
		printf("Assertion Failed: %s\nFile: %s, Line: %d\n", message, file, line);
		exit(-1);
	}
}

// Testing functions

template<typename string>
void test_alignment()
{
	printf("Testing alignment functions... ");

	// string::center
	ASSERT(string("abc").center(8) == "  abc   ");
	ASSERT(string("abcd").center(8) == "  abcd  ");
	ASSERT(string("abc").center(8, "-") == "--abc---");
	ASSERT(string("abcd").center(8, "-") == "--abcd--");
	ASSERT(string("😀😀😀").center(8) == "  😀😀😀   ");
	ASSERT(string("😀😀😀😀").center(8) == "  😀😀😀😀  ");
	ASSERT(string("😀😀😀").center(8, "✏") == "✏✏😀😀😀✏✏✏");
	ASSERT(string("😀😀😀😀").center(8, "✏") == "✏✏😀😀😀😀✏✏");

	// string::ljust
	ASSERT(string("abc").ljust(8) == "abc     ");
	ASSERT(string("abc").ljust(8,  "-") == "abc-----");
	ASSERT(string("😀😀😀").ljust(8) == "😀😀😀     ");
	ASSERT(string("😀😀😀").ljust(8, "✏") == "😀😀😀✏✏✏✏✏");

	// string::rjust
	ASSERT(string("abc").rjust(8) == "     abc");
	ASSERT(string("abc").rjust(8, "-") == "-----abc");
	ASSERT(string("😀😀😀").rjust(8) == "     😀😀😀");
	ASSERT(string("😀😀😀").rjust(8, "✏") == "✏✏✏✏✏😀😀😀");

	// string::zfill
	ASSERT(string("abc").zfill(8) == "00000abc");
	ASSERT(string("+abc").zfill(8) == "+0000abc");
	ASSERT(string("-abc").zfill(8) == "-0000abc");
	ASSERT(string("😀😀😀").zfill(8) == "00000😀😀😀");
	ASSERT(string("+😀😀😀").zfill(8) == "+0000😀😀😀");
	ASSERT(string("-😀😀😀").zfill(8) == "-0000😀😀😀");

	printf("OK!\n");
}

template<typename string>
void test_search()
{
	using namespace ext;
	printf("Testing search functions... ");

	// string::find
	ASSERT(string("abcabc").find("abc") == 0);
	ASSERT(string("abc---").find("abc") == 0);
	ASSERT(string("---abc").find("abc") == 3);
	ASSERT(string("------").find("abc") == -1);
	ASSERT(string("😀😀😀😀😀😀").find("😀😀😀") == 0);
	ASSERT(string("😀😀😀✏✏✏").find("😀😀😀") == 0);
	ASSERT(string("✏✏✏😀😀😀").find("😀😀😀") == 9);
	ASSERT(string("✏✏✏✏✏✏").find("😀😀😀") == -1);

	// string::rfind
	ASSERT(string("abcabc").rfind("abc") == 3);
	ASSERT(string("abc---").rfind("abc") == 0);
	ASSERT(string("---abc").rfind("abc") == 3);
	ASSERT(string("------").rfind("abc") == -1);
	ASSERT(string("😀😀😀😀😀😀").rfind("😀😀😀") == 12);
	ASSERT(string("😀😀😀✏✏✏").rfind("😀😀😀") == 0);
	ASSERT(string("✏✏✏😀😀😀").rfind("😀😀😀") == 9);
	ASSERT(string("✏✏✏✏✏✏").rfind("😀😀😀") == -1);

	// string::index
	ASSERT(string("abcabc").index("abc") == 0);
	ASSERT(string("abc---").index("abc") == 0);
	ASSERT(string("---abc").index("abc") == 3);
	ASSERT(string("😀😀😀😀😀😀").index("😀😀😀") == 0);
	ASSERT(string("😀😀😀✏✏✏").index("😀😀😀") == 0);
	ASSERT(string("✏✏✏😀😀😀").index("😀😀😀") == 9);

	// string::rindex
	ASSERT(string("abcabc").rindex("abc") == 3);
	ASSERT(string("abc---").rindex("abc") == 0);
	ASSERT(string("---abc").rindex("abc") == 3);
	ASSERT(string("😀😀😀😀😀😀").rindex("😀😀😀") == 12);
	ASSERT(string("😀😀😀✏✏✏").rindex("😀😀😀") == 0);
	ASSERT(string("✏✏✏😀😀😀").rindex("😀😀😀") == 9);

	// string::count
	ASSERT(string("------").count("abc") == 0);
	ASSERT(string("abc---").count("abc") == 1);
	ASSERT(string("---abc").count("abc") == 1);
	ASSERT(string("abcabc").count("abc") == 2);
	ASSERT(string("✏✏✏✏✏✏").count("😀😀😀") == 0);
	ASSERT(string("😀😀😀✏✏✏").count("😀😀😀") == 1);
	ASSERT(string("✏✏✏😀😀😀").count("😀😀😀") == 1);
	ASSERT(string("😀😀😀😀😀😀").count("😀😀😀") == 2);

	printf("OK!\n");
}

template<typename string>
void test_replace()
{
	using namespace ext;
	printf("Testing replace functions... ");

	// string::replace
	ASSERT(string("aaaaaaaaa").replace("a", "b") == "bbbbbbbbb");
	ASSERT(string("aaaaaaaaa").replace("aaa", "bbb") == "bbbbbbbbb");
	ASSERT(string("abc------").replace("abc", "def") == "def------");
	ASSERT(string("---abc---").replace("abc", "def") == "---def---");
	ASSERT(string("------abc").replace("abc", "def") == "------def");
	ASSERT(string("aaa").replace("a", "abc") == "abcabcabc");
	ASSERT(string("abcabcabc").replace("abc", "a") == "aaa");
	ASSERT(string("aaa-aaa-aaa").replace("aa", "bb") == "bba-bba-bba");

	// string::translate
	ASSERT(string("abcdef").translate([] (int32_t ch) -> int32_t {return 'a';}) == "aaaaaa");
	ASSERT(string("abcdef").translate([] (int32_t ch) -> int32_t {return -1;}) == "");
	ASSERT(string("abcdef").translate([] (int32_t ch) -> int32_t {return ch - 'a' + 'A';}) == "ABCDEF");

	// string::maketrans

	// string::expandtabs
	ASSERT(string("\t").expandtabs() == "    ");
	ASSERT(string("\t\t").expandtabs() == "        ");
	ASSERT(string("\t\t\t").expandtabs() == "            ");
	ASSERT(string("a\ta\ta\t").expandtabs() == "a   a   a   ");
	ASSERT(string("aa\taa\taa\t").expandtabs() == "aa  aa  aa  ");
	ASSERT(string("aaa\taaa\taaa\t").expandtabs() == "aaa aaa aaa ");
	ASSERT(string("aaaa\taaaa\taaaa\t").expandtabs() == "aaaa    aaaa    aaaa    ");

	printf("OK!\n");
}

template<typename string>
void test_split_join()
{
	using namespace ext;
	printf("Testing split and join functions... ");

	// string::join
	ASSERT(string(" ").join(std::vector<string>({})) == "");
	ASSERT(string(" ").join(std::vector<string>({"a", "b", "c"})) == "a b c");
	ASSERT(string("abc").join(std::vector<string>({})) == "");
	ASSERT(string("abc").join(std::vector<string>({"-", "-", "-"})) == "-abc-abc-");

	// string::split (space)
	ASSERT(string("abc \t\v\n\r\f def").split() == std::vector<string>({"abc", "def"}));
	ASSERT(string("a b c d").split() == std::vector<string>({"a", "b", "c", "d"}));
	ASSERT(string("a b c d").split(2) == std::vector<string>({"a", "b", "c d"}));
	ASSERT(string("a b c d").split(1) == std::vector<string>({"a", "b c d"}));
	ASSERT(string("a b c d").split(0) == std::vector<string>({"a b c d"}));

	// string::split (string)
	ASSERT(better_string<char>("---abc---").split("abc") == std::vector<better_string<char>>({"---", "---"}));
	ASSERT(better_string<char>("abc---def").split("---") == std::vector<better_string<char>>({"abc", "def"}));
	ASSERT(better_string<char>("abc---def").split("--") == std::vector<better_string<char>>({"abc", "-def"}));
	ASSERT(better_string<char>("a-b-c-d").split("-") == std::vector<better_string<char>>({"a", "b", "c", "d"}));
	ASSERT(better_string<char>("a-b-c-d").split("-", 2) == std::vector<better_string<char>>({"a", "b", "c-d"}));
	ASSERT(better_string<char>("a-b-c-d").split("-", 1) == std::vector<better_string<char>>({"a", "b-c-d"}));
	ASSERT(better_string<char>("a-b-c-d").split("-", 0) == std::vector<better_string<char>>({"a-b-c-d"}));
	ASSERT(better_string<char>("a😀b😀c😀d").split("😀") == std::vector<better_string<char>>({"a", "b", "c", "d"}));

	// string::rsplit (space)
	ASSERT(string("abc \t\v\n\r\f def").rsplit() == std::vector<string>({"abc", "def"}));
	ASSERT(string("a b c d").rsplit() == std::vector<string>({"a", "b", "c", "d"}));
	ASSERT(string("a b c d").rsplit(2) == std::vector<string>({"a b", "c", "d"}));
	ASSERT(string("a b c d").rsplit(1) == std::vector<string>({"a b c", "d"}));
	ASSERT(string("a b c d").rsplit(0) == std::vector<string>({"a b c d"}));

	// string::rsplit (string)
	ASSERT(better_string<char>("---abc---").rsplit("abc") == std::vector<better_string<char>>({"---", "---"}));
	ASSERT(better_string<char>("abc---def").rsplit("---") == std::vector<better_string<char>>({"abc", "def"}));
	ASSERT(better_string<char>("abc---def").rsplit("--") == std::vector<better_string<char>>({"abc-", "def"}));
	ASSERT(better_string<char>("a-b-c-d").rsplit("-") == std::vector<better_string<char>>({"a", "b", "c", "d"}));
	ASSERT(better_string<char>("a-b-c-d").rsplit("-", 2) == std::vector<better_string<char>>({"a-b", "c", "d"}));
	ASSERT(better_string<char>("a-b-c-d").rsplit("-", 1) == std::vector<better_string<char>>({"a-b-c", "d"}));
	ASSERT(better_string<char>("a-b-c-d").rsplit("-", 0) == std::vector<better_string<char>>({"a-b-c-d"}));
	ASSERT(better_string<char>("a😀b😀c😀d").rsplit("😀") == std::vector<better_string<char>>({"a", "b", "c", "d"}));

	printf("OK!\n");
}

template<typename string>
void test_format()
{
	using namespace ext;
	printf("Testing formatting functions... ");
	
	// str - bool
	ASSERT(str(true) == "true");
	ASSERT(str(false) == "false");
	
	// str - int
	ASSERT(str(0) == "0");
	ASSERT(str(42) == "42");
	ASSERT(str(-42) == "-42");
	ASSERT(str(42U) == "42");
	ASSERT(str(-42U) == "4294967254");

	// str - float
//	ASSERT(str(0.f) == "0");
//	ASSERT(str(-0.f) == "-0");

	// repr - string
	ASSERT(repr("") == "\"\"");
	ASSERT(repr("abcdef") == "\"abcdef\"");
	ASSERT(repr("\a\b\f\n\r\t\v") == "\"\\a\\b\\f\\n\\r\\t\\v\"");
	ASSERT(repr("✏✏✏") == "\"✏✏✏\"");
	ASSERT(repr("😀😀😀") == "\"😀😀😀\"");
	
	// ascii - string
	ASSERT(ascii("") == "\"\"");
	ASSERT(ascii("abcdef") == "\"abcdef\"");
	ASSERT(ascii("\a\b\f\n\r\t\v") == "\"\\a\\b\\f\\n\\r\\t\\v\"");
	ASSERT(ascii("✏✏✏") == "\"\\u270f\\u270f\\u270f\"");
	ASSERT(ascii("😀😀😀") == "\"\\U0001f600\\U0001f600\\U0001f600\"");

	// string::format - general
	ASSERT(string("{{}}").format() == "{}");
	ASSERT(string("abcdef").format() == "abcdef");
	ASSERT(string("abc{}").format("def") == "abcdef");
	ASSERT(string("{}def").format("abc") == "abcdef");
	ASSERT(string("{}{}").format("abc", "def") == "abcdef");
	ASSERT(string("{0}{1}{2}").format("aaa", "bbb", "ccc") == "aaabbbccc");
	ASSERT(string("{2}{1}{0}").format("aaa", "bbb", "ccc") == "cccbbbaaa");

	// string::format - bool
	ASSERT(string("{}").format(true) == "true");
	ASSERT(string("{:8}").format(true) == "true    ");
	ASSERT(string("{:>8}").format(true) == "    true");
	ASSERT(string("{}").format(false) == "false");
	ASSERT(string("{:8}").format(false) == "false   ");
	ASSERT(string("{:>8}").format(false) == "   false");
	ASSERT(string("{:d}").format(true) == "1");
	ASSERT(string("{:4d}").format(true) == "   1");
	ASSERT(string("{:04d}").format(true) == "0001");
	ASSERT(string("{:d}").format(false) == "0");
	ASSERT(string("{:4d}").format(false) == "   0");
	ASSERT(string("{:04d}").format(false) == "0000");

	// string::format - int
	ASSERT(string("{}").format(42) == "42");
	ASSERT(string("{:6}").format(42) == "    42");
	ASSERT(string("{:+6}").format(42) == "   +42");
	ASSERT(string("{:=6}").format(42) == "    42");
	ASSERT(string("{:>6}").format(42) == "    42");
	ASSERT(string("{:<6}").format(42) == "42    ");
	ASSERT(string("{:^6}").format(42) == "  42  ");
	ASSERT(string("{:=+6}").format(42) == "+   42");
	ASSERT(string("{:>+6}").format(42) == "   +42");
	ASSERT(string("{:<+6}").format(42) == "+42   ");
	ASSERT(string("{:^+6}").format(42) == " +42  ");
	ASSERT(string("{:06}").format(42) == "000042");
	ASSERT(string("{:=06}").format(42) == "000042");
	ASSERT(string("{:>06}").format(42) == "000042");
	ASSERT(string("{:<06}").format(42) == "420000");
	ASSERT(string("{:^06}").format(42) == "004200");
	ASSERT(string("{:+06}").format(42) == "+00042");
	ASSERT(string("{:=+06}").format(42) == "+00042");
	ASSERT(string("{:>+06}").format(42) == "000+42");
	ASSERT(string("{:<+06}").format(42) == "+42000");
	ASSERT(string("{:^+06}").format(42) == "0+4200");
	ASSERT(string("{:😀=+06}").format(42) == "+😀😀😀42");
	ASSERT(string("{:😀>+06}").format(42) == "😀😀😀+42");
	ASSERT(string("{:😀<+06}").format(42) == "+42😀😀😀");
	ASSERT(string("{:😀^+06}").format(42) == "😀+42😀😀");

	ASSERT(string("{}").format(-42) == "-42");
	ASSERT(string("{:6}").format(-42) == "   -42");
	ASSERT(string("{:+6}").format(-42) == "   -42");
	ASSERT(string("{:=6}").format(-42) == "-   42");
	ASSERT(string("{:>6}").format(-42) == "   -42");
	ASSERT(string("{:<6}").format(-42) == "-42   ");
	ASSERT(string("{:^6}").format(-42) == " -42  ");
	ASSERT(string("{:06}").format(-42) == "-00042");
	ASSERT(string("{:=06}").format(-42) == "-00042");
	ASSERT(string("{:>06}").format(-42) == "000-42");
	ASSERT(string("{:<06}").format(-42) == "-42000");
	ASSERT(string("{:^06}").format(-42) == "0-4200");
	ASSERT(string("{:+06}").format(-42) == "-00042");
	ASSERT(string("{:😀=+06}").format(-42) == "-😀😀😀42");
	ASSERT(string("{:😀>+06}").format(-42) == "😀😀😀-42");
	ASSERT(string("{:😀<+06}").format(-42) == "-42😀😀😀");
	ASSERT(string("{:😀^+06}").format(-42) == "😀-42😀😀");

	ASSERT(string("{}").format(42U) == "42");
	ASSERT(string("{:6}").format(42U) == "    42");
	ASSERT(string("{:+6}").format(42U) == "   +42");
	ASSERT(string("{:=6}").format(42U) == "    42");
	ASSERT(string("{:>6}").format(42U) == "    42");
	ASSERT(string("{:<6}").format(42U) == "42    ");
	ASSERT(string("{:^6}").format(42U) == "  42  ");
	ASSERT(string("{:=+6}").format(42U) == "+   42");
	ASSERT(string("{:>+6}").format(42U) == "   +42");
	ASSERT(string("{:<+6}").format(42U) == "+42   ");
	ASSERT(string("{:^+6}").format(42U) == " +42  ");
	ASSERT(string("{:06}").format(42U) == "000042");
	ASSERT(string("{:=06}").format(42U) == "000042");
	ASSERT(string("{:>06}").format(42U) == "000042");
	ASSERT(string("{:<06}").format(42U) == "420000");
	ASSERT(string("{:^06}").format(42U) == "004200");
	ASSERT(string("{:+06}").format(42U) == "+00042");
	ASSERT(string("{:=+06}").format(42U) == "+00042");
	ASSERT(string("{:>+06}").format(42U) == "000+42");
	ASSERT(string("{:<+06}").format(42U) == "+42000");
	ASSERT(string("{:^+06}").format(42U) == "0+4200");
	ASSERT(string("{:😀=+06}").format(42U) == "+😀😀😀42");
	ASSERT(string("{:😀>+06}").format(42U) == "😀😀😀+42");
	ASSERT(string("{:😀<+06}").format(42U) == "+42😀😀😀");
	ASSERT(string("{:😀^+06}").format(42U) == "😀+42😀😀");

	ASSERT(string("{:b}").format(42) == "101010");
	ASSERT(string("{:#b}").format(42) == "0b101010");
	ASSERT(string("{:#010b}").format(42) == "0b00101010");
	ASSERT(string("{:o}").format(42) == "52");
	ASSERT(string("{:#o}").format(42) == "0o52");
	ASSERT(string("{:#06o}").format(42) == "0o0052");
	ASSERT(string("{:d}").format(42) == "42");
	ASSERT(string("{:06d}").format(42) == "000042");
	ASSERT(string("{:n}").format(42) == "42");
	ASSERT(string("{:06n}").format(42) == "000042");
	ASSERT(string("{:x}").format(42) == "2a");
	ASSERT(string("{:#x}").format(42) == "0x2a");
	ASSERT(string("{:#06x}").format(42) == "0x002a");
	ASSERT(string("{:X}").format(42) == "2A");
	ASSERT(string("{:#X}").format(42) == "0X2A");
	ASSERT(string("{:#06X}").format(42) == "0X002A");

	ASSERT(string("{:b}").format(42U) == "101010");
	ASSERT(string("{:#b}").format(42U) == "0b101010");
	ASSERT(string("{:#010b}").format(42U) == "0b00101010");
	ASSERT(string("{:o}").format(42U) == "52");
	ASSERT(string("{:#o}").format(42U) == "0o52");
	ASSERT(string("{:#06o}").format(42U) == "0o0052");
	ASSERT(string("{:d}").format(42U) == "42");
	ASSERT(string("{:06d}").format(42U) == "000042");
	ASSERT(string("{:n}").format(42U) == "42");
	ASSERT(string("{:06n}").format(42U) == "000042");
	ASSERT(string("{:x}").format(42U) == "2a");
	ASSERT(string("{:#x}").format(42U) == "0x2a");
	ASSERT(string("{:#06x}").format(42U) == "0x002a");
	ASSERT(string("{:X}").format(42U) == "2A");
	ASSERT(string("{:#X}").format(42U) == "0X2A");
	ASSERT(string("{:#06X}").format(42U) == "0X002A");

	// string::format - string

	printf("OK!\n");
}

int main()
{
	using namespace ext;

	// Run tests
	test_alignment<better_string<char>>();
	test_search<better_string<char>>();
	test_replace<better_string<char>>();
	test_split_join<better_string<char>>();
	test_format<better_string<char>>();

	// On success
	printf("--------------------\nSuccess!\n");
	return 0;
}
