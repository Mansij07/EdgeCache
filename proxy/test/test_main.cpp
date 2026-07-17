#include "test_framework.h"

using namespace std;

int g_testFailures = 0;

int main() {
    int total = 0;
    int failedCases = 0;
    for (auto& tc : testRegistry()) {
        int before = g_testFailures;
        tc.fn();
        bool ok = (g_testFailures == before);
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << tc.name << "\n";
        if (!ok) ++failedCases;
        ++total;
    }
    std::cout << "\n" << (total - failedCases) << "/" << total << " test cases passed"
              << std::endl;
    return failedCases == 0 ? 0 : 1;
}
