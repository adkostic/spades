//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#include "standard.hpp"
#include "dataset_readers.hpp"
#include "read_converter.hpp"

#include "de/insert_size_refiner.hpp"
#include "de/paired_info.hpp"

#include "utils.hpp"
#include "stats/debruijn_stats.hpp"

#include "pair_info_count.hpp"
#include "sequence_mapper.hpp"
#include "short_read_mapper.hpp"
#include "long_read_mapper.hpp"
#include "pair_info_filler.hpp"
#include "stats/debruijn_stats.hpp"
#include "path_extend/split_graph_pair_info.hpp"

namespace debruijn_graph {

//    template<class PairedRead>
//    pair<bool, int> GetISFromLongEdge(const PairedRead& read, size_t edge_length_threshold) const {
//        auto pos_left = GetLastKmerPos(read.first.sequence());
//        auto pos_right = GetFirstKmerPos(read.second.sequence());
//        if (pos_left.second == -1u || pos_right.second == -1u || pos_left.first != pos_right.first || g_.length(pos_left.first) < edge_length_threshold) {
//          return make_pair(false, 0);
//        }
//
//        return make_pair(true, (int) (pos_right.second - pos_left.second - small_k_ - read.insert_size() + left.size() + right.size()));
//    }
//
//  pair<EdgeId, size_t> GetFirstKmerPos(const Sequence &sequence) const {
//    if (sequence.size() < k_) {
//      return make_pair(EdgeId(0), -1u);
//    }
//
//    Kmer left = sequence.start<Kmer>(k_);
//    left = kmer_mapper_.Substitute(left);
//
//    return index_.get(left);
//  }
//
//  pair<EdgeId, size_t> GetLastKmerPos(const Sequence &sequence) const {
//    if (sequence.size() < k_) {
//      return make_pair(EdgeId(0), -1u);
//    }
//
//    Kmer right = sequence.end<Kmer>(k_);
//    right = kmer_mapper_.Substitute(right);
//
//    return index_.get(right);
//  }
template<class graph_pack, class PairedStreams, class Mapper>
size_t RefineInsertSizeParallel(const graph_pack& gp,
        const Mapper& mapper,
        PairedStreams& streams,
        InsertSizeHistogramCounter<graph_pack>& counter,
        size_t edge_length_threshold) {

  typedef typename Mapper::Kmer Kmer;
  size_t mapper_k = mapper.KmerSize();

  size_t nthreads = streams.size();
  std::vector<size_t> rls(nthreads, 0);
  counter.Init(nthreads);

#pragma omp parallel for num_threads(nthreads)
  for (size_t i = 0; i < nthreads; ++i) {
    typename PairedStreams::ReadT r;
    auto& stream = streams[i];
    stream.reset();

    while (!stream.eof()) {
      stream >> r;
      Sequence first = r.first().sequence();
      Sequence second = r.second().sequence();
      rls[i] = std::max(std::max(first.size(), second.size()), rls[i]);

      if (first.size() < mapper_k || second.size() < mapper_k)
          continue;
      
      auto pos_left = mapper.GetKmerPos(first.start<Kmer>(mapper_k));
      auto pos_right = mapper.GetKmerPos(second.end<Kmer>(mapper_k));
      if (pos_left.second == -1u || pos_right.second == -1u || pos_left.first != pos_right.first || gp.g.length(pos_left.first) < edge_length_threshold) {
        counter.ProcessPairedRead(i, false, 0);
      } else {
        counter.ProcessPairedRead(i, true, (int) (pos_right.second - pos_left.second - mapper_k - r.insert_size() + first.size() + second.size()));
      }
    }
  }
  counter.Finalize();

  size_t rl = 0;
  for (size_t i = 0; i < nthreads; ++i) {
    rl = std::max(rl, rls[i]);
  }
  return rl;
}


template<class graph_pack, class PairedStreams, class DataSet>
bool RefineInsertSizeForLib(const graph_pack& gp,
                      PairedStreams& streams,
                      DataSet& data,
                      size_t edge_length_threshold) {

  INFO("Estimating insert size (takes a while)");
  InsertSizeHistogramCounter<graph_pack> hist_counter(gp, /* ignore negative */ true);

  //FIXME
  auto mapper = ChooseProperMapper(gp, data.read_length == 0 ? gp.k_value + 1 : data.read_length);

  size_t rl = RefineInsertSizeParallel(gp, *mapper, streams, hist_counter, edge_length_threshold);
  if (data.read_length == 0)
      data.read_length = rl;

  INFO(hist_counter.mapped() << " paired reads (" << ((double) hist_counter.mapped() * 100.0 / (double) hist_counter.total()) << "% of all) aligned to long edges");
  if (hist_counter.negative() > 3 * hist_counter.mapped())
      WARN("Too much reads aligned with negative insert size. Does the library orientation set properly?");
  if (hist_counter.mapped() == 0)
    return false;

  std::map<size_t, size_t> percentiles;
  hist_counter.FindMean(data.mean_insert_size, data.insert_size_deviation, percentiles);
  hist_counter.FindMedian(data.median_insert_size, data.insert_size_mad, data.insert_size_distribution);

  std::tie(data.insert_size_left_quantile, data.insert_size_right_quantile) = GetISInterval(0.8, data.insert_size_distribution);

  if (data.insert_size_distribution.size() == 0)
    return false;

  return true;
}

void ProcessSingleReads(conj_graph_pack& gp, size_t ilib) {
    const io::SequencingLibrary<debruijn_config::DataSetData>& reads = cfg::get().ds.reads[ilib];
    SequenceMapperNotifier notifier(gp);
    SimpleLongReadMapper read_mapper(gp, gp.single_long_reads[ilib]);
    notifier.Subscribe(ilib, &read_mapper);

    if (cfg::get().use_multithreading) {
        auto single_streams = single_binary_readers(reads, true, false);
        notifier.ProcessLibrary(single_streams, ilib, reads.data().read_length, single_streams.size());
    }
    else {
        io::SingleStreams single_streams(single_easy_reader(reads, true, false));
        notifier.ProcessLibrary(single_streams, ilib, reads.data().read_length, single_streams.size());
    }
}


void ProcessPairedReads(conj_graph_pack& gp, size_t ilib, bool calculate_threshold, bool map_single_reads) {
    SequenceMapperNotifier notifier(gp);
    const io::SequencingLibrary<debruijn_config::DataSetData>& reads = cfg::get().ds.reads[ilib];
    INFO("Left insert size qauntile " << reads.data().insert_size_left_quantile << ", right insert size quantile " << reads.data().insert_size_right_quantile);

    SimpleLongReadMapper read_mapper(gp, gp.single_long_reads[ilib]);
    if (map_single_reads) {
        notifier.Subscribe(ilib, &read_mapper);
    }

    path_extend::SplitGraphPairInfo split_graph(
            gp, (size_t) reads.data().median_insert_size,
            (size_t) reads.data().insert_size_deviation,
            (size_t) reads.data().insert_size_left_quantile,
            (size_t) reads.data().insert_size_right_quantile,
            reads.data().read_length, gp.g.k(),
            cfg::get().pe_params.param_set.split_edge_length,
            reads.data().insert_size_distribution);
    if (calculate_threshold) {
        notifier.Subscribe(ilib, &split_graph);
    }

    LatePairedIndexFiller pif(gp.g, PairedReadCountWeight, gp.paired_indices[ilib]);
    notifier.Subscribe(ilib, &pif);

    VERIFY(cfg::get().use_multithreading);
    auto paired_streams = paired_binary_readers(reads, true, (size_t) reads.data().mean_insert_size);
    notifier.ProcessLibrary(paired_streams, ilib, reads.data().read_length, paired_streams.size());
    cfg::get_writable().ds.reads[ilib].data().pi_threshold = split_graph.GetThreshold();

    if (map_single_reads) {
        ProcessSingleReads(gp, ilib);
    }
}

bool ShouldMapSingleReads(bool has_good_rr_reads, size_t ilib) {
    bool map_single_reads = cfg::get().always_single_reads_rr || (!has_good_rr_reads && cfg::get().single_reads_rr);
    if (cfg::get().ds.reads[ilib].type() != io::LibraryType::HQMatePairs) {
        return map_single_reads;
    }
    if (map_single_reads) {
        return true;
    }
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        if (cfg::get().ds.reads[i].type() != io::LibraryType::MatePairs && cfg::get().ds.reads[i].type() != io::LibraryType::HQMatePairs) {
            return false;
        }
    }
    return true;
}

void PairInfoCount::run(conj_graph_pack &gp, const char*) {
    if (!cfg::get().developer_mode) {
        gp.paired_indices.Attach();
        gp.paired_indices.Init();
    }

    size_t edge_length_threshold = stats::Nx(gp.g, 50);
    INFO("Graph N50: " << edge_length_threshold);
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        INFO("Estimating insert size for library #" << i);
        if (cfg::get().ds.reads[i].is_paired()) {

            bool insert_size_refined;
            VERIFY(cfg::get().use_multithreading);
            auto streams = paired_binary_readers(cfg::get().ds.reads[i], false, 0);
            insert_size_refined = RefineInsertSizeForLib(gp, streams, cfg::get_writable().ds.reads[i].data(), edge_length_threshold);

            if (!insert_size_refined) {
                cfg::get_writable().ds.reads[i].data().mean_insert_size = 0.0;
                WARN("Unable to estimate insert size for paired library #" << i);
                if (cfg::get().ds.reads[i].data().read_length > 0 && cfg::get().ds.reads[i].data().read_length <= cfg::get().K) {
                    WARN("Maximum read length (" << cfg::get().ds.reads[i].data().read_length << ") should be greater than K (" << cfg::get().K << ")");
                }
                else if (cfg::get().ds.reads[i].data().read_length <= cfg::get().K * 11 / 10) {
                    WARN("Maximum read length (" << cfg::get().ds.reads[i].data().read_length << ") is probably too close to K (" << cfg::get().K << ")");
                } else {
                    WARN("None of paired reads aligned properly. Please, check orientation of your read pairs.");
                }
                continue;
            } else {
                INFO("  Estimated insert size for paired library #" << i);
                INFO("  Insert size = " << cfg::get().ds.reads[i].data().mean_insert_size <<
                        ", deviation = " << cfg::get().ds.reads[i].data().insert_size_deviation <<
                        ", left quantile = " << cfg::get().ds.reads[i].data().insert_size_left_quantile <<
                        ", right quantile = " << cfg::get().ds.reads[i].data().insert_size_right_quantile <<
                        ", read length = " << cfg::get().ds.reads[i].data().read_length);
            }
        }
    }

    bool has_good_rr_reads = false;
    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        if (cfg::get().ds.reads[i].is_paired() &&
                (cfg::get().ds.reads[i].data().mean_insert_size == 0.0 ||
                cfg::get().ds.reads[i].data().mean_insert_size < 1.1 * (double) cfg::get().ds.reads[i].data().read_length)) {
            continue;
        }
        if (cfg::get().ds.reads[i].is_repeat_resolvable()) {
            has_good_rr_reads = true;
            break;
        }
    }

    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        bool map_single_reads = ShouldMapSingleReads(has_good_rr_reads, i);
        INFO("Mapping library #" << i);
        if (cfg::get().ds.reads[i].is_paired() && cfg::get().ds.reads[i].data().mean_insert_size != 0.0) {
            INFO("Mapping paired reads (takes a while) ");
            bool calculate_threshold = (cfg::get().ds.reads[i].type() == io::LibraryType::PairedEnd);
            ProcessPairedReads(gp, i, calculate_threshold, map_single_reads);
        }

        if (cfg::get().ds.reads[i].type() == io::LibraryType::SingleReads && map_single_reads) {
            INFO("Mapping single reads (takes a while) ");
            ProcessSingleReads(gp, i);
        }

        if (map_single_reads) {
            INFO("Total paths obtained from single reads: " << gp.single_long_reads[i].size());
        }
    }
}

}
