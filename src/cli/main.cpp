#include <iostream>

namespace {

void print_usage(std::ostream& out) {
  out << "route-engine -- C++ route and network engine (in progress)\n"
      << "\n"
      << "Commit 1 contains the graph foundation only. No subcommand is\n"
      << "implemented yet, so every invocation is a usage error.\n"
      << "\n"
      << "Planned interface:\n"
      << "  route-engine validate   --nodes N.csv --edges E.csv\n"
      << "  route-engine route      --nodes N.csv --edges E.csv --from ID --to ID\n"
      << "                          --algo dijkstra|astar [--heuristic zero|euclidean]\n"
      << "  route-engine generate   --seed S --nodes N --out-prefix PREFIX\n"
      << "  route-engine benchmark  --sizes 1000,10000,100000 --seed S --out FILE\n"
      << "\n"
      << "Exit codes:\n"
      << "  0  successful operation; route found where applicable\n"
      << "  1  valid query, but no route exists\n"
      << "  2  command-line usage error\n"
      << "  3  graph or file validation error\n"
      << "  4  heuristic-contract violation\n";
}

}  // namespace

int main() {
  print_usage(std::cerr);
  return 2;
}
