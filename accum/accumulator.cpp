#include "accumulator.h"
#include <cmath>

int add_two(int lhs) { return lhs + 2; };

void register_fun(pal_accum_i *api) { api->add_two = &add_two; }