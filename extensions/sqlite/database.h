#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace source2root::sqlite {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Database {
public:
    explicit Database(const std::filesystem::path& filename);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    void Execute(const std::string& sql);
    bool InTransaction() const;
    std::int64_t InsertId() const;
    int Changes() const;
private:
    friend class Statement;
    sqlite3* database_ = nullptr;
    std::chrono::steady_clock::time_point deadline_;
    void Budget();
    void Check(int result) const;
};

class Statement {
public:
    Statement(std::shared_ptr<Database> database, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void BindInt(int index, std::int32_t value);
    void BindFloat(int index, double value);
    void BindString(int index, const std::string& value);
    void BindNull(int index);
    bool Step();
    void Reset();
    int Columns() const;
    bool IsNull(int column) const;
    std::int32_t Int(int column) const;
    double Float(int column) const;
    std::string String(int column) const;
private:
    std::shared_ptr<Database> database_;
    sqlite3_stmt* statement_ = nullptr;
    bool row_ = false, done_ = false, started_ = false;
    void Parameter(int index) const;
    void Column(int index) const;
};

}
