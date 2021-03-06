#ifndef _SEARCH_H
#define _SEARCH_H

#include "base.h"

namespace cube {
    template<u64 _size>
    struct array_u2 {
        std::array<std::atomic_uint64_t, (_size + 31) / 32> a;

        constexpr u64 size() const {
            return _size;
        }

        u64 get(u64 i) const {
            u64 j = i / 32;
            u64 k = i % 32 * 2;
            return (a[j] >> k) & u64(3);
        }

        bool compare_and_set(u64 i, u64 old_x, u64 new_x) {
            u64 j = i / 32;
            u64 k = i % 32 * 2;
            u64 old_y = a[j];
            while (((old_y >> k) & u64(3)) == old_x) {
                u64 new_y = (old_y & ~(u64(3) << k)) | (new_x << k);
                if (a[j].compare_exchange_strong(old_y, new_y)) {
                    return true;
                }
            }
            return false;
        }

        void fill(u64 x) {
            u64 y = x;
            for (u64 i = 1; i < 6; i++) {
                y = y | (y << (u64(1) << i));
            }
            for (u64 i = 0; i < a.size(); i++) {
                a[i] = y;
            }
        }
    };

    std::vector<u64> _split(u64 m, u64 n) {
        u64 mm = (m + 31) / 32;
        u64 div = mm / n;
        u64 mod = mm % n;
        std::vector<u64> sp{0};
        for (u64 i = 0; i < n; i++) {
            sp.push_back(std::min(sp[i] + (div + (i < mod ? 1 : 0)) * 32, m));
        }
        return sp;
    }

    template<u64 _size, typename C>
    u64 _set_multi(array_u2<_size> &distance_m3, const C &i_s, u64 old_x, u64 new_x) {
        auto it = i_s.begin();
        if (not distance_m3.compare_and_set(*it, old_x, new_x)) {
            return 0;
        }
        ++it;
        auto last = i_s.end();
        while (it != last) {
            assert(distance_m3.compare_and_set(*it, old_x, new_x));
            ++it;
        }
        return i_s.size();
    }

    template<typename _solver>
    std::tuple<u64, u64> _forward(const _solver &s, array_u2<_solver::n_state> &distance_m3,
                                  u64 start, u64 end, u64 prev_depth_m3, u64 depth_m3) {
        constexpr std::array<u64, 3> modify{
                0xffffffffffffffff,
                0xaaaaaaaaaaaaaaaa,
                0x5555555555555555
        };
        constexpr u64 mask = 0x5555555555555555;
        u64 count_distinct = 0;
        u64 count = 0;
        for (u64 i = start; i < end; i += 32) {
            u64 x = distance_m3.a[i / 32] ^modify[prev_depth_m3];
            if (((x >> u64(1)) & x & mask) == 0) {
                continue;
            }
            u64 j_end = std::min(i + 32, end);
            for (u64 j = i; j < j_end; j++) {
                if (distance_m3.get(j) == prev_depth_m3) {
                    typename _solver::t_state a = s.int_to_state(j);
                    for (typename _solver::t_state b: s.adj(a)) {
                        u64 k = s.state_to_int(b);
                        if (distance_m3.get(k) == 3) {
                            u64 c = _set_multi<_solver::n_state>(distance_m3, s.alt(b, k), 3, depth_m3);
                            if (c > 0) {
                                count_distinct++;
                                count += c;
                            }
                        }
                    }
                }
            }
        }
        return {count_distinct, count};
    }

    template<typename _solver>
    std::tuple<u64, u64> _backward(const _solver &s, array_u2<_solver::n_state> &distance_m3,
                                   u64 start, u64 end, u64 prev_depth_m3, u64 depth_m3) {
        constexpr u64 mask = 0x5555555555555555;
        u64 count_distinct = 0;
        u64 count = 0;
        for (u64 i = start; i < end; i += 32) {
            u64 x = distance_m3.a[i / 32];
            if (((x >> u64(1)) & x & mask) == 0) {
                continue;
            }
            u64 j_end = std::min(i + 32, end);
            for (u64 j = i; j < j_end; j++) {
                if (distance_m3.get(j) == 3) {
                    typename _solver::t_state a = s.int_to_state(j);
                    for (typename _solver::t_state b: s.adj(a)) {
                        u64 k = s.state_to_int(b);
                        if (distance_m3.get(k) == prev_depth_m3) {
                            u64 c = _set_multi<_solver::n_state>(distance_m3, s.alt(a, j), 3, depth_m3);
                            if (c > 0) {
                                count_distinct++;
                                count += c;
                            }
                        }
                    }
                }
            }
        }
        return {count_distinct, count};
    }

    template<typename _solver>
    void bfs(const _solver &s, array_u2<_solver::n_state> &distance_m3, u64 n_thread = 1) {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<u64> sp = _split(_solver::n_state, n_thread);
        distance_m3.fill(3);
        u64 count_start;
        {
            typename _solver::t_state a_start = s.cube_to_state(_solver::t_cube::i());
            u64 i_start = s.state_to_int(a_start);
            count_start = _set_multi<_solver::n_state>(distance_m3, s.alt(a_start, i_start), 3, 0);
            assert(count_start > 0);
        }
        std::cout << "bfs: n_state=" << _solver::n_state << ", n_thread=" << n_thread << std::endl;
        std::cout << "bfs: depth=" << 0 << ", count_distinct=" << 1 << ", count=" << count_start << std::endl;
        u64 total_count_distinct = 1;
        u64 total_count = count_start;
        std::array<u64, 3> count_m3{count_start, 0, 0};
        for (u64 depth = 1; total_count != _solver::n_state; depth++) {
            auto t1 = std::chrono::steady_clock::now();
            u64 count_distinct = 0;
            u64 count = 0;
            std::vector<std::future<std::tuple<u64, u64>>> result{};
            if (count_m3[(depth - 1) % 3] <= _solver::n_state - total_count) {
                for (u64 i = 0; i < n_thread; i++) {
                    result.push_back(std::async(
                            std::launch::async,
                            &_forward<_solver>,
                            std::cref(s), std::ref(distance_m3), sp[i], sp[i + 1], (depth - 1) % 3, depth % 3));
                }
            } else {
                for (u64 i = 0; i < n_thread; i++) {
                    result.push_back(std::async(
                            std::launch::async,
                            &_backward<_solver>,
                            std::cref(s), std::ref(distance_m3), sp[i], sp[i + 1], (depth - 1) % 3, depth % 3));
                }
            }
            for (u64 i = 0; i < n_thread; i++) {
                auto[cd, c] = result[i].get();
                count_distinct += cd;
                count += c;
            }
            auto t2 = std::chrono::steady_clock::now();
            std::chrono::duration<double> d = t2 - t1;
            std::cout << "bfs: depth=" << depth << ", count_distinct=" << count_distinct << ", count=" << count
                      << ", time=" << d.count() << "s" << std::endl;
            total_count_distinct += count_distinct;
            total_count += count;
            count_m3[depth % 3] += count;
        }
        auto t3 = std::chrono::steady_clock::now();
        std::chrono::duration<double> d = t3 - t0;
        std::cout << "bfs: total_count_distinct=" << total_count_distinct << ", total_count=" << total_count
                  << ", total_time=" << d.count() << "s" << std::endl;
    }

    constexpr u64 computer_distance(u64 distance_m3, u64 distance_adj) {
        return distance_adj + (distance_m3 - distance_adj - 3) % 3 - 1;
    }

    template<typename _solver>
    struct get_distance {
        static std::tuple<u64, typename _solver::t_hint> call(const _solver &s, const typename _solver::t_state &a) {
            typename _solver::t_state b = a;
            u64 i = s.state_to_int(b);
            u64 depth = 0;
            while (not s.is_start(b)) {
                u64 target = (s.distance_m3->get(i) + 2) % 3;
                bool found = false;
                for (const typename _solver::t_state &c: s.adj(b)) {
                    u64 j = s.state_to_int(c);
                    if (s.distance_m3->get(j) == target) {
                        b = c;
                        i = j;
                        depth++;
                        found = true;
                        break;
                    }
                }
                assert (found);
            }
            return {depth, depth};
        }
    };

    template<typename _solver>
    struct get_distance_hint {
        static std::tuple<u64, typename _solver::t_hint> call(
                const _solver &s, const typename _solver::t_state &a, const typename _solver::t_hint &hint) {
            u64 d = computer_distance(s.distance_m3->get(s.state_to_int(a)), hint);
            return {d, d};
        }
    };

    template<u64 capacity>
    struct t_moves {
        u8 n;
        std::array<u8, capacity> a;
    };

    template<u64 capacity>
    struct random_moves {
        std::default_random_engine e;
        std::uniform_int_distribution<u8> d;

        random_moves(u64 n_base, u64 seed) : e(seed), d(0, n_base - 1) {
        }

        t_moves<capacity> operator()(u64 n) {
            assert(n <= capacity);
            t_moves<capacity> moves{u8(n), {}};
            for (u64 i = 0; i < n; i++) {
                moves.a[i] = d(e);
            }
            return moves;
        }
    };

    template<typename _solver, u64 capacity>
    typename _solver::t_cube moves_to_cube(const t_moves<capacity> &moves) {
        typename _solver::t_cube a = _solver::t_cube::i();
        u64 n = moves.n;
        for (u64 i = 0; i < n; i++) {
            a = a * _solver::base[moves.a[i]];
        }
        return a;
    }

    template<typename _solver, u64 capacity>
    std::string moves_to_string(const t_moves<capacity> &moves) {
        std::string s = "(";
        u64 n = moves.n;
        for (u64 i = 0; i < n; i++) {
            s += _solver::base_name[moves.a[i]];
            if (i < n - 1) {
                s += " ";
            }
        }
        s += ")";
        return s;
    }

    struct flag {
        static constexpr u64 none = 0;
        static constexpr u64 solution = 1;
        static constexpr u64 optimum = 2;
        static constexpr u64 end = 4;
    };

    template<typename _solver, u64 capacity>
    struct ida_star_node {
        typename _solver::t_state state;
        typename _solver::t_hint hint;
        t_moves<capacity> moves;
    };

    template<typename _solver, u64 capacity>
    struct get_sym_mask {
        static u64 call(
                const _solver &s, const typename _solver::t_cube &a, const ida_star_node<_solver, capacity> &b) {
            return u64(-1);
        }
    };

    template<typename _solver, u64 capacity>
    struct ida_star {
        typedef ida_star_node<_solver, capacity> node;

        const _solver &s;
        const typename _solver::t_cube a;
        const u64 max_n_moves;
        const u64 sym_mask_n_moves;
        u64 n_moves;
        u64 optimum_n_moves;
        bool end;
        node node_a;
        std::vector<node> stack;
        std::vector<u64> count;
        double layer_time;
        double total_time;
        bool verbose;

        ida_star(const _solver &_s, const typename _solver::t_cube &_a, u64 _max_n_moves, u64 _sym_mask_n_moves = 0) :
                s(_s), a(_a), max_n_moves(std::min(_max_n_moves, capacity)), sym_mask_n_moves(_sym_mask_n_moves) {
            typename _solver::t_state state_a = s.cube_to_state(_a);
            auto[dist_a, hint_a] = get_distance<_solver>::call(s, state_a);
            n_moves = std::min(dist_a, max_n_moves);
            optimum_n_moves = u64(-1);;
            end = false;
            node_a = node{
                    state_a,
                    hint_a,
                    t_moves<capacity>{u8(0), {}}
            };
            stack.push_back(node_a);
            count = std::vector<u64>(n_moves + 1, 0);
            layer_time = 0.0;
            total_time = 0.0;
            verbose = true;
        }

        std::tuple<u64, t_moves<capacity>> operator()() {
            auto t0 = std::chrono::steady_clock::now();
            while (true) {
                if (end) {
                    if (verbose) {
                        std::cout << "ida_star: end" << std::endl;
                    }
                    return {flag::end, t_moves<capacity>{u8(0), {}}};
                }
                if (stack.empty()) {
                    auto t1 = std::chrono::steady_clock::now();
                    std::chrono::duration<double> d = t1 - t0;
                    t0 = t1;
                    layer_time += d.count();
                    total_time += d.count();
                    if (verbose) {
                        std::cout << "ida_star: complete, n_moves=" << n_moves
                                  << ", total_count=" << vector_sum<u64>(count)
                                  << ", layer_time=" << layer_time
                                  << "s, total_time=" << total_time << "s" << std::endl;
                        std::cout << "count=" << vector_to_string<u64>(count) << std::endl;
                    }
                    if (n_moves == max_n_moves) {
                        end = true;
                    } else {
                        n_moves++;
                        stack.push_back(node_a);
                        count = std::vector<u64>(n_moves + 1, 0);
                        layer_time = 0.0;
                        if (optimum_n_moves != u64(-1)) {
                            return {flag::none, t_moves<capacity>{u8(0), {}}};
                        }
                    }
                } else {
                    node b = stack.back();
                    stack.pop_back();
                    count[b.moves.n]++;
                    if (b.moves.n == n_moves) {
                        if (s.is_start(b.state)) {
                            auto t1 = std::chrono::steady_clock::now();
                            std::chrono::duration<double> d = t1 - t0;
                            t0 = t1;
                            layer_time += d.count();
                            total_time += d.count();
                            if (verbose) {
                                std::cout << "ida_star: found, n_moves=" << n_moves
                                          << ", total_count=" << vector_sum<u64>(count)
                                          << ", layer_time=" << layer_time
                                          << "s, total_time=" << total_time << "s" << std::endl;
                                std::cout << "count=" << vector_to_string<u64>(count) << std::endl;
                            }
                            if (optimum_n_moves == u64(-1)) {
                                optimum_n_moves = b.moves.n;
                            }
                            u64 f = b.moves.n == optimum_n_moves ? flag::solution | flag::optimum : flag::solution;
                            return {f, b.moves};
                        }
                    } else {
                        u64 mask = b.moves.n == 0 ? u64(-1) : _solver::base_mask[b.moves.a[b.moves.n - 1]];
                        if (b.moves.n < sym_mask_n_moves) {
                            mask = mask & get_sym_mask<_solver, capacity>::call(s, a, b);
                        }
                        std::array<typename _solver::t_state, _solver::n_base> adj_b = s.adj(b.state);
                        for (u64 i = _solver::n_base - 1; i < _solver::n_base; i--) {
                            if ((mask >> i) & u64(1)) {
                                typename _solver::t_state state_c = adj_b[i];
                                auto[dist_c, hint_c] = get_distance_hint<_solver>::call(s, state_c, b.hint);
                                if (b.moves.n + 1 + dist_c <= n_moves) {
                                    node c{
                                            state_c,
                                            hint_c,
                                            t_moves<capacity>{u8(b.moves.n + 1), b.moves.a}
                                    };
                                    c.moves.a[b.moves.n] = i;
                                    stack.push_back(c);
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    template<typename _solver0, typename _solver1, u64 capacity>
    struct combine_search {
        typedef decltype(_solver0(1).template solve<capacity>(_solver0::t_cube::i())) t_iter0;
        typedef decltype(_solver1(1).template solve<capacity>(_solver1::t_cube::i())) t_iter1;

        static constexpr t_moves<capacity>
        combine_moves(const t_moves<capacity> &moves0, const t_moves<capacity> &moves1) {
            t_moves<capacity> moves{
                    u8(moves0.n + moves1.n),
                    moves0.a
            };
            for (u64 i = 0; i < moves0.n; i++) {
                moves.a[i] = u8(_solver0::base_index[moves0.a[i]]);
            }
            for (u64 i = 0; i < moves1.n; i++) {
                moves.a[moves0.n + i] = u8(_solver1::base_index[moves1.a[i]]);
            }
            return moves;
        }

        const _solver0 &s0;
        const _solver1 &s1;
        const typename _solver0::t_cube a;
        const u64 max_n_moves;
        u64 last_n_moves;
        u64 optimum_n_moves;
        bool end;
        t_iter0 it0;
        u64 count;
        double total_time;
        bool verbose;

        combine_search(const _solver0 &_s0, const _solver1 &_s1, const typename _solver0::t_cube &_a,
                       u64 _max_n_moves) :
                s0(_s0), s1(_s1), a(_a), max_n_moves(std::min(_max_n_moves, capacity)),
                it0(_s0.template solve<capacity>(_a, max_n_moves)) {
            it0.verbose = false;
            last_n_moves = u64(-1);
            optimum_n_moves = u64(-1);
            end = false;
            count = 0;
            total_time = 0.0;
            verbose = true;
        }

        std::tuple<u64, t_moves<capacity>> operator()() {
            auto t0 = std::chrono::steady_clock::now();
            while (true) {
                if (end) {
                    if (verbose) {
                        std::cout << "combine_search: end" << std::endl;
                    }
                    return {flag::end, t_moves<capacity>{u8(0), {}}};
                }
                auto[f0, moves0] = it0();
                if (f0 & flag::solution) {
                    count++;
                    typename _solver0::t_cube b = a * moves_to_cube<_solver0>(moves0);
                    t_iter1 it1 = s1.template solve<capacity>(b, std::min(max_n_moves, last_n_moves) - moves0.n);
                    it1.verbose = false;
                    auto[f1, moves1] = it1();
                    while (not(f1 & flag::optimum) and not(f1 & flag::end)) {
                        std::tie(f1, moves1) = it1();
                    }
                    if ((f1 & flag::optimum) and (moves1.n == 0 or last_n_moves > moves0.n + moves1.n)) {
                        auto t1 = std::chrono::steady_clock::now();
                        std::chrono::duration<double> d = t1 - t0;
                        t0 = t1;
                        total_time += d.count();
                        if (verbose) {
                            std::cout << "combine_search: found, n_moves=(" << u64(moves0.n) << " " << u64(moves1.n)
                                      << "), count=" << count
                                      << ", total_time=" << total_time << "s" << std::endl;
                        }
                        if (moves1.n == 0) {
                            last_n_moves = moves0.n;
                            optimum_n_moves = moves0.n;
                            end = true;
                        } else {
                            last_n_moves = moves0.n + moves1.n;
                        }
                        u64 f = last_n_moves == optimum_n_moves ? flag::solution | flag::optimum : flag::solution;
                        return {f, combine_moves(moves0, moves1)};
                    }
                } else if (f0 & flag::end) {
                    auto t1 = std::chrono::steady_clock::now();
                    std::chrono::duration<double> d = t1 - t0;
                    t0 = t1;
                    total_time += d.count();
                    if (verbose) {
                        std::cout << "combine_search: complete, count=" << count
                                  << ", total_time=" << total_time << "s" << std::endl;
                    }
                    end = true;
                }
            }
        }
    };

    template<typename _solver0, typename _solver1>
    struct combine_solver {
        typedef typename _solver0::t_cube t_cube;

        static constexpr u64 n_super_base = _solver0::n_super_base;
        static constexpr u64 n_base = _solver0::n_super_base;

        static constexpr std::array<t_cube, n_super_base> super_base = _solver0::super_base;
        static constexpr std::array<const char *, n_super_base> super_base_name = _solver0::super_base_name;
        static constexpr std::array<u64, n_base> base_index = []() -> std::array<u64, n_base> {
            std::array<u64, n_base> _base_index{};
            for (u64 i = 0; i < n_base; i++) {
                _base_index[i] = i;
            }
            return _base_index;
        }();
        static constexpr std::array<t_cube, n_base> base = array_sub<t_cube, n_super_base, n_base>(
                super_base, base_index);
        static constexpr std::array<const char *, n_base> base_name = array_sub<const char *, n_super_base, n_base>(
                super_base_name, base_index);

        u64 n_thread;
        _solver0 s0;
        _solver1 s1;

        explicit combine_solver(u64 _n_thread) : n_thread(_n_thread), s0(_n_thread), s1(_n_thread) {
        }

        template<u64 capacity>
        combine_search<_solver0, _solver1, capacity> solve(const t_cube &a, u64 max_n_moves = capacity) const {
            return combine_search<_solver0, _solver1, capacity>(s0, s1, a, max_n_moves);
        }
    };

    template<typename _solver, u64 capacity>
    struct thread_dfs {
        static constexpr char name[] = "thread";

        typedef _solver solver;

        typedef ida_star_node<_solver, capacity> node;

        static void dfs_one(
                const _solver &s, const node &a, u64 n_moves,
                u64 &count, std::tuple<u64, t_moves<capacity>> &result, volatile bool &stop) {
            std::array<node, _solver::n_base * capacity> stack;
            stack[0] = a;
            u64 stack_size = 1;
            while (not stop and stack_size > 0) {
                stack_size--;
                node b = stack[stack_size];
                count++;
                if (b.moves.n == n_moves) {
                    if (s.is_start(b.state)) {
                        result = {flag::solution | flag::optimum, b.moves};
                        stop = true;
                        break;
                    }
                } else {
                    u64 mask = b.moves.n == 0 ? u64(-1) : _solver::base_mask[b.moves.a[b.moves.n - 1]];
                    std::array<typename _solver::t_state, _solver::n_base> adj_b = s.adj(b.state);
                    for (u64 i = _solver::n_base - 1; i < _solver::n_base; i--) {
                        if ((mask >> i) & u64(1)) {
                            typename _solver::t_state state_c = adj_b[i];
                            auto[dist_c, hint_c] = get_distance_hint<_solver>::call(s, state_c, b.hint);
                            if (b.moves.n + 1 + dist_c <= n_moves) {
                                node c{
                                        state_c,
                                        hint_c,
                                        t_moves<capacity>{u8(b.moves.n + 1), b.moves.a}
                                };
                                c.moves.a[b.moves.n] = i;
                                stack[stack_size] = c;
                                stack_size++;
                            }
                        }
                    }
                }
            }
        }

        static void dfs_multi(
                const _solver &s, const std::vector<node> &nodes, u64 n_moves,
                const std::vector<u64> &tasks, const std::vector<u64> &split, std::vector<u64> &count,
                std::tuple<u64, t_moves<capacity>> &result, volatile bool &stop, u64 thread_id) {
            u64 start = split[thread_id];
            u64 end = split[thread_id + 1];
            for (u64 i = start; not stop and i < end; i++) {
                u64 j = tasks[i];
                dfs_one(s, nodes[j], n_moves, count[j], result, stop);
                u64 f = std::get<0>(result);
                if (f & flag::solution) {
                    break;
                }
            }
        }

        static void dfs_all(
                const _solver &s, const std::vector<node> &nodes, u64 n_thread, u64 n_moves,
                const std::vector<u64> &tasks, const std::vector<u64> &split, std::vector<u64> &count,
                std::vector<std::tuple<u64, t_moves<capacity>>> &result, volatile bool &stop) {
            std::vector<std::future<void>> future{};
            for (u64 i = 0; i < n_thread; i++) {
                future.push_back(std::async(
                        std::launch::async,
                        &thread_dfs<_solver, capacity>::dfs_multi,
                        std::cref(s), std::cref(nodes), n_moves,
                        std::cref(tasks), std::cref(split), std::ref(count),
                        std::ref(result[i]), std::ref(stop), i));
            }
            for (u64 i = 0; i < n_thread; i++) {
                future[i].get();
            }
        }

        const _solver &s;
        const std::vector<node> &nodes;
        const u64 n_thread;

        thread_dfs(const _solver &_s, const std::vector<node> &_nodes, u64 _n_thread) :
                s(_s), nodes(_nodes), n_thread(_n_thread) {
        }

        std::tuple<u64, t_moves<capacity>> run(
                u64 n_moves,
                const std::vector<u64> &tasks, const std::vector<u64> &split, std::vector<u64> &count) const {
            u64 n_nodes = nodes.size();
            for (u64 i = 0; i < n_nodes; i++) {
                count[i] = 0;
            }
            std::vector<std::tuple<u64, t_moves<capacity>>> result(
                    n_thread, {flag::none, t_moves<capacity>{u8(0), {}}});
            volatile bool stop = false;
            dfs_all(s, nodes, n_thread, n_moves, tasks, split, count, result, stop);
            for (u64 i = 0; i < n_thread; i++) {
                u64 f = std::get<0>(result[i]);
                if (f & flag::solution) {
                    return result[i];
                }
            }
            return {flag::none, t_moves<capacity>{u8(0), {}}};
        }
    };

    struct simple_schedule {
        static constexpr char name[] = "simple";

        static void call(
                u64 n_thread, const std::vector<bool> &required,
                std::vector<u64> &tasks, std::vector<u64> &split, const std::vector<u64> &count) {
            u64 n_nodes = required.size();
            u64 k = 0;
            for (u64 i = 0; i < n_thread; i++) {
                split[i] = k;
                for (u64 j = i; j < n_nodes; j += n_thread) {
                    if (required[j]) {
                        tasks[k] = j;
                        k++;
                    }
                }
            }
            split[n_thread] = k;
        }
    };

    struct linear_schedule {
        static constexpr char name[] = "linear";

        static void call(
                u64 n_thread, const std::vector<bool> &required,
                std::vector<u64> &tasks, std::vector<u64> &split, const std::vector<u64> &count) {
            u64 n_nodes = required.size();

            u64 n_tasks = 0;
            for (u64 j = 0; j < n_nodes; j++) {
                if (required[j]) {
                    tasks[n_tasks] = j;
                    n_tasks++;
                }
            }

            u64 total_count = 0;
            std::vector<u64> accumulate(n_tasks, 0);
            {
                u64 a = 0;
                u64 b = 0;
                for (u64 k = 0; k < n_tasks; k++) {
                    u64 c = count[tasks[k]] + 1;
                    total_count += c;
                    a = a + b + c;
                    b = c;
                    accumulate[k] = a;
                }
            }
            assert(total_count * 2 * n_thread / n_thread == total_count * 2);

            u64 k = 0;
            for (u64 i = 0; i < n_thread; i++) {
                while (accumulate[k] * n_thread < i * total_count * 2) {
                    k++;
                }
                split[i] = k;
            }
            split[n_thread] = n_tasks;
        }
    };

    struct best_schedule {
        static constexpr char name[] = "best";

        static void call(
                u64 n_thread, const std::vector<bool> &required,
                std::vector<u64> &tasks, std::vector<u64> &split, const std::vector<u64> &count) {
            u64 n_nodes = required.size();

            u64 n_tasks = 0;
            for (u64 j = 0; j < n_nodes; j++) {
                if (required[j]) {
                    tasks[n_tasks] = j;
                    n_tasks++;
                }
            }
            std::stable_sort(tasks.begin(), tasks.begin() + n_tasks, [&count](u64 i, u64 j) -> bool {
                return count[i] > count[j];
            });

            std::fill(split.begin(), split.end(), 0);

            std::vector<u64> heap(n_thread, 0);
            std::iota(heap.begin(), heap.end(), 0);
            std::vector<u64> thread_count(n_thread, 0);
            auto down = [&thread_count](u64 i, u64 j) -> bool {
                if (thread_count[i] > thread_count[j]) {
                    return true;
                } else if (thread_count[i] == thread_count[j]) {
                    return i > j;
                } else {
                    return false;
                }
            };

            std::vector<u64> assignment(n_nodes, u64(-1));
            for (u64 k = 0; k < n_tasks; k++) {
                std::pop_heap(heap.begin(), heap.end(), down);
                u64 i = heap[n_thread - 1];
                u64 j = tasks[k];
                assignment[j] = i;
                split[i + 1]++;
                thread_count[i] += count[j] + 1;
                std::push_heap(heap.begin(), heap.end(), down);
            }

            std::stable_sort(tasks.begin(), tasks.begin() + n_tasks, [&assignment](u64 i, u64 j) -> bool {
                if (assignment[i] < assignment[j]) {
                    return true;
                } else if (assignment[i] == assignment[j]) {
                    return i < j;
                } else {
                    return false;
                }
            });

            for (u64 i = 0; i < n_thread; i++) {
                split[i + 1] += split[i];
            }
        }
    };

    double _efficiency(
            u64 n_thread, u64 addition,
            const std::vector<u64> &tasks, const std::vector<u64> &split, const std::vector<u64> &count) {
        u64 total_count = 0;
        u64 max_thread_count = 0;
        for (u64 i = 0; i < n_thread; i++) {
            u64 start = split[i];
            u64 end = split[i + 1];
            u64 thread_count = 0;
            for (u64 k = start; k < end; k++) {
                thread_count += count[tasks[k]] + addition;
            }
            total_count = total_count + thread_count;
            max_thread_count = std::max(max_thread_count, thread_count);
        }
        return double(total_count) / double(max_thread_count * n_thread);
    }

    template<typename _solver, u64 capacity, typename parallel_dfs, typename schedule>
    struct parallel_ida_star {
        typedef ida_star_node<_solver, capacity> node;

        static std::tuple<u64, t_moves<capacity>, std::vector<node>, std::vector<u8>> bfs(
                const _solver &s, const typename _solver::t_cube &a, u64 max_n_moves, u64 bfs_count) {
            std::cout << "parallel_ida_star.bfs: bfs_count=" << bfs_count << std::endl;
            std::vector<node> nodes{};
            std::vector<u8> dists{};

            {
                typename _solver::t_state state_a = s.cube_to_state(a);
                auto[dist_a, hint_a] = get_distance<_solver>::call(s, state_a);
                if (dist_a <= max_n_moves) {
                    node node_a{
                            state_a,
                            hint_a,
                            t_moves<capacity>{u8(0), {}}
                    };
                    if (dist_a == 0 and s.is_start(state_a)) {
                        std::cout << "parallel_ida_star.bfs: found, n_moves=" << 0
                                  << ", count=" << nodes.size() << std::endl;
                        return {flag::solution | flag::optimum, node_a.moves, {}, {}};
                    }
                    nodes.push_back(node_a);
                    dists.push_back(u8(dist_a));
                }
                std::cout << "parallel_ida_star.bfs: complete, n_moves=" << 0
                          << ", count=" << nodes.size() << std::endl;
                if (nodes.empty()) {
                    std::cout << "parallel_ida_star.bfs: end" << std::endl;
                    return {flag::end, t_moves<capacity>{u8(0), {}}, {}, {}};
                }
            }

            for (u64 n_moves = 1; n_moves <= max_n_moves and nodes.size() < bfs_count; n_moves++) {
                std::vector<node> next_nodes{};
                std::vector<u8> next_dists{};
                for (const node &b: nodes) {
                    u64 mask = (b.moves.n == 0 ? u64(-1) : _solver::base_mask[b.moves.a[b.moves.n - 1]])
                               & get_sym_mask<_solver, capacity>::call(s, a, b);
                    std::array<typename _solver::t_state, _solver::n_base> adj_b = s.adj(b.state);
                    for (u64 i = 0; i < _solver::n_base; i++) {
                        if ((mask >> i) & u64(1)) {
                            typename _solver::t_state state_c = adj_b[i];
                            auto[dist_c, hint_c] = get_distance_hint<_solver>::call(s, state_c, b.hint);
                            if (b.moves.n + 1 + dist_c <= max_n_moves) {
                                node c{
                                        state_c,
                                        hint_c,
                                        t_moves<capacity>{u8(b.moves.n + 1), b.moves.a}
                                };
                                c.moves.a[b.moves.n] = i;
                                if (dist_c == 0 and s.is_start(state_c)) {
                                    std::cout << "parallel_ida_star.bfs: found, n_moves=" << n_moves
                                              << ", count=" << nodes.size() << std::endl;
                                    return {flag::solution | flag::optimum, c.moves, {}, {}};
                                }
                                next_nodes.push_back(c);
                                next_dists.push_back(u8(dist_c));
                            }
                        }
                    }
                }
                nodes = std::move(next_nodes);
                dists = std::move(next_dists);
                std::cout << "parallel_ida_star.bfs: complete, n_moves=" << n_moves
                          << ", count=" << nodes.size() << std::endl;
                if (nodes.empty()) {
                    std::cout << "parallel_ida_star.bfs: end" << std::endl;
                    return {flag::end, t_moves<capacity>{u8(0), {}}, {}, {}};
                }
            }

            return {flag::none, t_moves<capacity>{u8(0), {}}, nodes, dists};
        }

        static std::tuple<u64, t_moves<capacity>> run(
                const _solver &s, const typename parallel_dfs::solver &p_s, const typename _solver::t_cube &a,
                u64 n_thread, u64 _max_n_moves, u64 bfs_count) {
            auto t0 = std::chrono::steady_clock::now();
            u64 max_n_moves = std::min(_max_n_moves, capacity);
            auto[f, moves, nodes, dists] = bfs(s, a, max_n_moves, bfs_count);
            if ((f & flag::solution) or (f & flag::end)) {
                return {f, moves};
            }

            std::cout << "parallel_ida_star: dfs=" << parallel_dfs::name
                      << ", schedule=" << schedule::name
                      << ", n_thread=" << n_thread << std::endl;
            parallel_dfs dfs(p_s, nodes, n_thread);
            u64 n_nodes = nodes.size();
            std::vector<bool> required(n_nodes, false);
            std::vector<u64> tasks(n_nodes, 0);
            std::vector<u64> split(n_thread + 1, 0);
            std::vector<u64> count(n_nodes, 0);
            u64 bfs_n_moves = nodes[0].moves.n;
            u64 min_dist = *std::min_element(dists.begin(), dists.end());
            for (u64 n_moves = bfs_n_moves + min_dist; n_moves <= max_n_moves; n_moves++) {
                auto t1 = std::chrono::steady_clock::now();
                for (u64 i = 0; i < n_nodes; i++) {
                    required[i] = (bfs_n_moves + dists[i] <= n_moves);
                }
                schedule::call(n_thread, required, tasks, split, count);
                auto[f, moves] = dfs.run(n_moves, tasks, split, count);
                auto t2 = std::chrono::steady_clock::now();
                std::chrono::duration<double> d21 = t2 - t1;
                std::chrono::duration<double> d20 = t2 - t0;
                if (f & flag::solution) {
                    std::cout << "parallel_ida_star: found, n_moves=" << n_moves
                              << ", count=" << vector_sum<u64>(count)
                              << ", efficiency=" << _efficiency(n_thread, 0, tasks, split, count)
                              << ", layer_time=" << d21.count()
                              << "s, total_time=" << d20.count() << "s" << std::endl;
                    return {f, moves};
                } else {
                    std::cout << "parallel_ida_star: complete, n_moves=" << n_moves
                              << ", count=" << vector_sum<u64>(count)
                              << ", efficiency=" << _efficiency(n_thread, 0, tasks, split, count)
                              << ", layer_time=" << d21.count()
                              << "s, total_time=" << d20.count() << "s" << std::endl;
                }
            }

            std::cout << "parallel_ida_star: end" << std::endl;
            return {flag::end, t_moves<capacity>{u8(0), {}}};
        }
    };
}

#endif
