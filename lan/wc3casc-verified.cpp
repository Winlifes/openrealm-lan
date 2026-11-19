// Local extraction helper. CascLib is MIT licensed; see third_party/CascLib.
// Unlike the upstream sample, this checks every read, the expected size, and
// the output write, and never publishes a partial file as a successful asset.
#include "CascLib.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static fs::path relativePath(std::string name) {
    for (char &c : name) if (c == ':' || c == '\\') c = '/';
    fs::path p(name);
    if (p.is_absolute()) throw std::runtime_error("absolute CASC name");
    for (const auto &part : p)
        if (part == ".." || part == ".") throw std::runtime_error("unsafe CASC name");
    if (name.empty() || name.back() == '/' || name.find('\t') != std::string::npos ||
        name.find('\n') != std::string::npos) throw std::runtime_error("invalid CASC file name");
    return p;
}

static bool extract(HANDLE storage, const std::string &name, const fs::path &root,
                    unsigned long long &written, DWORD &error) {
    written = 0;
    HANDLE file = nullptr;
    if (!CascOpenFile(storage, name.c_str(), CASC_LOCALE_ZHCN,
                      CASC_OPEN_BY_NAME | CASC_STRICT_DATA_CHECK, &file)) {
        error = GetCascError(); return false;
    }
    ULONGLONG expected = 0;
    if (!CascGetFileSize64(file, &expected)) {
        error = GetCascError(); CascCloseFile(file); return false;
    }
    // This is a loose SD asset extractor, not a general storage downloader.
    if (expected > 1024ULL * 1024 * 1024) {
        error = 223; CascCloseFile(file); return false;
    }
    const fs::path dest = root / relativePath(name);
    const fs::path partial = dest.string() + ".partial";
    fs::create_directories(dest.parent_path());
    FILE *out = fopen(partial.c_str(), "wb");
    if (!out) { error = 5; CascCloseFile(file); return false; }
    bool ok = true;
    char buffer[256 * 1024];
    while (written < expected) {
        DWORD got = 0;
        const DWORD request = static_cast<DWORD>(std::min<ULONGLONG>(sizeof(buffer), expected - written));
        if (!CascReadFile(file, buffer, request, &got) || !got || got > request) {
            error = GetCascError(); if (!error) error = 38; ok = false; break;
        }
        if (fwrite(buffer, 1, got, out) != got) { error = 29; ok = false; break; }
        written += got;
    }
    if (fclose(out)) { error = 29; ok = false; }
    CascCloseFile(file);
    if (written != expected) ok = false;
    if (ok) { fs::rename(partial, dest); error = 0; }
    else fs::remove(partial);
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: wc3casc-verified list INSTALL CATALOG.tsv\n"
                  << "       wc3casc-verified extract-list INSTALL NAMES.txt OUTPUT REPORT.tsv\n";
        return 2;
    }
    HANDLE storage = nullptr;
    if (!CascOpenStorage(argv[2], CASC_LOCALE_ZHCN, &storage)) {
        std::cerr << "CascOpenStorage failed: " << GetCascError() << '\n'; return 1;
    }
    int result = 0;
    try {
        const std::string mode = argv[1];
        if (mode == "list") {
            std::ofstream out(argv[3]);
            if (!out) throw std::runtime_error("cannot write catalog");
            CASC_FIND_DATA data{};
            HANDLE find = CascFindFirstFile(storage, "*", &data, nullptr);
            if (!find || find == INVALID_HANDLE_VALUE) throw std::runtime_error("CASC catalog unavailable");
            unsigned long long count = 0;
            do {
                if (data.NameType == CascNameFull)
                    out << data.FileSize << '\t' << data.bFileAvailable << '\t' << data.szFileName << '\n';
                ++count;
            } while (CascFindNextFile(find, &data));
            CascFindClose(find);
            out.close();
            if (!out) throw std::runtime_error("catalog write failed");
            std::cout << "catalog entries: " << count << std::endl;
        } else if (mode == "extract-list" && argc == 6) {
            std::ifstream names(argv[3]);
            std::ofstream report(argv[5]);
            if (!names || !report) throw std::runtime_error("cannot open list/report");
            report << "status\tbytes\terror\tname\n";
            unsigned long long ok = 0, failed = 0, total = 0;
            std::string name;
            while (std::getline(names, name)) {
                if (!name.empty() && name.back() == '\r') name.pop_back();
                if (name.empty()) continue;
                unsigned long long written = 0; DWORD error = 0;
                if (extract(storage, name, argv[4], written, error)) { ++ok; total += written; }
                else { ++failed; std::cerr << "FAIL " << error << ' ' << name << std::endl; }
                report << (error ? "failed" : "ok") << '\t' << written << '\t' << error << '\t' << name << '\n';
                if ((ok + failed) % 1000 == 0) {
                    report.flush();
                    std::cout << "extracted=" << ok << " failed=" << failed << " bytes=" << total << std::endl;
                }
            }
            report.close();
            if (!report || names.bad()) throw std::runtime_error("list/report I/O failed");
            std::cout << "done extracted=" << ok << " failed=" << failed << " bytes=" << total << std::endl;
            result = failed ? 1 : 0;
        } else { throw std::runtime_error("unknown mode or invalid arguments"); }
    } catch (const std::exception &e) { std::cerr << e.what() << std::endl; result = 2; }
    CascCloseStorage(storage);
    return result;
}
