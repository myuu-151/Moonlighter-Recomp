#pragma once
// A recording's frame lines, kept as text and parsed when a replay reads them (tests/enemy_replay.h,
// tests/translated/enemy_translated_test.cpp). Parsed all at once, a recording takes tens of MB (a
// json::Value of 72 bytes per number on the GameCube): more than the GameCube has, and MLGC's test
// builds (TTEST=1) run these replays there. frames[i] parses line i; what a replay reads stays
// parsed: the first frame always, and the three read last (it reads each frame in order, and the
// one before it). The values are the same as a whole parse's.

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "util/Json.h"

class ReplayFrames {
public:
    void push_back(std::string line) { lines_.push_back(std::move(line)); }
    size_t size() const { return lines_.size(); }
    bool empty() const { return lines_.empty(); }

    const json::Value& operator[](size_t i)
    {
        if (i == 0) {
            if (!firstParsed_) {
                first_ = json::Parse(lines_.at(0));
                firstParsed_ = true;
            }
            return first_;
        }
        for (Slot& s : recent_)
            if (s.index == i) return s.value;
        Slot& s = recent_[next_];
        next_ = (next_ + 1) % recent_.size();
        s.value = json::Parse(lines_.at(i));
        s.index = i;
        return s.value;
    }

    // for (const json::Value& f : frames): each parsed in turn.
    class iterator {
    public:
        iterator(ReplayFrames* frames, size_t i) : frames_(frames), i_(i) {}
        const json::Value& operator*() const { return (*frames_)[i_]; }
        iterator& operator++()
        {
            ++i_;
            return *this;
        }
        bool operator!=(const iterator& other) const { return i_ != other.i_; }

    private:
        ReplayFrames* frames_;
        size_t i_;
    };
    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, lines_.size()); }

private:
    struct Slot {
        size_t index = static_cast<size_t>(-1);
        json::Value value;
    };
    std::vector<std::string> lines_;
    json::Value first_;
    bool firstParsed_ = false;
    std::array<Slot, 3> recent_;
    size_t next_ = 0;
};
