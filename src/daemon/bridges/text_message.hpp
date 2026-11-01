// text_message.hpp — Decode text datagrams (JSON or CSV) into flat fields.
//
// Accepted forms, one message each:
//   JSON object / array : {"imu": {"ax": 0.1}, "t": 1.5}  → imu/ax, t
//                         [1.0, 2.0]                       → 0, 1
//   key=value CSV line   : ax=0.1,ay=9.8,t=1.5             → ax, ay, t
//   plain CSV line       : 0.1,9.8                         → 0, 1
//
// A datagram that starts with '{' or '[' holds one or more whitespace-separated
// JSON values (pretty-printed or newline-delimited).  Any other datagram holds
// one CSV message per line.
//
// Numbers and booleans (as 1 / 0) become fields; strings and null are ignored.
// The parser is self-contained (the repo has no JSON DOM library outside the
// optional ROS2 adapter) and bounded: nesting deeper than MAX_JSON_DEPTH is a
// parse error and at most MAX_FIELDS_PER_MESSAGE fields are kept per message.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "field_path.hpp"

namespace spectra::daemon
{

inline constexpr size_t MAX_JSON_DEPTH         = 32;
inline constexpr size_t MAX_FIELDS_PER_MESSAGE = 1024;

// Parse one JSON value starting at text[*pos] (leading whitespace allowed) and
// append its numeric leaves to `out`.  On success *pos is just past the value.
// Returns false on a syntax error (fills *error if non-null).
bool flatten_json_value(std::string_view        text,
                        size_t*                 pos,
                        std::vector<FlatField>& out,
                        std::string*            error = nullptr);

// Convenience: the whole text must be exactly one JSON value.
bool flatten_json(std::string_view text, std::vector<FlatField>& out, std::string* error = nullptr);

// Parse one CSV line ("a=1,b=2" or "1,2").  Tokens that are not numbers are
// ignored.  Returns false when the line yields no field.
bool parse_csv_line(std::string_view line, std::vector<FlatField>& out);

// Decode a datagram into messages.  Returns the number of messages that failed
// to parse.  Messages that parse but carry no numeric field are not returned.
size_t decode_text_datagram(std::string_view datagram, std::vector<std::vector<FlatField>>& out);

}   // namespace spectra::daemon
