//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * bidirectional_path.h
 *
 *  Created on: Nov 14, 2011
 *      Author: andrey
 */

#ifndef BIDIRECTIONAL_PATH_H_
#define BIDIRECTIONAL_PATH_H_

#include "../debruijn_graph.hpp"

using debruijn_graph::Graph;
using debruijn_graph::EdgeId;
using debruijn_graph::VertexId;

namespace path_extend {

class BidirectionalPath;

class PathListener {
public:
    virtual void FrontEdgeAdded(EdgeId e, BidirectionalPath * path, int gap = 0) = 0;
    virtual void BackEdgeAdded(EdgeId e, BidirectionalPath * path, int gap = 0) = 0;
    virtual void FrontEdgeRemoved(EdgeId e, BidirectionalPath * path) = 0;
    virtual void BackEdgeRemoved(EdgeId e, BidirectionalPath * path) = 0;
    virtual ~PathListener() {
    }
};

class BidirectionalPath : public PathListener {

public:
    BidirectionalPath(const Graph& g)
            : g_(g),
              data_(),
              conj_path_(NULL),
              cumulative_len_(),
              gap_len_(),
              listeners_(),
              id_(0),
              weight_(1.0),
              has_overlaped_begin_(false),
              has_overlaped_end_(false),
              overlap_(false) {
    }

    BidirectionalPath(const Graph& g, const std::vector<EdgeId>& path)
            : BidirectionalPath(g) {
        for (size_t i = 0; i < path.size(); ++i) {
            PushBack(path[i]);
        }
        RecountLengths();
    }

    BidirectionalPath(const Graph& g, EdgeId startingEdge)
            : BidirectionalPath(g) {
        PushBack(startingEdge);
    }

    BidirectionalPath(const BidirectionalPath& path)
            : g_(path.g_),
              data_(path.data_),
              conj_path_(NULL),
              cumulative_len_(path.cumulative_len_),
              gap_len_(path.gap_len_),
              listeners_(),
              id_(path.id_),
              weight_(path.weight_),
              has_overlaped_begin_(path.has_overlaped_begin_),
              has_overlaped_end_(path.has_overlaped_end_),
              overlap_(path.overlap_) {
    }

public:
    void Subscribe(PathListener * listener) {
        listeners_.push_back(listener);
    }

    void Unsubscribe(PathListener * listener) {
        listeners_.push_back(listener);
    }

    void SetConjPath(BidirectionalPath* path) {
        conj_path_ = path;
    }

    const BidirectionalPath* GetConjPath() const {
        return conj_path_;
    }

    BidirectionalPath* GetConjPath() {
        return conj_path_;
    }

    void SetWeight(float w) {
        weight_ = w;
    }

    double GetWeight() const {
        return weight_;
    }

    size_t Size() const {
        return data_.size();
    }

    const Graph& graph() const {
        return g_;
    }

    bool Empty() const {
        return data_.empty();
    }

    size_t Length() const {
        if (gap_len_.size() == 0 || cumulative_len_.size() == 0) {
            return 0;
        }
        return cumulative_len_[0] + gap_len_[0];
    }

    EdgeId operator[](size_t index) const {
        return data_[index];
    }

    EdgeId At(size_t index) const {
        return data_[index];
    }

    EdgeId ReverseAt(size_t index) const {
        return data_[data_.size() - index - 1];
    }

    size_t LengthAt(size_t index) const {
        return cumulative_len_[index];
    }

    int GapAt(size_t index) const {
        return gap_len_[index];
    }

    size_t GetId() const {
        return id_;
    }

    EdgeId Back() const {
        return data_.back();
    }

    EdgeId Front() const {
        return data_.front();
    }

    void PushBack(EdgeId e, int gap = 0) {
        data_.push_back(e);
        gap_len_.push_back(gap);
        IncreaseLengths(g_.length(e), gap);
        NotifyBackEdgeAdded(e, gap);
    }

    void PushBack(const BidirectionalPath& path) {
        for (size_t i = 0; i < path.Size(); ++i) {
            PushBack(path.At(i), path.GapAt(i));
        }
    }

    void PopBack() {
        if (data_.empty()) {
            return;
        }
        EdgeId e = data_.back();
        DecreaseLengths();
        gap_len_.pop_back();
        data_.pop_back();
        NotifyBackEdgeRemoved(e);
    }

    void PopBack(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            PopBack();
        }
    }

    void Clear() {
        while (!Empty()) {
            PopBack();
        }
    }

    void SetId(uint32_t uid) {
        id_ = uid;
    }

    virtual void FrontEdgeAdded(EdgeId, BidirectionalPath*, int) {
    }

    virtual void BackEdgeAdded(EdgeId e, BidirectionalPath*, int gap) {
        PushFront(g_.conjugate(e), gap);
    }

    virtual void FrontEdgeRemoved(EdgeId, BidirectionalPath*) {
    }

    virtual void BackEdgeRemoved(EdgeId, BidirectionalPath *) {
        PopFront();
    }

    int FindFirst(EdgeId e) const {
        for (size_t i = 0; i < Size(); ++i) {
            if (data_[i] == e) {
                return (int) i;
            }
        }
        return -1;
    }

    int FindLast(EdgeId e) const {
        for (int i = (int) Size() - 1; i >= 0; --i) {
            if (data_[i] == e) {
                return i;
            }
        }
        return -1;
    }

    bool Contains(EdgeId e) const {
        return FindFirst(e) != -1;
    }

    vector<size_t> FindAll(EdgeId e, size_t start = 0) const {
        vector<size_t> result;
        for (size_t i = start; i < Size(); ++i) {
            if (data_[i] == e) {
                result.push_back(i);
            }
        }
        return result;
    }

    bool CompareFrom(size_t from, const BidirectionalPath& sample) const {
        if (from + sample.Size() > Size()) {
            return false;
        }

        for (size_t i = 0; i < sample.Size(); ++i) {
            if (At(from + i) != sample[i]) {
                return false;
            }
        }
        return true;
    }

    size_t CommonEndSize(const BidirectionalPath& p) const {
        std::vector<size_t> begins = FindAll(p.At(0));
        for (size_t i = 0; i < begins.size(); ++i) {
            size_t it1 = begins[i];
            size_t it2 = 0;
            while (it2 < p.Size() and At(it1) == p.At(it2)) {
                it1++;
                it2++;
                if (it1 == Size()) {
                    return it2;
                }
            }
        }
        return 0;
    }

    size_t OverlapEndSize(const BidirectionalPath* path2) const {
        if (Size() == 0) {
            return 0;
        }
        int last1 = (int) Size() - 1;
        int max_over = 0;
        vector<size_t> begins2 = path2->FindAll(At(last1));
        for (size_t i = 0; i < begins2.size(); ++i) {
            int begin2 = (int) begins2[i];
            int cur1 = last1;
            while (begin2 > 0 && cur1 > 0 && path2->At(begin2 - 1) == At(cur1 - 1)) {
                cur1--;
                begin2--;
            }
            int over = last1 - cur1 + 1;
            if (begin2 == 0 && cur1 > 0 && over > max_over) {
                max_over = over;
            }
        }
        return max_over;
    }

    int FindFirst(const BidirectionalPath& path, size_t from = 0) const {
        if (path.Size() > Size()) {
            return -1;
        }
        for (size_t i = from; i <= Size() - path.Size(); ++i) {
            if (CompareFrom(i, path)) {
                return (int) i;
            }
        }
        return -1;
    }

    int FindLast(const BidirectionalPath& path) const {
        if (path.Size() > Size()) {
            return -1;
        }
        for (int i = (int) (Size() - path.Size()); i >= 0; --i) {
            if (CompareFrom((size_t) i, path)) {
                return i;
            }
        }
        return -1;
    }

    bool Contains(const BidirectionalPath& path) const {
        return FindFirst(path) != -1;
    }

    bool Equal(const BidirectionalPath& path) const {
        return operator==(path);
    }

    bool operator==(const BidirectionalPath& path) const {
        return Size() == path.Size() && CompareFrom(0, path);
    }

    bool operator!=(const BidirectionalPath& path) const {
        return !operator==(path);
    }

    void CheckConjugateEnd() {
        size_t prev_size = 0;
        while (prev_size != Size()) {
            prev_size = Size();
            FindConjEdges();
        }
    }

    void FindConjEdges() {
        for (size_t begin_pos = 0; begin_pos < Size(); ++begin_pos) {
            size_t begin = begin_pos;
            vector<size_t> conj_pos = FindAll(g_.conjugate(At(begin_pos)), begin + 1);
            for (auto end_pos = conj_pos.rbegin(); end_pos != conj_pos.rend(); ++end_pos) {
                VERIFY(*end_pos < Size());
                size_t end = *end_pos;
                if (end <= begin) {
                    continue;
                }
                while (begin < end && At(begin) == g_.conjugate(At(end))) {
                    begin++;
                    end--;
                }
                DEBUG("Found palindromic fragment from " << begin_pos << " to " << *end_pos);
                Print();
                VERIFY(*end_pos < Size());
                size_t tail_size = Size() - *end_pos - 1;
                size_t head_size = begin_pos;
                size_t palindrom_half_size = begin - begin_pos;
                size_t head_len = Length() - LengthAt(begin_pos);
                size_t tail_len = *end_pos < Size() - 1 ? LengthAt(*end_pos + 1) : 0;
                size_t palindrom_len = (size_t) max((int) LengthAt(begin_pos) - (int) LengthAt(begin), 0);
                size_t between = (size_t) max(0, (int) LengthAt(begin) - (int) (end < Size() - 1 ? LengthAt(end + 1) : 0));
                DEBUG("tail len " << tail_len << " head len " << head_len << " palindrom_len "<< palindrom_len << " between " << between);
                if (palindrom_len <= cfg::get().max_repeat_length) {
                    if (palindrom_len < head_len && palindrom_len < tail_len) {
                        DEBUG("too big head and end");
                        continue;
                    }
                    if (between > palindrom_len) {
                        DEBUG("to big part between");
                        continue;
                    }
                }
                bool delete_tail = tail_size < head_size;
                if (tail_size == head_size) {
                    delete_tail = tail_len < head_len;
                }
                if (delete_tail) {
                    PopBack(tail_size + palindrom_half_size);
                    return;
                } else {
                    GetConjPath()->PopBack(head_size + palindrom_half_size);
                    return;
                }
            }
        }
    }

    BidirectionalPath SubPath(size_t from, size_t to) const {
        BidirectionalPath result(g_);
        for (size_t i = from; i < min(to, Size()); ++i) {
            result.PushBack(data_[i], gap_len_[i]);
        }
        return result;
    }

    BidirectionalPath SubPath(size_t from) const {
        return SubPath(from, Size());
    }

    double Coverage() const {
        double cov = 0.0;

        for (size_t i = 0; i < Size(); ++i) {
            cov += g_.coverage(data_[i]) * (double) g_.length(data_[i]);
        }
        return cov / (double) Length();
    }

    BidirectionalPath Conjugate(uint32_t id = 0) const {
        BidirectionalPath result(g_);
        if (id == 0) {
            result.SetId(id_ % 2 == 0 ? id_ + 1 : id_ - 1);
        } else {
            result.SetId(id);
        }
        if (Empty()) {
            return result;
        }
        result.PushBack(g_.conjugate(Back()), 0);
        for (int i = ((int) Size()) - 2; i >= 0; --i) {
            result.PushBack(g_.conjugate(data_[i]), gap_len_[i + 1]);
        }

        return result;
    }

    vector<EdgeId> ToVector() const {
        return vector<EdgeId>(data_.begin(), data_.end());
    }

    bool CameToInterstrandBulge() const {
        if (Empty())
            return false;

        EdgeId lastEdge = Back();
        VertexId lastVertex = g_.EdgeEnd(lastEdge);

        if (g_.OutgoingEdgeCount(lastVertex) == 2) {
            vector<EdgeId> bulgeEdges(g_.out_begin(lastVertex), g_.out_end(lastVertex));
            VertexId nextVertex = g_.EdgeEnd(bulgeEdges[0]);

            if (bulgeEdges[0] == g_.conjugate(bulgeEdges[1]) && nextVertex == g_.EdgeEnd(bulgeEdges[1]) && g_.CheckUniqueOutgoingEdge(nextVertex)
                    && *(g_.out_begin(nextVertex)) == g_.conjugate(lastEdge)) {

                DEBUG("Came to interstrand bulge " << g_.int_id(lastEdge));
                return true;
            }
        }
        return false;
    }

    bool IsInterstrandBulge() const {
        if (Empty())
            return false;

        EdgeId lastEdge = Back();
        VertexId lastVertex = g_.EdgeEnd(lastEdge);
        VertexId prevVertex = g_.EdgeStart(lastEdge);

        if (g_.OutgoingEdgeCount(prevVertex) == 2 && g_.IncomingEdgeCount(lastVertex) == 2 && g_.CheckUniqueOutgoingEdge(lastVertex)
                && g_.CheckUniqueIncomingEdge(prevVertex) && *(g_.in_begin(prevVertex)) == g_.conjugate(*(g_.out_begin(lastVertex)))) {

            vector<EdgeId> bulgeEdges(g_.out_begin(prevVertex), g_.out_end(prevVertex));
            EdgeId bulgeEdge = bulgeEdges[0] == lastEdge ? bulgeEdges[1] : bulgeEdges[0];

            if (bulgeEdge == g_.conjugate(lastEdge)) {
                DEBUG("In interstrand bulge " << g_.int_id(lastEdge));
                return true;
            }
        }
        return false;
    }

    void Print() const {
        DEBUG("Path " << id_);
        DEBUG("Length " << Length());
        DEBUG("Weight " << weight_);
        DEBUG("#, edge, length, gap length, total length, total length from begin");
        for (size_t i = 0; i < Size(); ++i) {
            DEBUG(i << ", " << g_.int_id(At(i)) << ", " << g_.length(At(i)) << ", " << GapAt(i) << ", " << LengthAt(i) << ", " << Length() - LengthAt(i));
        }
    }

    void PrintInString() const {
        stringstream str;
        for (size_t i = 0; i < Size(); ++i) {
            str << g_.int_id(At(i)) << " ";
        }
        DEBUG(str.str());
    }
    void PrintInfo() const {
        INFO("Path " << id_);
        INFO("Length " << Length());
        INFO("Weight " << weight_);
        INFO("#, edge, length, gap length, total length");
        for (size_t i = 0; i < Size(); ++i) {
            INFO(i << ", " << g_.int_id(At(i)) << ", " << g_.length(At(i)) << ", " << GapAt(i) << ", " << LengthAt(i));
        }
    }

    void Print(std::ostream& os) {
        if (Empty()) {
            return;
        }
        os << "Path " << GetId() << endl;
        os << "Length " << Length() << endl;
        os << "#, edge, length, total length" << endl;
        for (size_t i = 0; i < Size(); ++i) {
            os << i << ", " << g_.int_id(At(i)) << ", " << g_.length(At(i)) << ", " << LengthAt(i) << endl;
        }
    }

    void SetOverlapedBeginTo(BidirectionalPath* to) {
        if (has_overlaped_begin_) {
            to->SetOverlapBegin();
        }
        SetOverlapBegin();
        to->SetOverlapEnd();
    }

    void SetOverlapedEndTo(BidirectionalPath* to) {
        if (has_overlaped_end_) {
            to->SetOverlapEnd();
        }
        SetOverlapEnd();
        to->SetOverlapBegin();
    }

    void SetOverlap(bool overlap = true) {
        overlap_ = overlap;
        conj_path_->overlap_ = overlap;
    }

    bool HasOverlapedBegin() const {
        return has_overlaped_begin_;
    }

    bool HasOverlapedEnd() const {
        return has_overlaped_end_;
    }

    bool IsOverlap() const {
        return overlap_;
    }
private:

    void RecountLengths() {
        cumulative_len_.clear();
        size_t currentLength = 0;
        for (auto iter = data_.rbegin(); iter != data_.rend(); ++iter) {
            currentLength += g_.length((EdgeId) *iter);
            cumulative_len_.push_front(currentLength);
        }
    }

    void IncreaseLengths(size_t length, size_t gap) {
        for (auto iter = cumulative_len_.begin(); iter != cumulative_len_.end(); ++iter) {
            *iter += length + gap;
        }

        cumulative_len_.push_back(length);
    }

    void DecreaseLengths() {
        size_t length = g_.length(data_.back()) + gap_len_.back();
        for (auto iter = cumulative_len_.begin(); iter != cumulative_len_.end(); ++iter) {
            *iter -= length;
        }
        cumulative_len_.pop_back();
    }

    void NotifyFrontEdgeAdded(EdgeId e, int gap) {
        for (auto i = listeners_.begin(); i != listeners_.end(); ++i) {
            (*i)->FrontEdgeAdded(e, this, gap);
        }
    }

    void NotifyBackEdgeAdded(EdgeId e, int gap) {
        for (auto i = listeners_.begin(); i != listeners_.end(); ++i) {
            (*i)->BackEdgeAdded(e, this, gap);
        }
    }

    void NotifyFrontEdgeRemoved(EdgeId e) {
        for (auto i = listeners_.begin(); i != listeners_.end(); ++i) {
            (*i)->FrontEdgeRemoved(e, this);
        }
    }

    void NotifyBackEdgeRemoved(EdgeId e) {
        for (auto i = listeners_.begin(); i != listeners_.end(); ++i) {
            (*i)->BackEdgeRemoved(e, this);
        }
    }

    void PushFront(EdgeId e, int gap = 0) {
        data_.push_front(e);
        if (gap_len_.size() > 0) {
            gap_len_[0] += gap;
        }
        gap_len_.push_front(0);
        int length = (int) g_.length(e);
        if (cumulative_len_.empty()) {
            cumulative_len_.push_front(length);
        } else {
            cumulative_len_.push_front(length + gap + cumulative_len_.front());
        }
        NotifyFrontEdgeAdded(e, gap);
    }

    void PopFront() {
        EdgeId e = data_.front();
        int cur_gap = gap_len_.front();
        if (gap_len_.size() > 1) {
            cur_gap += GapAt(1);
            gap_len_[1] = 0;
        }
        data_.pop_front();
        gap_len_.pop_front();

        cumulative_len_.pop_front();
        NotifyFrontEdgeRemoved(e);
    }

    void SetOverlapBegin(bool overlap = true) {
        if (has_overlaped_begin_ != overlap) {
            has_overlaped_begin_ = overlap;
        }
        if (GetConjPath()->has_overlaped_end_ != overlap) {
            GetConjPath()->has_overlaped_end_ = overlap;
        }
    }

    void SetOverlapEnd(bool overlap = true) {
        GetConjPath()->SetOverlapBegin(overlap);
    }

    const Graph& g_;
    std::deque<EdgeId> data_;
    BidirectionalPath* conj_path_;
    std::deque<size_t> cumulative_len_;  // Length from beginning of i-th edge to path end for forward directed path: L(e1 + e2 + ... + eN) ... L(eN)
    std::deque<int> gap_len_;  // e1 - gap2 - e2 - ... - gapN - eN
    std::vector<PathListener *> listeners_;
    uint32_t id_;  //Unique ID in PathContainer
    float weight_;
    bool has_overlaped_begin_;
    bool has_overlaped_end_;
    bool overlap_;
    DECL_LOGGER("BidirectionalPath");
};

inline int SkipOneGap(EdgeId end, const BidirectionalPath& path, int gap, int pos, bool forward) {
    size_t len = 0;
    while (pos < (int) path.Size() && pos >= 0 && end != path.At(pos) && (int) len < 2 * gap) {
        len += path.graph().length(path.At(pos));
        forward ? pos++ : pos--;
    }
    if (pos < (int) path.Size() && pos >= 0 && end == path.At(pos)) {
        return pos;
    }
    return -1;
}

inline void SkipGaps(const BidirectionalPath& path1, size_t& cur_pos1, int gap1, const BidirectionalPath& path2, size_t& cur_pos2, int gap2, bool use_gaps,
                     bool forward) {
    if (use_gaps) {
        if (gap1 > 0 && gap2 <= 0) {
            int temp2 = SkipOneGap(path1.At(cur_pos1), path2, gap1, (int) cur_pos2, forward);
            if (temp2 >= 0) {
                cur_pos2 = (size_t) temp2;
            }
        } else if (gap2 > 0 && gap1 <= 0) {
            int temp1 = SkipOneGap(path2.At(cur_pos2), path1, gap2, (int) cur_pos1, forward);
            if (temp1 >= 0) {
                cur_pos1 = (size_t) temp1;
            }
        } else if (gap1 > 0 && gap2 > 0 && gap1 != gap2) {
            DEBUG("not equal gaps in two paths!!!");
        }
    }
}

inline size_t FirstNotEqualPosition(const BidirectionalPath& path1, size_t pos1, const BidirectionalPath& path2, size_t pos2, bool use_gaps) {
    int cur_pos1 = (int) pos1;
    int cur_pos2 = (int) pos2;
    int gap1 = path1.GapAt(cur_pos1);
    int gap2 = path2.GapAt(cur_pos2);
    while (cur_pos1 >= 0 && cur_pos2 >= 0) {
        if (path1.At(cur_pos1) == path2.At(cur_pos2)) {
            cur_pos1--;
            cur_pos2--;
        } else {
            return cur_pos1;
        }
        if (cur_pos1 >= 0 && cur_pos2 >= 0) {
            size_t p1 = (size_t) cur_pos1;
            size_t p2 = (size_t) cur_pos2;
            SkipGaps(path1, p1, gap1, path2, p2, gap2, use_gaps, false);
            cur_pos1 = (int) p1;
            cur_pos2 = (int) p2;
            gap1 = path1.GapAt(cur_pos1);
            gap2 = path2.GapAt(cur_pos2);
        }
    }
    return -1UL;
}
inline bool EqualBegins(const BidirectionalPath& path1, size_t pos1, const BidirectionalPath& path2, size_t pos2, bool use_gaps) {
    return FirstNotEqualPosition(path1, pos1, path2, pos2, use_gaps) == -1UL;
}

inline size_t LastNotEqualPosition(const BidirectionalPath& path1, size_t pos1, const BidirectionalPath& path2, size_t pos2, bool use_gaps) {
    size_t cur_pos1 = pos1;
    size_t cur_pos2 = pos2;
    while (cur_pos1 < path1.Size() && cur_pos2 < path2.Size()) {
        if (path1.At(cur_pos1) == path2.At(cur_pos2)) {
            cur_pos1++;
            cur_pos2++;
        } else {
            return cur_pos1;
        }
        int gap1 = cur_pos1 < path1.Size() ? path1.GapAt(cur_pos1) : 0;
        int gap2 = cur_pos2 < path2.Size() ? path2.GapAt(cur_pos2) : 0;
        SkipGaps(path1, cur_pos1, gap1, path2, cur_pos2, gap2, use_gaps, true);
    }
    return -1UL;
}
inline bool EqualEnds(const BidirectionalPath& path1, size_t pos1, const BidirectionalPath& path2, size_t pos2, bool use_gaps) {
    return LastNotEqualPosition(path1, pos1, path2, pos2, use_gaps) == -1UL;
}
inline bool PathIdCompare(const BidirectionalPath* p1, const BidirectionalPath* p2) {
    return p1->GetId() < p2->GetId();
}

inline bool PathCompare(const BidirectionalPath* p1, const BidirectionalPath* p2) {
    if (p1->GetId() != p2->GetId()) {
        return p1->GetId() < p2->GetId();
    }
    if (p1->Length() != p2->Length()) {
        return p1->Length() < p2->Length();
    }
    return p1->Size() < p2->Size();
}

typedef std::pair<BidirectionalPath*, BidirectionalPath*> PathPair;
bool compare_path_pairs(const PathPair& p1, const PathPair& p2) {
    if (p1.first->Length() != p2.first->Length() || p1.first->Size() == 0 || p2.first->Size() == 0) {
        return p1.first->Length() > p2.first->Length();
    }
    const Graph& g = p1.first->graph();
    return g.int_id(p1.first->Front()) < g.int_id(p2.first->Front());
}

class PathContainer {

public:

    typedef std::vector<PathPair> PathContainerT;

    class Iterator : public PathContainerT::iterator {
    public:
        Iterator(const PathContainerT::iterator& iter)
                : PathContainerT::iterator(iter) {
        }
        BidirectionalPath* get() const {
            return this->operator *().first;
        }
        BidirectionalPath* getConjugate() const {
            return this->operator *().second;
        }
    };

    PathContainer()
            : path_id_(0) {
    }

    BidirectionalPath& operator[](size_t index) const {
        return *(data_[index].first);
    }

    BidirectionalPath* Get(size_t index) const {
        return data_[index].first;
    }

    BidirectionalPath* GetConjugate(size_t index) const {
        return data_[index].second;
    }

    void DeleteAllPaths() {
        for (size_t i = 0; i < data_.size(); ++i) {
            delete data_[i].first;
            delete data_[i].second;
        }
    }

    size_t size() const {
        return data_.size();
    }

    void clear() {
        data_.clear();
    }

    void reserve(size_t size) {
        data_.reserve(size);
    }

    bool AddPair(BidirectionalPath* p, BidirectionalPath* cp) {
        p->SetConjPath(cp);
        cp->SetConjPath(p);
        p->Subscribe(cp);
        cp->Subscribe(p);
        p->SetId(++path_id_);
        cp->SetId(++path_id_);
        data_.push_back(std::make_pair(p, cp));
        return true;
    }

    void SortByLength() {
        std::stable_sort(data_.begin(), data_.end(), compare_path_pairs);
    }

    Iterator begin() {
        return Iterator(data_.begin());
    }

    Iterator end() {
        return Iterator(data_.end());
    }

    Iterator erase(Iterator iter) {
        return Iterator(data_.erase(iter));
    }

    void print() const {
        for (size_t i = 0; i < size(); ++i) {
            Get(i)->Print();
            GetConjugate(i)->Print();
        }
    }

    void FilterEmptyPaths() {
        DEBUG ("try to delete empty paths");
        for (Iterator iter = begin(); iter != end();) {
            if (iter.get()->Size() == 0) {
                iter = erase(iter);
            } else {
                ++iter;
            }
        }
        DEBUG("empty paths are removed");
    }

    void FilterInterstandBulges() {
        DEBUG ("Try to delete paths with interstand bulges");
        for (Iterator iter = begin(); iter != end(); ++iter) {
            if (iter.get()->IsInterstrandBulge()) {
                iter.get()->PopBack();
            }
            if (iter.getConjugate()->IsInterstrandBulge()) {
                iter.getConjugate()->PopBack();
            }
        }
        DEBUG("deleted paths with interstand bulges");
    }

    void ResetPathsId() {
        path_id_ = 0;
        for (size_t i = 0; i < data_.size(); ++i) {
            data_[i].first->SetId(++path_id_);
            data_[i].second->SetId(++path_id_);
        }
    }

private:
    std::vector<PathPair> data_;
    uint32_t path_id_;

protected:
    DECL_LOGGER("BidirectionalPath");

};

inline pair<size_t, size_t> ComparePaths(size_t start_pos1, size_t start_pos2, const BidirectionalPath& path1, const BidirectionalPath& path2,
                                         size_t max_diff) {
    path1.Print();
    path2.Print();
    if (start_pos1 >= path1.Size() || start_pos2 >= path2.Size()) {
        return make_pair(start_pos1, start_pos2);
    }
    const Graph& g = path1.graph();
    size_t cur_pos = start_pos1;
    size_t last2 = start_pos2;
    size_t last1 = cur_pos;
    cur_pos++;
    size_t diff_len = 0;
    while (cur_pos < path1.Size()) {
        if (diff_len > max_diff) {
            return make_pair(last1, last2);
        }
        EdgeId e = path1[cur_pos];
        vector<size_t> poses2 = path2.FindAll(e);
        bool found = false;
        for (size_t pos2 = 0; pos2 < poses2.size(); ++pos2) {
            if (poses2[pos2] > last2) {
                if (path2.LengthAt(last2) - path2.LengthAt(poses2[pos2]) - g.length(path2.At(last2)) - path2.GapAt(poses2[pos2]) > max_diff) {
                    break;
                }
                last2 = poses2[pos2];
                last1 = cur_pos;
                DEBUG("found " << cur_pos);
                found = true;
                break;
            }
        }
        if (!found) {
            diff_len += g.length(e) + path1.GapAt(cur_pos);
            DEBUG("not found " << cur_pos << " now diff len " << diff_len);
        } else {
            diff_len = 0;
        }
        cur_pos++;
    }
    return make_pair(last1, last2);
}
}  // path extend

#endif /* BIDIRECTIONAL_PATH_H_ */
