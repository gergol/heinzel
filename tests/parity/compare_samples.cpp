// Compare two blocks of samples, say how far apart they are, and say WHAT ACCOUNTS FOR IT.
//
// Separate from either program that produces samples, so that neither side of the comparison also
// gets to be the judge of it.
//
// THE NAMED CAUSE IS AN ARGUMENT AND NOT A COMMENT, which is milestone M4.5's requirement rather
// than a flourish: *a test that reports a difference without attributing it does not satisfy this
// condition*. A tolerance written on its own is indistinguishable from a margin widened until the
// test went green, and the difference between those two is the whole value of a parity check. So
// the allowance and the reason it exists travel together, into the message the test prints, where a
// reader who has just watched a number change can see what it was supposed to be.
//
// The pairing is enforced rather than encouraged. An allowance above zero with nothing named is a
// usage error, and a run whose samples agree exactly says so too, so "no difference" and "a
// difference somebody explained" never read alike.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace {

std::vector<double> read_samples(const char* path) {
    std::vector<double> samples;
    std::FILE* f = std::fopen(path, "r");
    if (f == nullptr) {
        std::fprintf(stderr, "compare_samples: cannot open %s\n", path);
        std::exit(2);
    }
    double value = 0.0;
    while (std::fscanf(f, "%lf", &value) == 1) {
        samples.push_back(value);
    }
    std::fclose(f);
    return samples;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: compare_samples <a> <b> <largest allowed difference> <what accounts for it>\n");
        return 2;
    }
    const std::vector<double> a = read_samples(argv[1]);
    const std::vector<double> b = read_samples(argv[2]);
    const double allowed = std::strtod(argv[3], nullptr);
    const char* because = argv[4];

    // An allowance with no cause named is the shape this argument exists to prevent.
    if (allowed > 0.0 && (because[0] == '\0')) {
        std::fprintf(stderr, "compare_samples: an allowance above zero has to name what accounts for it\n");
        return 2;
    }

    if (a.empty() || a.size() != b.size()) {
        std::fprintf(stderr, "compare_samples: %zu samples against %zu\n", a.size(), b.size());
        return 1;
    }

    double worst = 0.0;
    std::size_t worst_at = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double gap = std::fabs(a[i] - b[i]);
        if (gap > worst) {
            worst = gap;
            worst_at = i;
        }
    }

    std::printf("%zu samples; largest difference %.3e at sample %zu; allowed %.3e\n", a.size(), worst, worst_at,
                allowed);
    if (worst > 0.0) {
        std::printf("  what accounts for it: %s\n", because);
    } else {
        std::printf("  the two sides agree exactly\n");
    }
    if (worst > allowed) {
        std::fprintf(stderr,
                     "compare_samples: the two sides disagree by more than the difference this comparison "
                     "accounts for (%s)\n",
                     because);
        return 1;
    }
    return 0;
}
