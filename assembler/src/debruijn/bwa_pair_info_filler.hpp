//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "sam/sam_reader.hpp"
#include "standard.hpp"
#include "debruijn_graph.hpp"
#include "config_struct.hpp"

#include <io/osequencestream.hpp>
#include <de/paired_info.hpp>
#include <de/insert_size_refiner.hpp>

#ifndef PROJECT_BWA_PAIR_INFO_FILLER_HPP_H
#define PROJECT_BWA_PAIR_INFO_FILLER_HPP_H

namespace bwa_pair_info {

using namespace sam_reader;
using debruijn_graph::EdgeId;

typedef omnigraph::de::UnclusteredPairedInfoIndexT<debruijn_graph::Graph> PairedInfoIndexT;
typedef io::SequencingLibrary<debruijn_graph::debruijn_config::DataSetData> SequencingLibraryT;
typedef std::pair<debruijn_graph::EdgeId, debruijn_graph::EdgeId> EdgePair;
typedef unordered_map<size_t, debruijn_graph::EdgeId> EdgeIdMap;


class MapperReadT {
public:
    MapperReadT(): contig_id_(""), pos_(-1), len_(-1), is_forward_(true),
                   left_hard_clip_(0), right_hard_clip_(0), left_soft_clip_(0), right_soft_clip_(0){}

    MapperReadT(const string& ctg_id, int32_t pos, int32_t len, bool is_forward, const string& cigar):
        contig_id_(ctg_id), pos_(pos), len_(len), is_forward_(is_forward),
        left_hard_clip_(0), right_hard_clip_(0), left_soft_clip_(0), right_soft_clip_(0) {

        ParseCigar(cigar);
    }

    bool IsValid() const {
        return contig_id_ != "";
    }

private:

    void ParseCigar(const string& cigar);

public:
    const string &get_contig_id() const {
        return contig_id_;
    }
    int32_t get_pos() const {
        return pos_;
    }
    int32_t get_len() const {
        return len_;
    }
    bool is_forward() const {
        return is_forward_;
    }
    uint32_t get_left_soft_clip() const {
        return left_soft_clip_;
    }
    uint32_t get_right_soft_clip() const {
        return right_soft_clip_;
    }
    uint32_t get_left_hard_clip() const {
        return left_hard_clip_;
    }
    uint32_t get_right_hard_clip() const {
        return right_hard_clip_;
    }

private:
    string contig_id_;
    int32_t pos_;
    int32_t len_;
    bool is_forward_;
    uint32_t left_hard_clip_:16, right_hard_clip_:16;
    uint32_t left_soft_clip_:16, right_soft_clip_:16;
};


class BWAPairedReadProcessor {
public:
    virtual void ProcessPairedRead(const MapperReadT& l, const MapperReadT& r) = 0;

    virtual ~BWAPairedReadProcessor() {

    }
};

class BWACorrectingProcessor: public BWAPairedReadProcessor {
protected:
    const SequencingLibraryT& lib_;

    const EdgeIdMap& edge_id_map_;

    const debruijn_graph::Graph& g_;

    size_t count_;

public:

    struct MappedPositionT {
        EdgeId e_;
        int pos_;

        MappedPositionT(EdgeId e, int pos): e_(e), pos_(pos) {

        }
    };

    BWACorrectingProcessor(const SequencingLibraryT& lib, const EdgeIdMap& edge_id_map, const debruijn_graph::Graph& g):
        lib_(lib), edge_id_map_(edge_id_map), g_(g), count_(0) {
    }

    virtual bool CheckAlignments(const MappedPositionT& l, const MappedPositionT& r) = 0;

    virtual void ProcessAlignments(const MappedPositionT& l, const MappedPositionT& r) = 0;

    virtual void ProcessPairedRead(const MapperReadT& l, const MapperReadT& r);
};


class BWAISCounter: public BWACorrectingProcessor {
private:
    HistType hist_;

    size_t min_contig_len_;

    bool ignore_negative_;

    size_t mapped_count_;

    size_t negative_count_;


public:
    BWAISCounter(const SequencingLibraryT& lib, const EdgeIdMap& edge_id_map, const debruijn_graph::Graph& g,
                 size_t min_contig_len, bool ignore_negative = false):
        BWACorrectingProcessor(lib, edge_id_map, g), hist_(), min_contig_len_(min_contig_len),
        ignore_negative_(ignore_negative), mapped_count_(0), negative_count_(0) {
    }

    virtual bool CheckAlignments(const MappedPositionT& l, const MappedPositionT& r);

    virtual void ProcessAlignments(const MappedPositionT& l, const MappedPositionT& r);

    bool RefineInsertSize(SequencingLibraryT& reads) const ;

};


class BWAIndexFiller: public BWACorrectingProcessor {

private:
    PairedInfoIndexT& paired_index_;

    size_t min_contig_len_;

    EdgePair ConjugatePair(EdgePair ep) const;

public:
    BWAIndexFiller(const SequencingLibraryT& lib, const EdgeIdMap& edge_id_map, const debruijn_graph::Graph& g,
                   PairedInfoIndexT& paired_index, size_t min_contig_len = 0):
        BWACorrectingProcessor(lib, edge_id_map, g), paired_index_(paired_index), min_contig_len_(min_contig_len) {
    }

    virtual bool CheckAlignments(const MappedPositionT& l, const MappedPositionT& r);

    virtual void ProcessAlignments(const MappedPositionT& l, const MappedPositionT& r);
};


class BWAPairInfoFiller {
public:
    DECL_LOGGER("BWAPairInfo");

private:
    const debruijn_graph::Graph& g_;

    string bwa_path_;

    string work_dir_;

    size_t nthreads_;

    string index_base_;

    bool index_constructed_;

    unordered_map<size_t, debruijn_graph::EdgeId> edge_id_map_;

private:

    void OutputEdges(const string& filename) const;

    void FillEdgeIdMap();

    bool CreateIndex(const string& contigs);

    bool Init();

    bool RunBWA(const string& reads_file, const string& out_sam_file) const;

    bool AlignLib(const SequencingLibraryT& lib,
                      const string& sam_file_base,
                      vector<pair<string, string>>& resulting_sam_files);

    void ProcessSAMFiles(const string &left_sam, const string &right_sam,
                         BWAPairedReadProcessor& processor);

public:

    BWAPairInfoFiller(const debruijn_graph::Graph& g,
                      const string& bwa_path,
                      const string& work_dir,
                      size_t nthreads = 1):
        g_(g), bwa_path_(bwa_path), work_dir_(work_dir),
        nthreads_(nthreads), index_base_(""), index_constructed_(false),
        edge_id_map_() {
    }

    ~BWAPairInfoFiller() {
        path::remove_if_exists(work_dir_);
    }

    bool ProcessLib(size_t lib_index,
                    SequencingLibraryT& lib,
                    PairedInfoIndexT& paired_index,
                    size_t counter_edge_len,
                    size_t index_filler_edge_len);
};

};

#endif //PROJECT_BWA_PAIR_INFO_FILLER_HPP_H
