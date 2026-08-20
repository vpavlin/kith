// C++ side of the parity test: fold the shared fixtures with the ACTUAL desktop
// engine (../../src/kith_engine.hpp) and print the folded book as JSON on
// stdout. Compared against the JS engine's fold of the same fixtures.
//   g++ -std=c++17 -I<nlohmann_include> fold_cpp.cpp -o fold_cpp && ./fold_cpp < fixtures.json
#include "../../src/kith_engine.hpp"
#include <iostream>
#include <sstream>

int main() {
    std::stringstream ss;
    ss << std::cin.rdbuf();
    kith::json in = kith::json::parse(ss.str());
    std::string bookId = in.value("bookId", std::string("book1"));
    std::vector<kith::Event> log;
    for (auto& j : in["log"]) log.push_back(kith::eventFromJson(j));
    kith::json folded = kith::foldBook(bookId, log);
    std::cout << folded.dump() << std::endl;
    return 0;
}
