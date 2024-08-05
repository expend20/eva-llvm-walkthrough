#include "EvaLLVM.h"

#include <string>
#include <fstream>
#include <iostream>

std::string read_file(const std::string& filename) {
    std::ifstream t(filename);
    std::string line;
    std::string file_contents;
    while (std::getline(t, line)) {
        file_contents += line + "\n";
    }
    return file_contents;
}

std::string read_stdin() {
    printf("Reading until 'EOF' line\n");
    std::string line;
    std::string file_contents;
    while (std::getline(std::cin, line)) {
        if (line == "EOF") {
            break;
        }
        file_contents += line + "\n";
    }
    return file_contents;
}

int main(int argc, char *argv[]) {

    /**
     * Parameters check.
     */
    if (argc != 1 && argc != 3) {
        printf("Usage: %s [{input_filename} {output_filename}]\n", argv[0]);
        return 1;
    }

    /**
     * The program to be executed.
     */
    std::string input_data_str;
    std::string output_filename = "output.ll";
    if (argc == 3) {
        input_data_str = read_file(argv[1]);
        output_filename = argv[2];
    }
    else {
        input_data_str = read_stdin();
    }

    /**
     * Compiler instance.
     */
    EvaLLVM vm;

    /**
     * Generate LLVM IR.
     */
    vm.eval(input_data_str, output_filename);
    return 0;
}
