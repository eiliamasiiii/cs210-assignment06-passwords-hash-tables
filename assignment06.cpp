#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

uint64_t demoHash(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h = (h ^ c) * 1099511628211ULL;
    }
    return h;
}

struct UserRecord {
    std::string salt;
    uint64_t passwordHash = 0;
};
enum class Method { Chain, Linear, Quadratic, Double };

class HashTable {
    struct Entry {
        std::string username;
        UserRecord record;
    };
    enum class State { Empty, Active, Deleted };
    struct Slot {
        Entry entry;
        State state = State::Empty;
    };
    Method method;
    std::size_t capacity_;
    std::size_t count = 0;
    std::size_t deleted = 0;
    std::size_t rebuilds = 0;
    // Vectors store buckets/slots; all table operations are implemented here.
    std::vector<std::vector<Entry>> buckets;
    std::vector<Slot> slots;

    static std::size_t nextPrime(std::size_t n) {
        if (n < 5) n = 5;
        for (;; ++n) {
            bool prime = true;
            for (std::size_t d = 2; d <= n / d; ++d) {
                if (n % d == 0) { prime = false; break; }
            }
            if (prime) return n;
        }
    }
    std::size_t position(std::size_t home, std::size_t step,
                         std::size_t attempt) const {
        if (method == Method::Linear)
            return (home + attempt) % capacity_;
        if (method == Method::Quadratic)
            return (home + attempt * attempt) % capacity_;
        return (home + attempt * step) % capacity_;
    }
    std::size_t locate(const std::string& key) const {
        const auto home = demoHash(key) % capacity_;
        const auto step = 1 + demoHash("jump/" + key) % (capacity_ - 1);
        for (std::size_t i = 0; i < capacity_; ++i) {
            const auto p = position(home, step, i);
            if (slots[p].state == State::Empty) break;
            if (slots[p].state == State::Active &&
                slots[p].entry.username == key) return p;
        }
        return capacity_;
    }
    void place(const Entry& entry) {
        const auto home = demoHash(entry.username) % capacity_;
        if (method == Method::Chain) {
            buckets[home].push_back(entry);
        } else {
            const auto step = 1 + demoHash("jump/" + entry.username)
                                  % (capacity_ - 1);
            bool placed = false;
            for (std::size_t i = 0; i < capacity_; ++i) {
                const auto p = position(home, step, i);
                if (slots[p].state != State::Active) {
                    if (slots[p].state == State::Deleted) --deleted;
                    slots[p] = Slot{entry, State::Active};
                    placed = true;
                    break;
                }
            }
            if (!placed) throw std::logic_error("No available probe slot");
        }
        ++count;
    }
    void rehash(std::size_t requested) {
        auto oldBuckets = std::move(buckets);
        auto oldSlots = std::move(slots);
        capacity_ = nextPrime(requested);
        count = deleted = 0;
        if (method == Method::Chain) buckets.resize(capacity_);
        else slots.resize(capacity_);
        for (const auto& bucket : oldBuckets)
            for (const auto& entry : bucket) place(entry);
        for (const auto& slot : oldSlots)
            if (slot.state == State::Active) place(slot.entry);
        ++rebuilds;
    }
public:
    explicit HashTable(Method m, std::size_t initial = 11)
        : method(m), capacity_(nextPrime(initial)) {
        if (method == Method::Chain) buckets.resize(capacity_);
        else slots.resize(capacity_);
    }
    const UserRecord* find(const std::string& key) const {
        if (method == Method::Chain) {
            for (const auto& entry : buckets[demoHash(key) % capacity_])
                if (entry.username == key) return &entry.record;
            return nullptr;
        }
        const auto p = locate(key);
        return p == capacity_ ? nullptr : &slots[p].entry.record;
    }
    bool insert(const std::string& key, const UserRecord& record) {
        // Reject duplicates, so registration cannot replace a password.
        if (find(key)) return false;
        if (method == Method::Chain) {
            if (static_cast<double>(count + 1) / capacity_ > 0.75)
                rehash(capacity_ * 2);
        } else {
            // Prime capacity and <50% occupied guarantee a free square probe.
            // Counting tombstones also keeps unsuccessful searches short.
            if ((count + deleted + 1) * 2 >= capacity_)
                rehash((count + 1) * 2 >= capacity_
                       ? capacity_ * 2 : capacity_);
        }
        place(Entry{key, record});
        return true;
    }
    bool remove(const std::string& key) {
        if (method == Method::Chain) {
            auto& bucket = buckets[demoHash(key) % capacity_];
            for (std::size_t i = 0; i < bucket.size(); ++i) {
                if (bucket[i].username == key) {
                    bucket[i] = std::move(bucket.back());
                    bucket.pop_back();
                    --count;
                    return true;
                }
            }
            return false;
        }
        const auto p = locate(key);
        if (p == capacity_) return false;
        slots[p].entry = Entry{};
        slots[p].state = State::Deleted;
        --count;
        ++deleted;
        return true;
    }
    double loadFactor() const {
        return static_cast<double>(count) / capacity_;
    }
    std::size_t size() const { return count; }
    std::size_t capacity() const { return capacity_; }
    std::size_t rehashCount() const { return rebuilds; }
};

// Fixed example data makes the demonstration reproducible, not secure.
const std::string pepper = "classroom-pepper-06";
UserRecord makeRecord(const std::string& salt, const std::string& password) {
    return {salt, demoHash(salt + password + pepper)};
}
bool login(const HashTable& users, const std::string& name,
           const std::string& password) {
    const auto* user = users.find(name);
    return user && user->passwordHash ==
        demoHash(user->salt + password + pepper);
}
int checks = 0;
void check(bool passed, const std::string& label) {
    ++checks;
    std::cout << (passed ? "PASS: " : "FAIL: ") << label << '\n';
    if (!passed) throw std::runtime_error(label);
}

void testMethod(Method method, const std::string& label) {
    std::cout << "\n" << label << "\n";
    HashTable table(method);
    std::vector<std::string> names;
    for (int i = 0; names.size() < 4; ++i) {
        auto name = "member_" + std::to_string(i);
        if (demoHash(name) % 11 == 3) names.push_back(name);
    }
    std::cout << "Same starting bucket (3 of 11):";
    for (const auto& name : names) std::cout << ' ' << name;
    std::cout << '\n';
    check(table.find("absent") == nullptr && !table.remove("absent"),
          "empty table lookup and removal");
    bool inserted = true;
    for (const auto& name : names)
        inserted &= table.insert(name, makeRecord("s/" + name, "demo"));
    check(inserted && table.size() == 4, "four colliding keys inserted");
    check(!table.insert(names[0], makeRecord("other", "changed")) &&
          login(table, names[0], "demo"), "duplicate rejected; record kept");
    check(table.remove(names[1]) && !table.find(names[1]),
          "remove an entry in the collision sequence");
    check(login(table, names[2], "demo") && login(table, names[3], "demo"),
          "later colliding keys remain reachable after removal");
    check(table.insert(names[1], makeRecord("fresh", "new")) &&
          login(table, names[1], "new"), "reinsert removed username");
    const auto before = table.capacity();
    for (int i = 0; i < 80; ++i)
        table.insert("extra_" + std::to_string(i), makeRecord("extra", "pw"));
    bool preserved = true;
    for (int i = 0; i < 80; ++i)
        preserved &= login(table, "extra_" + std::to_string(i), "pw");
    for (std::size_t i = 0; i < names.size(); ++i)
        preserved &= login(table, names[i], i == 1 ? "new" : "demo");
    check(table.capacity() > before && preserved && table.size() == 84,
          "automatic growth preserves all 84 records");
    std::cout << "Capacity " << before << " -> " << table.capacity()
              << "; rehashes=" << table.rehashCount()
              << "; load=" << table.loadFactor() << '\n';
    for (int i = 0; i < 80; ++i)
        table.remove("extra_" + std::to_string(i));
    for (const auto& name : names) table.remove(name);
    check(table.size() == 0 && table.loadFactor() == 0 &&
          !table.find(names[0]), "remove every record");
    check(table.insert("restart", makeRecord("reset", "pw")) &&
          login(table, "restart", "pw"), "reuse table after emptying it");
}

int main() {
    std::cout << std::fixed << std::setprecision(3);
    testMethod(Method::Chain, "SEPARATE CHAINING");
    testMethod(Method::Linear, "LINEAR PROBING");
    testMethod(Method::Quadratic, "QUADRATIC PROBING");
    testMethod(Method::Double, "DOUBLE HASHING");
    std::cout << "\nLOGIN DEMO - chosen strategy: separate chaining\n";
    HashTable users(Method::Chain, 5);
    const std::vector<std::string> names =
        {"eilia", "maya", "noah", "zoe", "leo"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto oldCapacity = users.capacity();
        const auto salt = "demo-salt-" + std::to_string(i + 10);
        check(users.insert(names[i], makeRecord(salt, "practice" +
              std::to_string(i))), "register " + names[i]);
        if (users.capacity() != oldCapacity)
            std::cout << "REHASH: " << oldCapacity << " -> "
                      << users.capacity() << " buckets\n";
    }
    for (const auto& name : names) {
        const auto* record = users.find(name);
        std::cout << name << ": salt=" << record->salt
                  << ", hash=" << record->passwordHash << '\n';
    }
    check(users.size() == 5 && users.rehashCount() > 0,
          "five users stored and automatic rehash demonstrated");
    check(login(users, "eilia", "practice0"), "correct password accepted");
    check(!login(users, "eilia", "wrong"), "wrong password rejected");
    check(!login(users, "unknown", "practice0"), "unknown user rejected");
    check(users.remove("maya"), "remove maya");
    check(!login(users, "maya", "practice1"), "removed user rejected");
    check(!users.remove("maya"), "second removal returns false");
    check(login(users, "leo", "practice4"), "remaining user can log in");
    std::cout << "Final users=" << users.size()
              << ", load=" << users.loadFactor() << '\n';
    std::cout << "\nAll " << checks << " checks passed.\n";
}
