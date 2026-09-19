#include "runtime.h"

#include <cstring>
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <smx/smx-v1.h>
#include <zlib/zlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sr {
namespace {

template <typename T>
T Read(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        throw std::runtime_error("SMX structure exceeds file bounds");

    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

void CheckMemoryBudget(const std::vector<std::uint8_t>& file) {
    const auto header = Read<sp::sp_file_hdr_t>(file, 0);

    if (header.magic != sp::SmxConsts::FILE_MAGIC || header.imagesize > 4 * 1024 * 1024 ||
        header.imagesize < sizeof(header))
        throw std::runtime_error("invalid or oversized SMX image");

    std::vector<std::uint8_t> image;

    if (header.compression == sp::SmxConsts::FILE_COMPRESSION_GZ) {
        if (header.dataoffs < sizeof(header) || header.dataoffs > header.imagesize ||
            header.disksize > file.size() || header.dataoffs >= header.disksize)
            throw std::runtime_error("invalid compressed SMX bounds");

        image.resize(header.imagesize);
        std::memcpy(image.data(), file.data(), header.dataoffs);
        uLongf size = header.imagesize - header.dataoffs;

        if (uncompress(image.data() + header.dataoffs, &size, file.data() + header.dataoffs,
                       header.disksize - header.dataoffs) != Z_OK || size != header.imagesize - header.dataoffs)
            throw std::runtime_error("invalid compressed SMX image");
    } else if (header.compression == sp::SmxConsts::FILE_COMPRESSION_NONE) {
        if (header.imagesize > file.size())
            throw std::runtime_error("truncated SMX image");

        image = file;
    } else
        throw std::runtime_error("unsupported SMX compression");

    bool found_data = false;

    for (unsigned i = 0; i < header.sections; ++i) {
        auto section = Read<sp::sp_file_section_t>(image, sizeof(header) + i * sizeof(sp::sp_file_section_t));
        const std::uint64_t name_offset = std::uint64_t{header.stringtab} + section.nameoffs;

        if (name_offset >= image.size() || std::uint64_t{section.dataoffs} + section.size > image.size())
            throw std::runtime_error("SMX section exceeds image bounds");

        const auto* name = reinterpret_cast<const char*>(image.data() + name_offset);
        const auto* end = static_cast<const char*>(std::memchr(name, 0, image.size() - name_offset));

        if (!end)
            throw std::runtime_error("SMX section name is not terminated");

        if (std::string_view(name, end - name) != ".data")
            continue;

        if (found_data || section.size < sizeof(sp::sp_file_data_t))
            throw std::runtime_error("invalid SMX data section");

        found_data = true;
        const auto data = Read<sp::sp_file_data_t>(image, section.dataoffs);

        if (data.memsize > 16 * 1024 * 1024 || data.datasize > data.memsize)
            throw std::runtime_error("SMX memory exceeds 16 MiB budget");
    }

    if (!found_data)
        throw std::runtime_error("SMX has no data section");
}

}

void Arguments::Count(int minimum, int maximum) const {
    if (maximum < 0)
        maximum = minimum;

    if (values_[0] < minimum || values_[0] > maximum)
        throw NativeError("wrong native argument count");
}

Cell Arguments::Int(int index) const {
    if (index <= 0 || index > values_[0])
        throw NativeError("native argument index out of bounds");

    return values_[index];
}

void* Arguments::Address(Cell address, std::size_t bytes, bool aligned) const {
    if (address < 0 || bytes == 0 || bytes > 4096 ||
        (aligned && address % sizeof(Cell) != 0) ||
        static_cast<std::uint64_t>(address) + bytes > std::numeric_limits<Cell>::max())
        throw NativeError("invalid VM memory range");

    char* first = nullptr;

    for (std::size_t i = 0; i < bytes; ++i) {
        char* byte = nullptr;

        if (context_.LocalToString(address + static_cast<Cell>(i), &byte) != SP_ERROR_NONE || !byte)
            throw NativeError("VM memory range crosses an invalid address");

        if (i == 0)
            first = byte;

        if (byte != first + i)
            throw NativeError("VM memory range is not contiguous");
    }

    return first;
}

void Arguments::OutputCell(int index, Cell value) const {
    std::memcpy(Address(Int(index), sizeof(value), true), &value, sizeof(value));
}

void Arguments::OutputArray(int index, int capacity, const std::vector<Cell>& values) const {
    if (capacity < 1 || capacity > 1024 || values.size() > static_cast<std::size_t>(capacity))
        throw NativeError("output array capacity out of bounds");

    auto* output = static_cast<Cell*>(Address(Int(index), capacity * sizeof(Cell), true));
    std::fill(output, output + capacity, Cell{0});
    std::copy(values.begin(), values.end(), output);
}

std::string Arguments::String(int index, std::size_t limit) const {
    const auto address = Int(index);
    std::string text;

    if (limit > 4095)
        throw NativeError("string limit exceeded");

    for (std::size_t i = 0; i <= limit; ++i) {
        if (address < 0 || static_cast<std::uint64_t>(address) + i > std::numeric_limits<Cell>::max())
            throw NativeError("invalid string address");

        const char byte = *static_cast<char*>(Address(address + static_cast<Cell>(i), 1));

        if (byte == 0)
            return text;

        text += byte;
    }

    throw NativeError("string is too long or not terminated");
}

std::vector<Cell> Arguments::Array(int index, int count, int limit) const {
    if (count < 0 || count > limit || count > 1024)
        throw NativeError("array count out of bounds");

    if (!count)
        return {};

    const auto* data = static_cast<Cell*>(Address(Int(index), count * sizeof(Cell), true));
    return {data, data + count};
}

void Arguments::Output(int index, int capacity, const std::string& value) const {
    if (capacity < 1 || capacity > 4096)
        throw NativeError("output capacity out of bounds");

    auto* data = static_cast<char*>(Address(Int(index), capacity));

    if (value.size() >= static_cast<std::size_t>(capacity))
        throw NativeError("output buffer is too small");

    std::memcpy(data, value.c_str(), value.size() + 1);
}

SourcePawn::IPluginFunction* Arguments::Callback(int index, bool optional) const {
    if (optional && context_.IsNullFunctionId(Int(index)))
        return nullptr;

    auto* function = context_.GetFunctionById(Int(index));

    if (!function || context_.IsNullFunctionId(Int(index)))
        throw NativeError("invalid callback function");

    return function;
}

class NativeBinding final : public SourcePawn::INativeCallback {
public:
    NativeBinding(int argc, int maximum, std::function<Cell(const Arguments&)> callback)
        : argc_(argc), maximum_(maximum), callback_(std::move(callback)) {}

    void AddRef() override {
        ++references_;
    }

    void Release() override {
        if (--references_ == 0)
            delete this;
    }

    Cell Invoke(SourcePawn::IPluginContext* context, const Cell* params) override {
        try {
            Arguments args(*context, params);
            args.Count(argc_, maximum_);
            return callback_(args);
        } catch (const std::exception& error) {
            return context->ThrowNativeError("%s", error.what());
        } catch (...) {
            return context->ThrowNativeError("native threw an unknown C++ exception");
        }
    }

private:
    int argc_, maximum_;
    unsigned references_ = 1;
    std::function<Cell(const Arguments&)> callback_;
};

PawnRuntime::PawnRuntime(const std::filesystem::path& library, Log log, std::size_t timeout_ms)
    : log_(std::move(log)) {
#ifdef _WIN32
    library_ = LoadLibraryExW(std::filesystem::absolute(library).c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

    auto factory_fn = library_ ? reinterpret_cast<SourcePawn::GetSourcePawnFactoryFn>(
        GetProcAddress(static_cast<HMODULE>(library_), "GetSourcePawnFactory")) : nullptr;
#else
    library_ = dlopen(std::filesystem::absolute(library).c_str(), RTLD_NOW | RTLD_LOCAL);
    auto factory_fn = library_ ? reinterpret_cast<SourcePawn::GetSourcePawnFactoryFn>(
        dlsym(library_, "GetSourcePawnFactory")) : nullptr;
#endif
    try {
        if (!factory_fn)
            throw std::runtime_error("SourcePawn runtime library/factory unavailable");

        auto* factory = factory_fn(SOURCEPAWN_API_VERSION);

        if (!factory || factory->ApiVersion() != SOURCEPAWN_API_VERSION)
            throw std::runtime_error("SourcePawn factory API mismatch");

        environment_.reset(factory->NewEnvironment());

        if (!environment_)
            throw std::runtime_error("SourcePawn environment creation failed");

        auto* api = environment_->APIv2();
        api->SetDebugListener(this);
        environment_->EnableDebugBreak();

        if (timeout_ms < 10 || timeout_ms > 5000 || !api->InstallWatchdogTimer(timeout_ms))
            throw std::runtime_error("SourcePawn watchdog installation failed");
    } catch (...) {
        if (environment_)
            environment_->Shutdown();

        environment_.reset();
#ifdef _WIN32
        if (library_)
            FreeLibrary(static_cast<HMODULE>(library_));
#else
        if (library_)
            dlclose(library_);
#endif
        library_ = nullptr;
        throw;
    }
}

PawnRuntime::~PawnRuntime() {
    if (environment_)
        environment_->Shutdown();

    environment_.reset();
#ifdef _WIN32
    if (library_)
        FreeLibrary(static_cast<HMODULE>(library_));
#else
    if (library_)
        dlclose(library_);
#endif
}

std::unique_ptr<SourcePawn::IPluginRuntime> PawnRuntime::Load(const std::filesystem::path& path,
                                                           std::vector<std::uint8_t>* source) {
    const auto size = std::filesystem::file_size(path);

    if (size > 4 * 1024 * 1024)
        throw std::runtime_error("SMX exceeds 4 MiB limit");

    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> file(static_cast<std::size_t>(size));

    if (!input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read complete SMX");

    if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("SMX changed while reading");

    CheckMemoryBudget(file);
    auto* owned = new std::uint8_t[file.size()];
    std::memcpy(owned, file.data(), file.size());
    char error[1024]{};
    std::unique_ptr<SourcePawn::IPluginRuntime> vm(environment_->APIv2()->LoadBinaryFromMemory(
        path.string().c_str(),
        owned,
        file.size(),
        [](std::uint8_t* bytes) {
            delete[] bytes;
        },
        error,
        sizeof(error)));

    if (!vm)
        throw std::runtime_error("invalid SMX: " + std::string(error));

    if (vm->GetMemUsage() > 16 * 1024 * 1024)
        throw std::runtime_error("VM exceeds 16 MiB memory limit");

    if (source)
        *source = std::move(file);

    return vm;
}

void PawnRuntime::Bind(SourcePawn::IPluginRuntime& vm, const char* name, int argc,
                      std::function<Cell(const Arguments&)> callback, int maximum) {
    std::uint32_t index;

    if (vm.FindNativeByName(name, &index) != SP_ERROR_NONE)
        return;

    auto* binding = new NativeBinding(argc, maximum, std::move(callback));
    const auto result = vm.UpdateNativeBindingObject(index, binding, 0, nullptr);
    binding->Release();

    if (result != SP_ERROR_NONE)
        throw std::runtime_error("native binding failed: " + std::string(name));
}

bool PawnRuntime::Invoke(const std::string& id, SourcePawn::IPluginFunction* function,
                         const std::vector<Cell>& cells, const char* text, Cell& result) {
    return Execute(
        id,
        function,
        [&] {
            for (auto value : cells)
                if (function->PushCell(value) != SP_ERROR_NONE)
                    return false;

            return !text || function->PushString(text) == SP_ERROR_NONE;
        },
        result);
}

bool PawnRuntime::Execute(const std::string& id, SourcePawn::IPluginFunction* function,
                          const std::function<bool()>& push, Cell& result) {
    result = 0;

    if (!function)
        return false;

    struct Active {
        PawnRuntime& runtime;
        std::string previous;
        ~Active() {
            --runtime.depth_;
            runtime.current_.swap(previous);
        }
    } active{*this,id};
    current_.swap(active.previous);
    ++depth_;
    int error = SP_ERROR_PARAM;

    try {
        if (push())
            error = function->Execute(&result);
        else
            function->Cancel();
    } catch (...) {
        function->Cancel();
        result = 0;
        return false;
    }

    if (error != SP_ERROR_NONE)
        log_(id + ": callback failed: " + environment_->APIv2()->GetErrorString(error));

    return error == SP_ERROR_NONE;
}

PawnRuntime::CallbackArguments::CallbackArguments(const SrCallbackArgument* arguments, std::uint32_t count) {
    if (count > SR_CALLBACK_MAX_ARGUMENTS || (count && !arguments))
        throw NativeError("Invalid callback arguments.");

    values_.reserve(count);
    std::size_t payload = 0;

    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& argument = arguments[i];
        const auto type = argument.type;
        const bool array = type == SR_CALLBACK_INT32_ARRAY || type == SR_CALLBACK_FLOAT32_ARRAY;
        const bool reference = type == SR_CALLBACK_INT32_REF || type == SR_CALLBACK_FLOAT32_REF;

        if (argument.size != sizeof(argument) || type < SR_CALLBACK_INT32 || type > SR_CALLBACK_FLOAT32_ARRAY ||
            (array ? argument.flags & ~SR_CALLBACK_COPYBACK : argument.flags) ||
            (array       ? !argument.count || argument.count > SR_CALLBACK_MAX_ARRAY
             : reference ? argument.count != 1
                         : argument.count != 0))
            throw NativeError("Invalid callback argument descriptor.");

        Value value{argument,{}, {}};

        if (type == SR_CALLBACK_STRING) {
            if (!argument.value.string)
                throw NativeError("Missing callback string.");

            std::size_t length = 0;

            while (length < SR_NATIVE_BUFFER_LIMIT && argument.value.string[length])
                ++length;

            if (length == SR_NATIVE_BUFFER_LIMIT)
                throw NativeError("Callback string exceeds limit.");

            payload += length + 1;

            if (payload > SR_CALLBACK_MAX_PAYLOAD)
                throw NativeError("Callback payload exceeds limit.");

            value.text.assign(argument.value.string,length);
        } else if (array || reference) {
            const bool real = type == SR_CALLBACK_FLOAT32_ARRAY || type == SR_CALLBACK_FLOAT32_REF;

            if (real ? !argument.value.reals : !argument.value.integers)
                throw NativeError("Missing callback buffer.");

            payload += static_cast<std::size_t>(argument.count) * sizeof(Cell);

            if (payload > SR_CALLBACK_MAX_PAYLOAD)
                throw NativeError("Callback payload exceeds limit.");

            value.cells.reserve(argument.count);

            for (std::uint32_t j = 0; j < argument.count; ++j)
                value.cells.push_back(real ? std::bit_cast<Cell>(argument.value.reals[j]) : argument.value.integers[j]);
        }

        values_.push_back(std::move(value));
    }
}

bool PawnRuntime::CallbackArguments::Push(SourcePawn::IPluginFunction& function) {
    for (auto& value : values_) {
        const auto& argument = value.argument;
        int status;

        switch (argument.type) {
        case SR_CALLBACK_INT32:
            status = function.PushCell(argument.value.integer);
            break;

        case SR_CALLBACK_FLOAT32:
            status = function.PushFloat(argument.value.real);
            break;

        case SR_CALLBACK_STRING:
            status = function.PushString(value.text.c_str());
            break;

        case SR_CALLBACK_INT32_REF:
        case SR_CALLBACK_FLOAT32_REF:
            status = function.PushCellByRef(value.cells.data());
            break;

        default:
            status = function.PushArray(
                value.cells.data(), argument.count, argument.flags & SR_CALLBACK_COPYBACK ? SM_PARAM_COPYBACK : 0);

            break;
        }

        if (status != SP_ERROR_NONE)
            return false;
    }

    return true;
}

void PawnRuntime::CallbackArguments::Commit() const {
    for (const auto& value : values_) {
        const auto& argument = value.argument;
        const bool reference = argument.type == SR_CALLBACK_INT32_REF || argument.type == SR_CALLBACK_FLOAT32_REF;

        if (!reference && !(argument.flags & SR_CALLBACK_COPYBACK))
            continue;

        const bool real = argument.type == SR_CALLBACK_FLOAT32_REF || argument.type == SR_CALLBACK_FLOAT32_ARRAY;

        for (std::uint32_t i = 0; i < argument.count; ++i) {
            if (real)
                argument.value.reals[i] = std::bit_cast<float>(value.cells[i]);
            else
                argument.value.integers[i] = value.cells[i];
        }
    }
}

bool PawnRuntime::Invoke(const std::string& id, SourcePawn::IPluginFunction* function,
                         CallbackArguments& arguments, Cell& result) {
    const bool success = Execute(
        id,
        function,
        [&] {
            return arguments.Push(*function);
        },
        result);

    if (success)
        arguments.Commit();

    return success;
}

void PawnRuntime::OnDebugSpew(const char*, ...) {}

void PawnRuntime::ReportError(const SourcePawn::IErrorReport& report, SourcePawn::IFrameIterator& frames) {
    std::string message = current_ + ": " + report.Message();

    for (; !frames.Done(); frames.Next()) {
        if (frames.IsInternalFrame())
            continue;

        message += "\n  ";
        message += frames.FunctionName() ? frames.FunctionName() : "<unknown>";
        message += " at ";
        message += frames.FilePath() ? frames.FilePath() : "<native>";
        message += ":" + std::to_string(frames.LineNumber());
    }

    log_(message);
}

}
