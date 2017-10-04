#ifndef GUARD_ADD_TWO_H
#define GUARD_ADD_TWO_H

#ifdef __cplusplus
extern "C" {
#endif

void register_api(void *api);

struct pal_accum_i {
  int (*add_two)(int lhs) = nullptr;
  int (*sub_three)(int lhs) = nullptr;
  const char *(*get_text)() = nullptr;
};

#ifdef __cplusplus
}
#endif

#endif
