// Sophomore's dream: compute floor(10^d * sum_{n=1}^{N} n^{-n}).

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gmpxx.h>

namespace {

unsigned long parse_positive(const char* text, const char* name) {
    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value == 0)
        throw std::invalid_argument(std::string(name) +
                                    " must be a positive integer");
    return value;
}

std::string format_duration(unsigned long long seconds) {
    const unsigned long long hours = seconds / 3600;
    const unsigned long long minutes = seconds / 60 % 60;
    const unsigned long long remaining_seconds = seconds % 60;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hours << ':'
           << std::setw(2) << minutes << ':' << std::setw(2)
           << remaining_seconds;
    return output.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: soph <terms> <digits>\n";
        return 1;
    }

    unsigned long term_count;
    unsigned long digits;
    try {
        term_count = parse_positive(argv[1], "terms");
        digits = parse_positive(argv[2], "digits");
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    unsigned int available_jobs = std::thread::hardware_concurrency();
    if (available_jobs == 0)
        available_jobs = 1;
    const std::size_t jobs = static_cast<std::size_t>(
        std::min<unsigned long>(available_jobs, term_count));

    mpz_class scale;
    mpz_ui_pow_ui(scale.get_mpz_t(), 10, digits);

    std::vector<unsigned long> order(term_count);
    std::iota(order.begin(), order.end(), 1UL);
    std::mt19937_64 random(std::random_device{}());
    std::shuffle(order.begin(), order.end(), random);

    std::vector<mpz_class> partials(jobs);
    std::atomic<unsigned long> next_offset{0};
    std::atomic<unsigned long> completed{0};
    std::mutex progress_mutex;
    std::condition_variable progress_changed;
    std::size_t reduction_level = 0;
    bool finished = false;
    const auto started = std::chrono::steady_clock::now();

    std::size_t depth = 0;
    for (std::size_t s = 1; s < jobs; s *= 2)
        ++depth;

    // --- Progress display (owns all stderr output) ---
    std::thread progress_thread([&] {
        std::size_t line_width = 0;
        std::size_t prev_level = 0;
        bool terms_finalized = false;

        const auto emit = [&](const std::string& text, bool newline) {
            std::cerr << '\r' << text;
            if (text.size() < line_width)
                std::cerr << std::string(line_width - text.size(), ' ');
            line_width = std::max(line_width, text.size());
            if (newline)
                std::cerr << '\n';
            std::cerr << std::flush;
        };

        const auto terms_line = [&] {
            const unsigned long count =
                completed.load(std::memory_order_relaxed);
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            std::ostringstream line;
            line << std::fixed << std::setprecision(1);
            line << "Computed " << count << "/" << term_count;
            if (term_count > 0)
                line << " (" << (100.0 * count / term_count) << "%)";
            line << " | elapsed "
                 << format_duration(
                        static_cast<unsigned long long>(elapsed + 0.5));
            if (count > 0 && elapsed > 0) {
                line << " | " << static_cast<unsigned long>(count / elapsed)
                     << " terms/s";
                const auto eta = static_cast<unsigned long long>(
                    elapsed * (term_count - count) / count + 0.5);
                line << " | ETA " << format_duration(eta);
            } else {
                line << " | ETA --:--:--";
            }
            return line.str();
        };

        const auto reduction_line = [&](std::size_t level) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            const std::size_t stride = std::size_t{1} << (level - 1);
            std::size_t pairs = 0;
            for (std::size_t i = 0; i + stride < jobs; i += stride * 2)
                ++pairs;
            std::ostringstream line;
            line << std::fixed << std::setprecision(1);
            line << "Reduction: level " << level << "/" << depth
                 << " (stride " << stride << ", " << pairs
                 << " additions) | " << elapsed << "s";
            return line.str();
        };

        const auto finalize_terms = [&] {
            if (terms_finalized)
                return;
            emit(terms_line(), true);
            terms_finalized = true;
        };

        const auto catch_up_reduction = [&](std::size_t up_to) {
            while (prev_level < up_to) {
                ++prev_level;
                emit(reduction_line(prev_level), prev_level == depth);
            }
        };

        for (;;) {
            std::size_t level;
            bool done;
            {
                std::lock_guard<std::mutex> lock(progress_mutex);
                level = reduction_level;
                done = finished;
            }

            if (level == 0) {
                emit(terms_line(), false);
            } else {
                finalize_terms();
                catch_up_reduction(level);
            }

            if (done)
                break;

            std::unique_lock<std::mutex> lock(progress_mutex);
            progress_changed.wait_for(
                lock, std::chrono::milliseconds(200),
                [&] { return finished || reduction_level != level; });
        }
    });

    // --- Phase 1: term evaluation ---
    std::vector<std::thread> workers;
    workers.reserve(jobs);
    for (std::size_t worker = 0; worker < jobs; ++worker) {
        workers.emplace_back([&, worker] {
            mpz_class power;
            mpz_class term;
            for (;;) {
                const unsigned long offset =
                    next_offset.fetch_add(1, std::memory_order_relaxed);
                if (offset >= term_count)
                    break;
                const unsigned long n = order[offset];
                mpz_ui_pow_ui(power.get_mpz_t(), n, n);
                mpz_tdiv_q(term.get_mpz_t(), scale.get_mpz_t(),
                           power.get_mpz_t());
                partials[worker] += term;
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();

    // --- Phase 2: reduction tree ---
    for (std::size_t stride = 1; stride < partials.size(); stride *= 2) {
        for (std::size_t i = 0; i + stride < partials.size();
             i += stride * 2)
            partials[i] += partials[i + stride];
        {
            std::lock_guard<std::mutex> lock(progress_mutex);
            ++reduction_level;
        }
        progress_changed.notify_one();
    }

    {
        std::lock_guard<std::mutex> lock(progress_mutex);
        finished = true;
    }
    progress_changed.notify_one();
    progress_thread.join();

    std::cout << partials.front() << "\n";
}
