// The corpus replay runner: the main() of imlui_fuzz_replay_<target>, linked with one fuzz target's
// LLVMFuzzerTestOneInput (tests/fuzz/<target>.cpp). It feeds every file named on the command line, and every file
// under every folder named there (recursively, in name order), to the target once, as libFuzzer would when replaying
// a corpus. A crash is the test failing; so is finding no input at all (a wrong corpus path).
import std;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

namespace fs = std::filesystem;

std::vector<std::uint8_t> readAll(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
        const fs::path arg{argv[i]};
        std::error_code ec;
        if (fs::is_directory(arg, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator{arg})
                if (entry.is_regular_file()) inputs.push_back(entry.path());
        } else if (fs::is_regular_file(arg, ec)) {
            inputs.push_back(arg);
        } else {
            std::cerr << "no such file or folder: " << arg.string() << '\n';
            return 1;
        }
    }
    std::ranges::sort(inputs);
    if (inputs.empty()) {
        std::cerr << "no inputs\n";
        return 1;
    }
    for (const auto& file : inputs) {
        std::cout << file.string() << std::endl; // flushed: the last name printed is the input that crashed
        const auto bytes = readAll(file);
        // libFuzzer never passes a null pointer, even for an empty input.
        const std::uint8_t empty = 0;
        LLVMFuzzerTestOneInput(bytes.empty() ? &empty : bytes.data(), bytes.size());
    }
    std::cout << inputs.size() << " inputs replayed\n";
    return 0;
}
