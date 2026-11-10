// Print the conservation corpus's recorded graph hashes, ready to paste into
// `tests/conservation/recorded.hpp`.
//
// WHY THIS EXISTS AS A COMMITTED TOOL rather than as a note. Oracle O5's own rule is that a change
// which deliberately alters the graph type breaks every hash at once, and that when it does, the
// corpus is re-recorded IN THE SAME COMMIT AS THE CHANGE, with the reason in the commit message.
// Re-recording therefore happens, by design, and a re-record done by hand-editing hex is a re-record
// nobody can check. This makes it one command.
//
// Build and run:
//     cmake --build build --target record_conservation_hashes
//     ./build/tools/record_conservation_hashes
//
// A re-record with no stated reason is worse than a wholesale break, because it is how a
// conservation oracle is quietly turned off. Say why in the commit message.
#include <cstdio>

#include "corpus.hpp"
#include "graph_hash.hpp"

using namespace heinzel;

int main() {
    std::printf("// Recorded by tools/record_conservation_hashes.cpp.\n");
    std::printf("inline constexpr RecordedGraph kRecorded[kCorpusSize] = {\n");
    for (std::size_t i = 0; i < kCorpusSize; ++i) {
        std::printf("    {\"%s\", 0x%016llxull},\n", corpus_entry(i).name.data_,
                    static_cast<unsigned long long>(corpus_hash(i)));
    }
    std::printf("};\n");
    return 0;
}
