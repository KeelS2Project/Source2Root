#pragma once

#include <sp_vm_api.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sr {

using Cell = cell_t;
static_assert(sizeof(Cell) == 4);

class NativeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Arguments {
public:
    Arguments(SourcePawn::IPluginContext& context, const Cell* values) : context_(context), values_(values) {}
    void Count(int minimum, int maximum = -1) const;
    int Count() const { return values_[0]; }
    Cell Int(int index) const;
    std::string String(int index, std::size_t limit = 512) const;
    std::vector<Cell> Array(int index, int count, int limit = 1024) const;
    void Output(int index, int capacity, const std::string& value) const;
    void OutputCell(int index, Cell value) const;
    void OutputArray(int index, int capacity, const std::vector<Cell>& values) const;
    std::string Format(int index) const;
    SourcePawn::IPluginFunction* Callback(int index) const;
private:
    SourcePawn::IPluginContext& context_;
    const Cell* values_;
    void* Address(Cell address, std::size_t bytes, bool aligned = false) const;
};

class PawnRuntime final : private SourcePawn::IDebugListener {
public:
    using Log = std::function<void(const std::string&)>;
    PawnRuntime(const std::filesystem::path& library, Log log, std::size_t timeout_ms = 1000);
    ~PawnRuntime();
    PawnRuntime(const PawnRuntime&) = delete;
    PawnRuntime& operator=(const PawnRuntime&) = delete;
    std::unique_ptr<SourcePawn::IPluginRuntime> Load(const std::filesystem::path& path,
                                                   std::vector<std::uint8_t>* source = nullptr);
    void Bind(SourcePawn::IPluginRuntime& vm, const char* name, int argc,
              std::function<Cell(const Arguments&)> callback, int maximum = -1);
    bool Invoke(const std::string& id, SourcePawn::IPluginFunction* function,
                const std::vector<Cell>& cells, const char* text, Cell& result);
    bool Idle() const { return depth_ == 0; }
private:
    void* library_ = nullptr;
    std::unique_ptr<SourcePawn::ISourcePawnEnvironment> environment_;
    Log log_;
    std::string current_;
    unsigned depth_ = 0;
    void OnDebugSpew(const char*, ...) override;
    void ReportError(const SourcePawn::IErrorReport& report, SourcePawn::IFrameIterator& frames) override;
};

}
