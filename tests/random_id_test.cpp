#include "platform/random_id.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (false)

// Lower-case canonical text, version 4, RFC 4122 variant, a new value each call.
int main() {
  const auto first = playden::RandomUuid(), second = playden::RandomUuid();
  CHECK(first && second && *first != *second);
  for (const auto* id : {&*first, &*second}) {
    CHECK(id->size() == 36);
    for (std::size_t i = 0; i < id->size(); ++i) {
      const char c = (*id)[i];
      if (i == 8 || i == 13 || i == 18 || i == 23) CHECK(c == '-');
      else CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    CHECK((*id)[14] == '4');
    CHECK(std::string("89ab").find((*id)[19]) != std::string::npos);
  }
  return 0;
}
