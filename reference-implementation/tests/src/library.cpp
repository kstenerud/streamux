#include <gtest/gtest.h>
#include <streamux/streamux.h>

TEST(Library, version)
{
    const char* expected = "1.0.0";
    const char* actual = streamux_version();
    ASSERT_STREQ(expected, actual);
}
