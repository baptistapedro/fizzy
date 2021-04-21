// Fizzy: A fast WebAssembly interpreter
// Copyright 2020 The Fizzy Authors.
// SPDX-License-Identifier: Apache-2.0

#include "wasi.hpp"
#include "execute.hpp"
#include "limits.hpp"
#include "parser.hpp"
#include "wasi_uvwasi.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <ostream>

namespace fizzy::wasi
{
namespace
{
WASI* wasi_impl = nullptr;

ExecutionResult return_enosys(std::any&, Instance&, const Value*, ExecutionContext&) noexcept
{
    return Value{uint32_t{wasi_impl->return_enosys()}};
}

ExecutionResult proc_exit(std::any&, Instance&, const Value* args, ExecutionContext&) noexcept
{
    // TODO: Handle error code.
    wasi_impl->proc_exit(args[0].as<uint32_t>());
    return Trap;  // Should never be reached.
}

ExecutionResult fd_write(
    std::any&, Instance& instance, const Value* args, ExecutionContext&) noexcept
{
    const auto fd = args[0].as<uint32_t>();
    const auto iov_ptr = args[1].as<uint32_t>();
    const auto iov_cnt = args[2].as<uint32_t>();
    const auto nwritten_ptr = args[3].as<uint32_t>();

    const auto ret = wasi_impl->fd_write(*instance.memory, fd, iov_ptr, iov_cnt, nwritten_ptr);
    return Value{uint32_t{ret}};
}

ExecutionResult fd_read(
    std::any&, Instance& instance, const Value* args, ExecutionContext&) noexcept
{
    const auto fd = args[0].as<uint32_t>();
    const auto iov_ptr = args[1].as<uint32_t>();
    const auto iov_cnt = args[2].as<uint32_t>();
    const auto nread_ptr = args[3].as<uint32_t>();

    const auto ret = wasi_impl->fd_read(*instance.memory, fd, iov_ptr, iov_cnt, nread_ptr);
    return Value{uint32_t{ret}};
}

ExecutionResult fd_prestat_get(
    std::any&, Instance& instance, const Value* args, ExecutionContext&) noexcept
{
    const auto fd = args[0].as<uint32_t>();
    const auto prestat_ptr = args[1].as<uint32_t>();

    const auto ret = wasi_impl->fd_prestat_get(*instance.memory, fd, prestat_ptr);
    return Value{uint32_t{ret}};
}

ExecutionResult environ_sizes_get(
    std::any&, Instance& instance, const Value* args, ExecutionContext&) noexcept
{
    const auto environc = args[0].as<uint32_t>();
    const auto environ_buf_size = args[1].as<uint32_t>();

    const auto ret = wasi_impl->environ_sizes_get(*instance.memory, environc, environ_buf_size);
    return Value{uint32_t{ret}};
}
}  // namespace

WASI::~WASI() {}

std::optional<bytes> load_file(std::string_view file, std::ostream& err) noexcept
{
    try
    {
        std::filesystem::path path{file};

        if (!std::filesystem::exists(path))
        {
            err << "File does not exist: " << path << "\n";
            return std::nullopt;
        }
        if (!std::filesystem::is_regular_file(path))
        {
            err << "Not a file: " << path << "\n";
            return std::nullopt;
        }

        std::ifstream wasm_file{path};
        if (!wasm_file)
        {
            err << "Failed to open file: " << path << "\n";
            return std::nullopt;
        }
        return bytes(std::istreambuf_iterator<char>{wasm_file}, std::istreambuf_iterator<char>{});
    }
    catch (...)
    {
        // Generic error message due to other exceptions.
        err << "Failed to load: " << file << "\n";
        return std::nullopt;
    }
}

bool run(bytes_view wasm_binary, int argc, const char* argv[], std::ostream& err)
{
    constexpr auto ns = "wasi_snapshot_preview1";
    const std::vector<ImportedFunction> wasi_functions = {
        {ns, "proc_exit", {ValType::i32}, std::nullopt, proc_exit},
        {ns, "fd_read", {ValType::i32, ValType::i32, ValType::i32, ValType::i32}, ValType::i32,
            fd_read},
        {ns, "fd_write", {ValType::i32, ValType::i32, ValType::i32, ValType::i32}, ValType::i32,
            fd_write},
        {ns, "fd_prestat_get", {ValType::i32, ValType::i32}, ValType::i32, fd_prestat_get},
        {ns, "fd_prestat_dir_name", {ValType::i32, ValType::i32, ValType::i32}, ValType::i32,
            return_enosys},
        {ns, "environ_sizes_get", {ValType::i32, ValType::i32}, ValType::i32, environ_sizes_get},
        {ns, "environ_get", {ValType::i32, ValType::i32}, ValType::i32, return_enosys},
    };


    wasi_impl = create_uvwasi().release();

    const auto uvwasi_err = wasi_impl->init(argc, argv);
    if (uvwasi_err != 0)
    {
        err << "Failed to initialise UVWASI: "
            << /* TODO uvwasi_embedder_err_code_to_string(uvwasi_err) */ ""
            << "\n";
        return false;
    }

    auto module = parse(wasm_binary);
    auto imports = resolve_imported_functions(*module, wasi_functions);
    auto instance =
        instantiate(std::move(module), std::move(imports), {}, {}, {}, MaxMemoryPagesLimit);
    assert(instance != nullptr);

    const auto start_function = find_exported_function_index(*instance->module, "_start");
    if (!start_function.has_value())
    {
        err << "File is not WASI compatible (_start not found)\n";
        return false;
    }

    // Manually validate type signature here
    // TODO: do this in find_exported_function_index
    if (instance->module->get_function_type(*start_function) != FuncType{})
    {
        err << "File is not WASI compatible (_start has invalid signature)\n";
        return false;
    }

    if (!find_exported_memory(*instance, "memory").has_value())
    {
        err << "File is not WASI compatible (no memory exported)\n";
        return false;
    }

    const auto result = execute(*instance, *start_function, {});
    if (result.trapped)
    {
        err << "Execution aborted with WebAssembly trap\n";
        return false;
    }
    assert(!result.has_value);

    return true;
}

bool load_and_run(int argc, const char** argv, std::ostream& err)
{
    const auto wasm_binary = load_file(argv[0], err);
    if (!wasm_binary)
        return false;

    return run(*wasm_binary, argc, argv, err);
}
}  // namespace fizzy::wasi
