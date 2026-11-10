#pragma once

// A program as text: its buffers, each stage's domain, fields and statements, its bands, and then the tree, one node
// per line, indented by depth, with every field that is not at its default. A schedule prints as each stage's graph
// nodes, loops and other choices, then its bands. It runs at run time only. Tests pin representative listings, so a
// change of structure is reviewed as text.

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops {
namespace detail {

inline void put(std::string& out, std::int64_t x) {
    char text[24]{};
    out.append(text, std::to_chars(text, text + sizeof text, x).ptr);
}

// ` name value` where the value is not `unset`.
inline void put_field(std::string& out, std::string_view name, std::int64_t value, std::int64_t unset) {
    if (value != unset) {
        out += ' ';
        out += name;
        out += ' ';
        put(out, value);
    }
}

// `text` and the index, where it is one (not negative).
inline void put_ref(std::string& out, std::string_view text, std::int64_t index) {
    if (index >= 0) {
        out += text;
        put(out, index);
    }
}

inline void put_flag(std::string& out, std::string_view name, bool set) {
    if (set) {
        out += ' ';
        out += name;
    }
}

inline std::string_view spelled(ElemType t) { return t == ElemType::F32 ? "f32" : t == ElemType::F64 ? "f64" : "unknown"; }

// A literal extent as its number, a symbol as `d<id>`.
inline void put_shape(std::string& out, const Extent* extent, std::size_t count) {
    out += '[';
    for (std::size_t a = 0; a < count; ++a) {
        out += a > 0 ? " " : "";
        if (is_literal(extent[a])) {
            put(out, literal_of(extent[a]));
        } else {
            out += 'd';
            put(out, symbol_of(extent[a]));
        }
    }
    out += ']';
}

inline void put_shape(std::string& out, const Shape& s) {
    Extent extent[kMaxRank]{};
    for (std::int32_t a = 0; a < s.rank; ++a) {
        extent[a] = extent_at(s, a);
    }
    put_shape(out, extent, static_cast<std::size_t>(s.rank));
}

// A local `s2`, an immediate `#0.25`, a run's start `start`, or a buffer `b3(1 0 -)`: per axis of the domain the
// buffer axis it walks (`-`: broadcast) and `*hop` where one step is more than one element; then the offsets
// (`@axis+n`), a lag and a gather's indexed axis where set.
inline void put_access(std::string& out, const Access& a, std::size_t axes) {
    if (a.source != Source::Buffer) {
        out += a.source == Source::Local ? "s" : a.source == Source::Immediate ? "#" : a.source == Source::First ? "start" : "_";
        if (a.source == Source::Local) {
            put(out, a.local);
        } else if (a.source == Source::Immediate) {
            char text[32]{};
            out.append(text, std::to_chars(text, text + sizeof text, std::bit_cast<double>(a.immediate)).ptr);
        }
        return;
    }
    out += 'b';
    put(out, a.buffer);
    out += '(';
    for (std::size_t k = 0; k < axes; ++k) {
        out += k > 0 ? " " : "";
        if (a.buffer_axis[k] < 0) {
            out += '-';
        } else {
            put(out, a.buffer_axis[k]);
        }
        if (a.step[k] != 1) {
            out += '*';
            put(out, a.step[k]);
        }
    }
    out += ')';
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        if (a.offset[k] != 0) {
            out += '@';
            put(out, static_cast<std::int64_t>(k));
            out += a.offset[k] > 0 ? "+" : "";
            put(out, a.offset[k]);
        }
    }
    put_field(out, "back", a.back, 0);
    put_field(out, "indexed", a.indexed_axis, -1);
}

inline void put_buffer(std::string& out, const Buffer& b, std::size_t index) {
    out += 'b';
    put(out, static_cast<std::int64_t>(index));
    out += ' ';
    out += spelled(b.type);
    out += ' ';
    put_shape(out, b.shape);
    put_ref(out, " feed ", b.home == Home::Feed ? b.feed : -1);
    put_field(out, "value", b.value, kNoValue);
    if (b.blocked_axis >= 0) {
        out += " blocked a";
        put(out, b.blocked_axis);
        out += '/';
        put(out, b.block);
    }
    put_field(out, "turned", b.turned, -1);
    put_field(out, "prefix", b.prefix, 0);
    put_flag(out, "only-past", b.only_past);
    put_flag(out, "ramp", b.ramp);
    put_flag(out, "constant", b.home == Home::Constant);
    put_field(out, "first", b.first, -1);
    put_ref(out, " band ", b.band);
    put_ref(out, " kept a", b.kept_axis);
    put_ref(out, " maker stage ", b.maker);
    put_ref(out, " reader stage ", b.reader);
    out += '\n';
}

inline void put_stmt(std::string& out, const Stmt& st, std::size_t index, std::size_t axes) {
    constexpr std::string_view kinds[] = {"map", "load", "gather", "fold", "select"};
    constexpr std::string_view precisions[] = {"", " fast", " normal", " high"};
    constexpr std::string_view folds[] = {"sum", "max", "min", "mean", "prod"};
    out += "  s";
    put(out, static_cast<std::int64_t>(index));
    out += ' ';
    out += kinds[static_cast<std::size_t>(st.kind)];
    out += ' ';
    out += spelled(st.type);
    if (st.kind == StmtKind::Map) {
        const OpName name = heinzel::name_of(st.op);
        out += ' ';
        for (std::size_t k = 0; k < 8 && name[k] != '\0'; ++k) {
            out += name[k];
        }
    }
    if (st.computed_in != st.type) {
        out += " in-";
        out += spelled(st.computed_in);
    }
    out += precisions[static_cast<std::size_t>(st.precision)];
    put_field(out, "node", st.node, -1);
    if (st.kind == StmtKind::Fold) {
        out += st.collapse == AttrKind::Reduce ? " reduce-" : " product-";
        out += folds[static_cast<std::size_t>(st.fold)];
        put_flag(out, "strict", st.strict);
        put_flag(out, "fused", st.fused);
    }
    put_field(out, "gathered", st.gathered, 0);
    put_field(out, "rank", st.rank, 0);
    out += st.gather == Gather::Back ? " taps-back" : st.gather == Gather::Table ? " table" : "";
    put_flag(out, "in-one-turn", st.phase_in_one_turn);
    put_field(out, "reach", st.reach, 0);
    for (const Access& a : st.in) {
        if (a.source != Source::None) {
            out += ' ';
            put_access(out, a, axes);
        }
    }
    if (st.store.source != Source::None) {
        out += " -> ";
        put_access(out, st.store, axes);
    }
    out += '\n';
}

inline void put_stage(std::string& out, const Program& p, std::size_t index) {
    const Stage& st = p.stages[index];
    out += "stage ";
    put(out, static_cast<std::int64_t>(index));
    out += ' ';
    put_shape(out, st.axes.begin(), st.axes.size());
    put_ref(out, " split n", st.split);
    put_field(out, "head", st.head, 0);
    put_ref(out, " time n", st.time_loop);
    put_ref(out, " band-axis a", st.band_axis);
    put_field(out, "lead", st.lead, 0);
    put_flag(out, "kept", st.kept);
    put_flag(out, "skewed", st.skewed);
    out += '\n';
    for (std::int32_t s = 0; s < st.count; ++s) {
        put_stmt(out, p.stmts[static_cast<std::size_t>(st.first + s)], static_cast<std::size_t>(s), st.axes.size());
    }
}

// A band: its stages, its chunk, and its buffers.
inline void put_band(std::string& out, const Program& p, std::size_t index) {
    const BandInfo& b = p.bands[index];
    out += "band ";
    put(out, static_cast<std::int64_t>(index));
    out += " stages ";
    put(out, b.first);
    out += "..";
    put(out, b.first + b.count - 1);
    put_field(out, "step", b.step, 0);
    put_flag(out, "parallel", b.parallel);
    for (std::size_t buffer = 0; buffer < p.buffers.size(); ++buffer) {
        if (p.buffers[buffer].band == static_cast<std::int32_t>(index)) {
            out += " b";
            put(out, static_cast<std::int64_t>(buffer));
        }
    }
    out += '\n';
}

// The stage whose Stage node is `root` (-1: none).
inline std::int64_t stage_of_root(const Program& p, std::size_t root) {
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        if (p.stages[s].root == static_cast<std::int32_t>(root)) {
            return static_cast<std::int64_t>(s);
        }
    }
    return -1;
}

// One node: `n12 loop a1 x16 masked strip 0`, `n14 registers acc s2 first link n17`, `n20 compute s3 keeps s3`,
// `n24 close s0 keeps s3`, and in a skewed stage `n9 compute s2 reads s4 shifted`, `n10 compute s0 lane 0`.
inline void put_node(std::string& out, const Program& p, std::size_t index, std::size_t depth) {
    constexpr std::string_view kinds[] = {"stage",   "loop",       "unroll", "vector",  "turns", "registers", "when-last", "when-not-last", "compute",
                                          "init",    "accumulate", "finish", "partial", "open",  "close",     "collect",   "rank",          "prefetch"};
    constexpr std::string_view tails[] = {"", " ones", " masked", " lanes-then-ones"};
    constexpr std::string_view roles[] = {" acc", " state", " gathered"};
    const Node& n = p.nodes[index];
    out.append(2 * depth, ' ');
    out += 'n';
    put(out, static_cast<std::int64_t>(index));
    out += ' ';
    out += kinds[static_cast<std::size_t>(n.kind)];
    put_ref(out, " ", n.kind == NodeKind::Stage ? stage_of_root(p, index) : -1);
    put_ref(out, " a", n.axis);
    if (n.kind == NodeKind::Loop || n.kind == NodeKind::Unroll || n.kind == NodeKind::Vector || n.kind == NodeKind::Turns) {
        put_ref(out, " x", n.step);
    }
    out += tails[static_cast<std::size_t>(n.tail)];
    put_field(out, "rest", n.rest, 1);
    put_ref(out, " strip ", n.strip);
    put_flag(out, "peel", n.peel_first);
    put_flag(out, "start", n.peels_start);
    put_flag(out, "chunks", n.chunks_fold);
    out += n.kind == NodeKind::Registers ? roles[static_cast<std::size_t>(n.role)] : "";
    put_ref(out, " s", n.stmt);
    put_flag(out, "first", n.opens_first);
    put_ref(out, " link n", n.link);
    put_ref(out, " reads s", n.reads);
    put_flag(out, "shifted", n.shifted);
    put_ref(out, " keeps s", n.keeps);
    put_flag(out, "closes", n.closes);
    put_field(out, "slot", n.slot, 0);
    put_field(out, "slots", n.slots, 1);
    put_field(out, "lane", n.lane, -1);
    out += '\n';
}

// A planned stage's nodes: `s1 = n7 n8`, the producer it computes inside last: `s2 = n4 n5 inline(n1 n2 through n3)`.
inline void put_nodes(std::string& out, const Schedule::Stage& ps, std::size_t index) {
    out += 's';
    put(out, static_cast<std::int64_t>(index));
    out += " =";
    for (std::size_t k = static_cast<std::size_t>(ps.inlined); k < ps.nodes.size(); ++k) {
        put_ref(out, " n", ps.nodes[k]);
    }
    if (ps.inlined > 0) {
        out += " inline(";
        for (std::size_t k = 0; k < static_cast<std::size_t>(ps.inlined); ++k) {
            put_ref(out, k > 0 ? " n" : "n", ps.nodes[k]);
        }
        put_ref(out, " through n", ps.through);
        out += ')';
    }
    out += '\n';
}

// One planned loop: `a2` (serial), `a3 tile 8`, `a0 unroll 4`, `a1 vector 8 x2` (lanes of eight in blocks of two).
inline void put_loop(std::string& out, const Schedule::Loop& l) {
    constexpr std::string_view kinds[] = {"", " vector", " unroll", " tile"};
    out += 'a';
    put(out, l.axis);
    out += kinds[static_cast<std::size_t>(l.kind)];
    if (l.kind != LoopKind::Serial) {
        out += ' ';
        put(out, l.step);
    } else {
        put_field(out, "step", l.step, 1);
    }
    if (l.unroll != 1) {
        out += " x";
        put(out, l.unroll);
    }
}

// A planned stage: its nodes; `  loops a2, a0 unroll 4 | a3` — its loops outermost first, `|` before the inner ones
// (`inner-from n` where that stands outside them); then, where it chose any, `  contract relaid 1 block 16 turned 1
// fetch parallel 1` — the multiply-add fused, operand 1 read from a copy in blocks of 16, turned around, the next
// block's stores fetched ahead, planned loop 1 split among workers — then `skewed`, its sections in lanes, each a
// sample behind the one before, `peeled`, a run's first sample run apart, and last `kept: freq gain q`, run only when
// one of the inputs of `g` it names changed.
inline void put_planned(std::string& out, const Schedule::Stage& ps, std::size_t index, const Graph& g) {
    put_nodes(out, ps, index);
    out += "  loops";
    const auto n = static_cast<std::int32_t>(ps.loops.size());
    for (std::int32_t j = 0; j <= n; ++j) {
        out += j == ps.inner_from ? " |" : "";
        if (j < n) {
            out += j == 0 || j == ps.inner_from ? " " : ", ";
            put_loop(out, ps.loops[static_cast<std::size_t>(j)]);
        }
    }
    if (ps.inner_from < 0 || ps.inner_from > n) {
        out += " inner-from ";
        put(out, ps.inner_from);
    }
    out += '\n';
    std::string chose;
    put_flag(chose, "contract", ps.contract);
    put_field(chose, "relaid", ps.relaid, -1);
    put_field(chose, "block", ps.relaid_block, 0);
    put_field(chose, "turned", ps.turned, -1);
    put_flag(chose, "fetch", ps.fetches);
    put_field(chose, "parallel", ps.parallel, -1);
    put_field(chose, "state-depth", ps.state_depth, kMaxStateDepth);
    put_flag(chose, "skewed", ps.skewed);
    put_flag(chose, "peeled", ps.peeled);
    chose += ps.kept ? " kept:" : "";
    for (std::size_t i = 0; ps.kept && i < g.inputs.size(); ++i) {
        if ((ps.watched[i / 64] >> (i % 64) & 1u) != 0) {
            chose += ' ';
            chose += g.inputs[i].name.view();
        }
    }
    if (!chose.empty()) {
        out += ' ';
        out += chose;
        out += '\n';
    }
}

} // namespace detail

// A schedule of graph `g` as text: its stages, its bands (`band s0+2 a0 30 parallel`: two stages from s0 along axis 0,
// 30 rows a chunk, split among workers), what a split is given.
inline std::string print(const Schedule& s, const Graph& g) {
    std::string out;
    for (std::size_t k = 0; k < s.stages.size(); ++k) {
        detail::put_planned(out, s.stages[k], k, g);
    }
    for (const Schedule::Band& b : s.bands) {
        out += "band s";
        detail::put(out, b.first);
        out += '+';
        detail::put(out, b.count);
        out += " a";
        detail::put(out, b.axis);
        out += ' ';
        detail::put(out, b.step);
        detail::put_flag(out, "parallel", b.parallel);
        out += '\n';
    }
    if (s.parallel_work != 0 || s.line_bytes != 0) {
        out += "parallel-work";
        detail::put_ref(out, " ", s.parallel_work);
        detail::put_field(out, "line-bytes", s.line_bytes, 0);
        out += '\n';
    }
    return out;
}

// The whole program as text.
inline std::string print(const Program& p) {
    std::string out;
    for (std::size_t b = 0; b < p.buffers.size(); ++b) {
        detail::put_buffer(out, p.buffers[b], b);
    }
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        detail::put_stage(out, p, s);
    }
    for (std::size_t b = 0; b < p.bands.size(); ++b) {
        detail::put_band(out, p, b);
    }
    out += "outputs";
    for (const std::int32_t o : p.outputs) {
        out += " b";
        detail::put(out, o);
    }
    detail::put_field(out, "parallel-work", p.parallel_work, 0);
    detail::put_field(out, "line-bytes", p.line_bytes, 0);
    out += '\n';
    std::vector<std::size_t> depth(p.nodes.size(), 0);
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const std::int32_t parent = p.nodes[n].parent;
        depth[n] = parent >= 0 && static_cast<std::size_t>(parent) < n ? depth[static_cast<std::size_t>(parent)] + 1 : 0;
        detail::put_node(out, p, n, depth[n]);
    }
    return out;
}

} // namespace heinzel::loops
