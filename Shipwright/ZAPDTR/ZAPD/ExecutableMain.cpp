#include <stdio.h>
#include <atomic>

extern "C" int zapd_main(int argc, char* argv[]);
extern "C" int zapd_report(int argc, char* argv[], std::atomic<size_t>* extractCount, std::atomic<size_t>* totalExtract);

int main(int argc, char* argv[]) {
    return zapd_report(argc, argv, nullptr, nullptr);
}
