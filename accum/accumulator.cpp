#include "accumulator.h"

// declare functions
static int add_two(int lhs);

// declare functions, and define them

static int sub_three(int lhs) { return lhs - 2; };
static const char *get_text() { return "super totally wonderful world"; };

void register_api(void *api_p) {
  auto api = reinterpret_cast<pal_accum_i *>(api_p);
  api->add_two = add_two;
  api->sub_three = sub_three;
  api->get_text = get_text;
}

static int add_two(int lhs) { return lhs + 2; }
