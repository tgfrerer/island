#ifndef GUARD_ADD_TWO_H
#define GUARD_ADD_TWO_H

#ifdef __cplusplus
extern "C" {
#endif

struct pal_accum_i {
  int (*add_two)(int lhs);
};

void register_fun(pal_accum_i *api);

#ifdef __cplusplus
}
#endif

#endif