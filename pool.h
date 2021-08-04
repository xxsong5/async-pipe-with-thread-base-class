#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>


template<typename T>
class AyncPool {
private:
   template<typename ANY> 
   static int traits_emplace_back_return_type(ANY);
public:
    using Type = T;

    AyncPool():m_open(true){}
    ~AyncPool(){this->close();}

    int push_back(T const& elem) {
        int counts = -1;
        if (m_open) {
            std::lock_guard<std::mutex> lck(m_poolmux);
            m_pool.push_back(elem);
            counts = m_pool.size();
        }
        m_condition.notify_one();
        return counts;
    }

    int push_back(T && elem) {
        int counts = -1;
        if (m_open) {
            std::lock_guard<std::mutex> lck(m_poolmux);
            m_pool.push_back(std::move(elem));
            counts = m_pool.size();
        }
        m_condition.notify_one();
        return counts;
    }

    template<typename... Params>
    auto 
    emplace_back(Params &&... params) -> decltype(traits_emplace_back_return_type(T(std::declval<Params>()...))) {
        int counts = -1;
        if(m_open) {
            std::lock_guard<std::mutex> lck(m_poolmux);
            m_pool.emplace_back(std::forward<Params>(params) ...);
            counts = m_pool.size();
        }
        m_condition.notify_one();
        return counts;
    }

    size_t size() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        return m_pool.size();
    }

    bool empty() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        return m_open ? m_pool.empty() : true;
    }

    T fetch() {
        std::unique_lock <std::mutex> lck(m_poolmux);
        size_t counts = m_pool.size();
        while (counts == 0 && m_open) {
           m_condition.wait(lck);
           counts = m_pool.size();
        }
        if (m_pool.size()) {
            T tmp = m_pool.front();
            m_pool.pop_front();
            return std::move(tmp);
        }
        return T();
    }

    T fetchNoWait() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        if (m_pool.size()) {
            T tmp = m_pool.front();
            m_pool.pop_front();
            return std::move(tmp);
        }
        return T();
    }

    bool fetch(T &elem) {
        std::unique_lock <std::mutex> lck(m_poolmux);
        size_t counts = m_pool.size();
        while (counts == 0 && m_open) {
           m_condition.wait(lck);
           counts = m_pool.size();
        }
        if (m_pool.size()) {
            elem = m_pool.front();
            m_pool.pop_front();
            return true;
        }
        return false;
    }

    bool fetchNoWait(T &elem) {
        std::lock_guard<std::mutex> lck(m_poolmux);
        if (m_pool.size()) {
            elem = m_pool.front();
            m_pool.pop_front();
            return true;
        }
        return false;
    }

    void close() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        m_open = false;
        m_condition.notify_all();
    }

    bool isOpened() {
        return m_open;
    }

private:
    volatile int                m_open;

    std::deque<T>               m_pool;
    std::mutex                  m_poolmux;
    std::condition_variable     m_condition;
};


template<typename T>
class AyncPool<std::shared_ptr<T>> {

private:
   template<typename ANY> 
   static int traits_emplace_back_return_type(ANY);

public:
    using Type = std::shared_ptr<T>;
    using ElemType = T;

    AyncPool(size_t max=10000):m_open(true),m_waiting_fetch(0),m_max(max){if(max==0){m_max=1;}}
    ~AyncPool(){this->close();}

    int push_back(Type const& elem) {
        int counts = -1;
        if (m_open) {
            std::unique_lock<std::mutex> lck(m_poolmux);
            while (m_pool.size() >= m_max) {
                m_e_condition.notify_all();
                m_f_condition.wait(lck);
            }

            m_pool.push_back(elem);
            counts = m_pool.size();
            m_e_condition.notify_one();
        }

        return counts;
    }

    int push_with_high_priority(Type const& elem) {
        int counts = -1;
        if (m_open) {
            std::lock_guard<std::mutex> lck(m_poolmux);
            m_pool.push_front(elem);
            counts = m_pool.size();
            m_e_condition.notify_one();
        }

        return counts;
    }

    int push_back(std::vector<Type> const& elems) {
        int counts = -1;
        if (m_open) {
            std::unique_lock<std::mutex> lck(m_poolmux);
            while (m_pool.size() >= m_max) {
                m_e_condition.notify_all();
                m_f_condition.wait(lck);
            }

            m_pool.insert(m_pool.end(), elems.begin(), elems.end());
            counts = m_pool.size();
            m_e_condition.notify_all();
        }

        return counts;
    }

    template<typename... Params>
    auto 
    emplace_back(Params &&... params) -> decltype(traits_emplace_back_return_type(ElemType(std::declval<Params>()...))) {
        int counts = -1;
        std::unique_lock<std::mutex> lck(m_poolmux);
        while (m_pool.size() >= m_max && m_open) {
            m_e_condition.notify_all();
            m_f_condition.wait(lck);
        }

        m_pool.push_back(std::make_shared<ElemType>(std::forward<Params>(params)...));
        counts = m_pool.size();
        m_e_condition.notify_one();

        return counts;
    }


    bool empty() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        return m_open ? m_pool.empty() : true;
    }

    size_t size() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        return m_pool.size();
    }

    Type fetch() {
        std::unique_lock <std::mutex> lck(m_poolmux);
        while (m_pool.size() == 0 && m_open) {
           m_f_condition.notify_all();
           ++m_waiting_fetch;
           m_e_condition.wait(lck);
           --m_waiting_fetch;
        }

        if (m_pool.size()) {
            Type tmp = m_pool.front();
            m_pool.pop_front();
            m_f_condition.notify_one();
            return tmp;
        }

        return Type();
    }

    Type fetchNoWait() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        if (m_pool.size()) {
            Type tmp = m_pool.front();
            m_pool.pop_front();
            m_f_condition.notify_one();
            return tmp;
        }
        return Type();
    }

    std::vector<Type> fetch(size_t counts) {

        std::unique_lock <std::mutex> lck(m_poolmux);
        while (m_pool.size() == 0 && m_open) {
           m_f_condition.notify_all();
           ++m_waiting_fetch;
           m_e_condition.wait(lck);
           --m_waiting_fetch;
        }

        if (m_pool.size() && counts) {
            size_t c = std::min(counts, m_pool.size());
            std::vector<Type>  vec(m_pool.begin(), m_pool.begin()+c);
            m_pool.erase(m_pool.begin(), m_pool.begin()+c);
            m_f_condition.notify_all();

            return vec;
        }

        return std::vector<Type>();
    }

    bool fetch(Type &elem) {
        std::unique_lock <std::mutex> lck(m_poolmux);
        while (m_pool.size() == 0 && m_open) {
           m_f_condition.notify_all();
           ++m_waiting_fetch;
           m_e_condition.wait(lck);
           --m_waiting_fetch;
        }

        if (m_pool.size()) {
            elem = m_pool.front();
            m_pool.pop_front();
            m_f_condition.notify_one();
            return true;
        }

        return false;
    }

    bool fetchNoWait(Type &elem) {
        std::lock_guard<std::mutex> lck(m_poolmux);
        if (m_pool.size()) {
            elem = m_pool.front();
            m_pool.pop_front();
            m_f_condition.notify_one();
            return true;
        }
        return false;
    }

    void close() {
        std::lock_guard<std::mutex> lck(m_poolmux);
        m_open = false;
        m_e_condition.notify_all();
        m_f_condition.notify_all();
    }

    int waitingFetchThreads() {
        return m_waiting_fetch;
    }

    bool isOpened() {
        return m_open;
    }


private:
    volatile int                m_open;
    volatile int                m_waiting_fetch;
    size_t                      m_max;

    std::deque<Type>            m_pool;
    std::mutex                  m_poolmux;
    std::condition_variable     m_f_condition;  //full  pool    wake up: get,  sleep: put.
    std::condition_variable     m_e_condition;  //empty pool    wake up: put,  sleep: get.
};
