#include <chrono>
#include <random>

#include "Test.h"

namespace t {

std::filesystem::path TempDir(const std::string& name) {
    static const std::string run = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007);
    auto dir = std::filesystem::temp_directory_path() / ("ck-tests-" + run) / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace t

int main(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    int failed = 0, run = 0;
    for (const auto& c : t::Registry()) {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) continue;
        ++run;
        try {
            c.fn();
            std::cout << "[ OK ] " << c.name << "\n";
        } catch (const t::Failure& f) {
            ++failed;
            std::cout << "[FAIL] " << c.name << "\n       " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << c.name << "\n       exception: " << e.what() << "\n";
        }
    }
    std::cout << run - failed << "/" << run << " passed\n";
    return failed ? 1 : 0;
}
