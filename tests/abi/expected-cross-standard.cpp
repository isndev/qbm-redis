// Link this consumer against qbm-redis archives built in both C++20 and C++23.
// The two language modes select different qb::expected representations.
#include <qbm/redis/server_reply.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#if QB_COMPAT_HAS_STD_EXPECTED
static_assert(std::is_same_v<qb::expected<int, std::string>, std::expected<int, std::string>>);
#endif

namespace {
using qb::redis::parser::Array;
using qb::redis::parser::Integer;
using qb::redis::parser::Map;
using qb::redis::parser::Null;
using qb::redis::parser::Value;

void
require(bool ok, const char *what) {
    if (!ok) {
        std::cerr << what << '\n';
        std::exit(1);
    }
}

std::unique_ptr<Value>
heap(Value value) {
    return std::make_unique<Value>(std::move(value));
}
} // namespace

int
main() {
    const std::string payload(256, 'x'); // heap-backed: exercises destruction on both sides
    {
        Value input(payload);
        auto  result = qb::redis::extract_string(input);
        require(result.has_value() && result.value() == payload, "extract_string success");
    }
    {
        Value input(Null{});
        auto  result = qb::redis::extract_string(input);
        require(!result.has_value() && result.error() == "null value", "extract_string error");
    }
    {
        Value input(Integer{42});
        auto  extract = &qb::redis::extract_integer<>;
        auto  result  = extract(input);
        require(result.has_value() && result.value() == 42, "extract_integer success");
    }
    {
        Value input(Null{});
        auto  result = qb::redis::extract_integer(input);
        require(!result.has_value() && result.error() == "null value", "extract_integer error");
    }
    {
        Array array;
        array.elements.push_back(heap(Value(payload)));
        Value input(std::move(array));
        auto  result = qb::redis::extract_string_array(input);
        require(result.has_value() && result.value().size() == 1 && result.value().front() == payload, "extract_string_array success");
    }
    {
        Value input(Integer{1});
        auto  result = qb::redis::extract_string_array(input);
        require(!result.has_value() && result.error() == "not an array", "extract_string_array error");
    }
    {
        Map map;
        map.entries.emplace_back(heap(Value("key")), heap(Value(payload)));
        Value input(std::move(map));
        auto  result = qb::redis::extract_string_map(input);
        require(result.has_value() && result.value().at("key") == payload, "extract_string_map success");
    }
    {
        Value input(Integer{1});
        auto  result = qb::redis::extract_string_map(input);
        require(!result.has_value() && result.error() == "not a map", "extract_string_map error");
    }
    {
        Value input("123-4");
        auto  result = qb::redis::extract_stream_id(input);
        require(result.has_value() && result.value().timestamp == 123 && result.value().sequence == 4, "extract_stream_id success");
    }
    {
        Value input("invalid");
        auto  result = qb::redis::extract_stream_id(input);
        require(!result.has_value() && result.error() == "invalid stream id format", "extract_stream_id error");
    }
    {
        Array array;
        array.elements.push_back(heap(Value(payload)));
        array.elements.push_back(heap(Value(Integer{7})));
        Value input(std::move(array));
        auto  result = qb::redis::extract_score_member(input.as_array(), 0);
        require(result.has_value() && result.value().member == payload && result.value().score == 7.0, "extract_score_member success");
    }
    {
        Array array;
        array.elements.push_back(heap(Value(payload)));
        Value input(std::move(array));
        auto  result = qb::redis::extract_score_member(input.as_array(), 0);
        require(!result.has_value() && result.error() == "not enough elements for score-member pair", "extract_score_member error");
    }
    std::cout << "PASS selector=" << QB_COMPAT_HAS_STD_EXPECTED << " six values, six errors, destructors\n";
}
