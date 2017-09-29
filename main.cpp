#include "accum/accumulator.h"
#include <iostream>

int main(int argc, char const *argv[]) {

  int result = 0;

  pal_accum_i *accum = new pal_accum_i();
  register_fun(accum);

  result = accum->add_two(result);

  std::cout << "hello world" << std::endl;
  std::cout << "the result is: " << result << std::endl;
  std::cout << "and with this, goodbye." << std::endl;

  // main needs to load the dynamic library and then watch the dynamic library
  // file - if there is change the library needs to be re-loaded.

  return 0;
}