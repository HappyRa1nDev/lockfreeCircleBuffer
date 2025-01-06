#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

template<typename t_value>
class ring_buffer {
public:
    explicit ring_buffer(size_t _capacity)
        : capacity_(_capacity), storage_(_capacity) {
        assert(_capacity != 0);
    }
    bool push(t_value _value) {
        size_t curr_tail = tail_.load(std::memory_order_relaxed);
        size_t curr_head = head_.load(std::memory_order_acquire);
        size_t next_tail = get_next(curr_tail);
        if (next_tail == curr_head) {
            return false;
        }
        storage_[curr_tail] = std::move(_value);
        tail_.store(next_tail, std::memory_order_release);

        return true;
    }

    bool pop(t_value& _value) {
        size_t curr_head = head_.load(std::memory_order_relaxed);
        size_t curr_tail = tail_.load(std::memory_order_acquire);

        if (curr_head == curr_tail) {
            
            return false;
        }
        _value = std::move(storage_[curr_head]);
        head_.store(get_next(curr_head), std::memory_order_release);

        return true;
    }

private:
    inline size_t get_next(size_t _slot) const {
        return ++_slot == capacity_ ? 0 : _slot;//return (_slot + 1) % storage_.size();
    }

    const size_t capacity_;
    std::vector<t_value> storage_;

    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> head_ = { 0 };
    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> tail_ = { 0 };
};

#define M_TO_STRING_WRAPPER(x) #x
#define M_TO_STRING(x) M_TO_STRING_WRAPPER(x)
#define M_SOURCE __FILE__ ":" M_TO_STRING(__LINE__)

class stopwatch {
    using clock_type = std::chrono::steady_clock;

public:
    stopwatch() {
        start_ = clock_type::now();
    }

    template<typename t_duration>
    t_duration elapsed_duration() const {
        using namespace std::chrono;

        auto delta = clock_type::now() - start_;
        return duration_cast<t_duration>(delta);
    }

private:
    clock_type::time_point start_;
};

class hash_calculator {
public:
    template<typename t_value>
    void set(const t_value& _value) {
        digest_ = std::hash<t_value>()(_value) ^ (digest_ << 1);
    }

    size_t value() const {
        return digest_;
    }

private:
    size_t digest_ = 0;
};

void test() {
    constexpr size_t k_count = 10'000'000;
    constexpr size_t k_size = 1024;

    ring_buffer<int> buffer(k_size);

    size_t producer_hash = 0;
    std::chrono::milliseconds producer_time;

    size_t consumer_hash = 0;
    std::chrono::milliseconds consumer_time;

    std::thread producer([&]() {
        hash_calculator hash;
        stopwatch watch;

        for (int i = 0; i < k_count; ++i) {
            hash.set(i);

            while (!buffer.push(i)) {
                std::this_thread::yield();
            }
        }

        producer_time = watch.elapsed_duration<std::chrono::milliseconds>();
        producer_hash = hash.value();
        });

    std::thread consumer([&]() {
        hash_calculator hash;
        stopwatch watch;

        for (int i = 0; i < k_count; ++i) {
            int value;

            while (!buffer.pop(value)) {
                std::this_thread::yield();
            }

            hash.set(value);
        }

        consumer_time = watch.elapsed_duration<std::chrono::milliseconds>();
        consumer_hash = hash.value();
        });

    producer.join();
    consumer.join();

    if (producer_hash != consumer_hash) {
        throw std::runtime_error(M_SOURCE ": workers hash must be equal");
    }

    std::cout << "producer_time: " << producer_time.count() << "ms; "
        << "consumer_time: " << consumer_time.count() << "ms"
        << std::endl;
}

int main() {
    while (true) {
        test();
    }

    return 0;
}
