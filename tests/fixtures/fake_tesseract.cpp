#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string environmentValue(const char* name)
{
    const char* value = std::getenv(name);
    return value ? value : "";
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 5 || std::string(argv[2]) != "stdout" || std::string(argv[3]) != "-l") {
        std::cerr << "invalid arguments";
        return 4;
    }

    const std::string imageName = std::filesystem::path(argv[1]).stem().string();
    const std::string language = argv[4];
    std::string behavior = environmentValue("MINITREASUREBOX_FAKE_TESSERACT_MODE");
    if (behavior.empty()) {
        behavior = language;
    }
    const std::string capturePath = environmentValue("MINITREASUREBOX_FAKE_TESSERACT_ARGUMENTS");
    if (!capturePath.empty()) {
        std::ofstream capture(capturePath, std::ios::trunc);
        if (capture) {
            for (int index = 1; index < argc; ++index) {
                if (index > 1) {
                    capture << '\n';
                }
                capture << argv[index];
            }
        }
    }
    if (behavior == "missing") {
        std::cerr << "Error opening data file";
        return 1;
    }
    if (behavior == "fail") {
        std::cerr << "fixture failure";
        return 2;
    }
    if (behavior == "slow") {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "too late";
        return 0;
    }
    if (behavior == "empty") {
        return 0;
    }
    std::cout << "fixture text for " << imageName;
    return 0;
}
