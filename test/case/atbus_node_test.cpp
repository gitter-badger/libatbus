#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <atomic>
#include <memory>
#include <limits>
#include <numeric>

#include <detail/libatbus_error.h>
#include <atbus_node.h>
#include <atbus_endpoint.h>
#include "frame/test_macros.h"


CASE_TEST(atbus_node, basic_test)
{
    atbus::node::get_hostname();
}
