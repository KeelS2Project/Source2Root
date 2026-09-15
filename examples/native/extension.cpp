#include <source2root/extension.hpp>
#include <random>
#include <stdexcept>

namespace {
using namespace keels2::authoring;

class RandomExtension final : public source2root::Extension {
public:
    static constexpr PluginInfo Info{"Source2Root Random", "KeelS2 Project", "1.0.0", "Random integers for script plugins"};
    static constexpr PluginRequirement Requirements[]{{"Source2Root", "1.0.0", DependencyRequirement::exact}};
    RandomExtension() : Extension("source2root.random") {}

private:
    bool OnExtensionStart() override {
        generator_.seed(std::random_device{}());
        return RegisterNative("RandomInt", &RandomExtension::RandomInt);
    }
    std::int32_t RandomInt(std::int32_t minimum, std::int32_t maximum) {
        if (minimum > maximum) throw std::invalid_argument("Minimum must not exceed maximum.");
        return std::uniform_int_distribution<std::int32_t>(minimum, maximum)(generator_);
    }
    std::mt19937 generator_;
};
}

KEELS2_PLUGIN(RandomExtension)
