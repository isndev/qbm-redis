// Compile twice, once per language mode. Both translation units instantiate the
// inline RESP parser; the linker must not select a result-returning body from
// the other mode when folding weak definitions.
#include <qbm/redis/parser.h>

#include <cstdio>
#include <string>

namespace {
bool
run_probe() {
    using qb::redis::parser::ParseErrorCode;
    using qb::redis::parser::RespParser;

    RespParser        parser;
    const std::string payload(256, 'p');
    const std::string wire = "$256\r\n" + payload + "\r\n";
    if (!parser.feed(wire))
        return false;
    auto parse_member = &RespParser::parse<>;
    auto value        = (parser.*parse_member)();
    if (!value.has_value() || value.value().as_string_view() != payload)
        return false;

    auto error = qb::redis::parser::parse("?\r\n");
    if (error.has_value() || error.error().code() != ParseErrorCode::INVALID_TYPE)
        return false;

    RespParser batch;
    if (!batch.feed(":42\r\n:43\r\n") || !batch.has_complete_value())
        return false;
    auto values = batch.parse_all();
    return values.size() == 2 && values[0].as_integer().value == 42 && values[1].as_integer().value == 43;
}
} // namespace

#if defined(QB_EXPECTED_ABI_PARSER_MAIN)
extern "C" int parser_helper();
int
main() {
    const bool local  = run_probe();
    const int  remote = parser_helper();
    std::printf("main selector=%d local=%d remote=%d\n", QB_COMPAT_HAS_STD_EXPECTED, local, remote);
    return local && remote == 1 ? 0 : 1;
}
#else
extern "C" int
parser_helper() {
    const bool ok = run_probe();
    std::printf("helper selector=%d result=%d\n", QB_COMPAT_HAS_STD_EXPECTED, ok);
    return ok ? 1 : 0;
}
#endif
