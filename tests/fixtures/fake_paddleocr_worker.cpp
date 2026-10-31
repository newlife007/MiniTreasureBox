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

std::string argumentValue(int argc, char* argv[], const std::string& name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] == name) return argv[index + 1];
    }
    return {};
}

}  // namespace

int main(int argc, char* argv[])
{
    const std::string models = argumentValue(argc, argv, "--models");
    const std::string image = argumentValue(argc, argv, "--image");
    const std::string language = argumentValue(argc, argv, "--language");
    if (models.empty() || image.empty() || language.empty()) {
        std::cerr << "invalid arguments";
        return 4;
    }

    const std::string imageName = std::filesystem::path(image).stem().string();
    std::string behavior = environmentValue("MINITREASUREBOX_FAKE_PADDLEOCR_MODE");
    if (behavior.empty()) behavior = language;
    const std::string capturePath = environmentValue(
        "MINITREASUREBOX_FAKE_PADDLEOCR_ARGUMENTS");
    if (!capturePath.empty()) {
        std::ofstream capture(capturePath, std::ios::trunc);
        for (int index = 1; capture && index < argc; ++index) {
            if (index > 1) capture << '\n';
            capture << argv[index];
        }
    }
    if (behavior == "missing") {
        std::cout << R"({"ok":false,"error":"model_missing"})";
        return 1;
    }
    if (behavior == "fail") {
        std::cout << R"({"ok":false,"error":"process_failed"})";
        return 2;
    }
    if (behavior == "protocol") {
        std::cout << "not-json";
        return 0;
    }
    if (behavior == "slow") {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (behavior == "empty") {
        std::cout << R"({"ok":false,"error":"empty_output"})";
        return 1;
    }
    if (behavior == "noisy") {
        std::cout << "Paddle native initialization log without newline";
    }
    std::cout << R"({"ok":true,"text":"fixture text for )"
              << imageName << R"(","confidence":0.99})";
    if (behavior == "noisy") {
        std::cout << "native trailing log on the same line";
    }
    return 0;
}
