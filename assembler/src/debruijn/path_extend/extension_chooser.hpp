//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * extension.hpp
 *
 *  Created on: Mar 5, 2012
 *      Author: andrey
 */

#ifndef EXTENSION_HPP_
#define EXTENSION_HPP_

#include <cfloat>
#include <iostream>
#include <fstream>
#include "weight_counter.hpp"
#include "pe_utils.hpp"
#include "next_path_searcher.hpp"

namespace path_extend {

typedef std::multimap<double, EdgeWithDistance> AlternativeConteiner;


class PathAnalyzer {

protected:
    const Graph& g_;

public:
    PathAnalyzer(const Graph& g): g_(g) {
    }

    int ExcludeTrivial(const BidirectionalPath& path, std::map<size_t, double>& edges, int from = -1) {
        int edgeIndex = (from == -1) ? (int) path.Size() - 1 : from;
        if ((int) path.Size() <= from) {
            return edgeIndex;
        }
        VertexId currentVertex = g_.EdgeEnd(path[edgeIndex]);
        while (edgeIndex >= 0 && g_.CheckUniqueIncomingEdge(currentVertex)) {
            EdgeId e = g_.GetUniqueIncomingEdge(currentVertex);
            currentVertex = g_.EdgeStart(e);

            edges.insert(make_pair((size_t)edgeIndex, 0.0));
            --edgeIndex;
        }
        return edgeIndex;
    }

    int ExcludeTrivialWithBulges(const BidirectionalPath& path, std::map<size_t, double>& edges) {
        edges.clear();

        if (path.Empty()) {
            return 0;
        }

        int lastEdge = (int) path.Size() - 1;
        do {
            lastEdge = ExcludeTrivial(path, edges, lastEdge);

            if (lastEdge >= 0) {
                VertexId v = g_.EdgeEnd(path[lastEdge]);
                VertexId u = g_.EdgeStart(path[lastEdge]);
                auto bulgeCandidates = g_.IncomingEdges(v);
                bool bulge = true;

                for (auto iter = bulgeCandidates.begin(); iter != bulgeCandidates.end(); ++iter) {
                    if (g_.EdgeStart(*iter) != u) {
                        bulge = false;
                        break;
                    }
                }

                if (!bulge) {
                    break;
                }

                --lastEdge;
            }
        } while (lastEdge >= 0);

        return lastEdge;
    }
};


class ExtensionChooserListener {

public:

    virtual void ExtensionChosen(double weight) = 0;

    virtual void ExtensionChosen(AlternativeConteiner& alts) = 0;

    virtual ~ExtensionChooserListener() {

    }
};


class ExtensionChooser {

public:
    typedef std::vector<EdgeWithDistance> EdgeContainer;

protected:
    const Graph& g_;

    WeightCounter * wc_;

    PathAnalyzer analyzer_;

    double prior_coeff_;

    bool excludeTrivial_;
    bool excludeTrivialWithBulges_;

    std::vector<ExtensionChooserListener *> listeners_;

public:
    ExtensionChooser(const Graph& g, WeightCounter * wc = 0, double priority = 0.0): g_(g), wc_(wc), analyzer_(g), prior_coeff_(priority),
        excludeTrivial_(true), excludeTrivialWithBulges_(true), listeners_() {
    }

    virtual ~ExtensionChooser() {

    }

    virtual EdgeContainer Filter(BidirectionalPath& path, EdgeContainer& edges) = 0;

    bool isExcludeTrivial() const
    {
        return excludeTrivial_;
    }

    double CountWeight(BidirectionalPath& path, EdgeId e) {
        return wc_->CountWeight(path, e);
    }

    bool isExcludeTrivialWithBulges() const
    {
        return excludeTrivialWithBulges_;
    }

    void setExcludeTrivial(bool excludeTrivial)
    {
        this->excludeTrivial_ = excludeTrivial;
    }

    void setExcludeTrivialWithBulges(bool excludeTrivialWithBulges)
    {
        this->excludeTrivialWithBulges_ = excludeTrivialWithBulges;
    }

    void ClearExcludedEdges() {
        wc_->GetExcludedEdges().clear();
    }

    PairedInfoLibraries& getLibs() {
        return wc_->getLibs();
    }

    void Subscribe(ExtensionChooserListener * listener) {
        listeners_.push_back(listener);
    }

    void NotifyAll(double weight) {
        for (auto iter = listeners_.begin(); iter != listeners_.end(); ++iter) {
            (*iter)->ExtensionChosen(weight);
        }
    }

    void NotifyAll(AlternativeConteiner& alts) {
        for (auto iter = listeners_.begin(); iter != listeners_.end(); ++iter) {
            (*iter)->ExtensionChosen(alts);
        }
    }

    bool WeighConterBased() const {
        return wc_ != 0;
    }

protected:
    void RemoveTrivial(BidirectionalPath& path){
    	wc_->GetExcludedEdges().clear();
        if (excludeTrivialWithBulges_)
        {
            analyzer_.ExcludeTrivialWithBulges(path, wc_->GetExcludedEdges());
        }
        else if (excludeTrivial_)
        {
            analyzer_.ExcludeTrivial(path, wc_->GetExcludedEdges());
        }
    }


};


class JointExtensionChooser: public ExtensionChooser {

protected:
    ExtensionChooser * first_;

    ExtensionChooser * second_;

public:
    JointExtensionChooser(Graph& g, ExtensionChooser * first, ExtensionChooser * second): ExtensionChooser(g),
        first_(first), second_(second)
    {
    }

    virtual EdgeContainer Filter(BidirectionalPath& path, EdgeContainer& edges) {
        EdgeContainer e1 = first_->Filter(path, edges);
        return second_->Filter(path, e1);
    }
};


class TrivialExtensionChooser: public ExtensionChooser {

public:
    TrivialExtensionChooser(Graph& g): ExtensionChooser(g)  {
    }

    virtual EdgeContainer Filter(BidirectionalPath& /*path*/, EdgeContainer& edges) {
        if (edges.size() == 1) {
             return edges;
        }
        return EdgeContainer();
    }
};


class TrivialExtensionChooserWithPI: public ExtensionChooser {

public:
    TrivialExtensionChooserWithPI(Graph& g, WeightCounter * wc): ExtensionChooser(g, wc) {
    }

    virtual EdgeContainer Filter(BidirectionalPath& path, EdgeContainer& edges) {
        wc_->GetExcludedEdges().clear();
        if (edges.size() == 1) {
                double weight = wc_->CountWeight(path, edges.back().e_);
                NotifyAll(weight);

                if (wc_->IsExtensionPossible(weight)) {
                    return edges;
                }
        }
        return EdgeContainer();
    }
};


class SimpleExtensionChooser: public ExtensionChooser {

protected:

	void RemoveTrivialAndCommon(BidirectionalPath& path, EdgeContainer& edges) {
        ClearExcludedEdges();
        if (edges.size() < 2) {
            return;
        }
        RemoveTrivial(path);
        int index = (int) path.Size() - 1;
        std::map<size_t, double>& excluded_edges = wc_->GetExcludedEdges();
        while (index >= 0) {
            if (excluded_edges.find(index) != excluded_edges.end()) {
                index--;
                continue;
            }
            EdgeId path_edge = path[index];
            double min_ideal_w = wc_->CountIdealInfo(path_edge, edges.at(0).e_,
                                                     path.LengthAt(index));
            bool common = true;
            for (size_t i = 0; i < edges.size(); ++i) {
                double ideal_weight = wc_->CountIdealInfo(path_edge,
                                                          edges.at(i).e_,
                                                          path.LengthAt(index));
                min_ideal_w = std::min(min_ideal_w, ideal_weight);
                if (!wc_->PairInfoExist(path_edge, edges.at(i).e_,
                                        (int) path.LengthAt(index))) {
                    common = false;
                }
            }
            if (common) {
                excluded_edges.insert(make_pair((size_t) index, 0.0));
            } else {
                excluded_edges.insert(make_pair((size_t) index, min_ideal_w));
            }
            index--;
        }
    }

	void FindWeights(BidirectionalPath& path, EdgeContainer& edges,
			AlternativeConteiner& weights) {
		for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
			double weight = wc_->CountWeight(path, iter->e_);
			weights.insert(std::make_pair(weight, *iter));
			DEBUG("Candidate " << g_.int_id(iter->e_) << " weight " << weight << " length " << g_.length(iter->e_));
			path.getLoopDetector().AddAlternative(iter->e_, weight);

		}
		NotifyAll(weights);
	}

	void FindPossibleEdges(AlternativeConteiner& weights, EdgeContainer& top,
			double max_weight) {
		auto possibleEdge = weights.lower_bound(max_weight / prior_coeff_);
		for (auto iter = possibleEdge; iter != weights.end(); ++iter) {
			top.push_back(iter->second);
		}
	}

	EdgeContainer FindFilteredEdges(BidirectionalPath& path,
			EdgeContainer& edges) {
		AlternativeConteiner weights;
		FindWeights(path, edges, weights);
		EdgeContainer top;
		auto maxWeight = (--weights.end())->first;
		FindPossibleEdges(weights, top, maxWeight);
		EdgeContainer result;
		if (top.size() >= 1 && wc_->IsExtensionPossible(maxWeight)) {
			result = top;
		}
		return result;
	}
public:
	SimpleExtensionChooser(const Graph& g, WeightCounter * wc, double priority) :
			ExtensionChooser(g, wc, priority) {

	}

	virtual EdgeContainer Filter(BidirectionalPath& path,
			EdgeContainer& edges) {
	    DEBUG("Paired-end extension chooser");
		if (edges.empty()) {
			return edges;
		}
		RemoveTrivial(path);
		path.Print();
		EdgeContainer result = edges;
		bool first_time = true;
		bool changed = true;
		if (first_time || (result.size() > 1 && changed)) {
		    first_time = false;
			RemoveTrivialAndCommon(path, result);
			EdgeContainer new_result = FindFilteredEdges(path, result);
			if (new_result.size() == result.size()) {
				changed = false;
			}
			result = new_result;
		}
		if (result.size() == 1) {
            DEBUG("Paired-end extension chooser helped");
        }
		return result;
	}

};



class ScaffoldingExtensionChooser: public ExtensionChooser {

    bool cluster_info_;

    static bool compare(pair<int,double> a, pair<int,double> b)
    {
        if (a.first < b.first) return true;
        else return false;
    }

public:
	ScaffoldingExtensionChooser(const Graph& g, WeightCounter * wc, double priority, bool cluster_info = true): ExtensionChooser(g, wc, priority), cluster_info_(cluster_info) {
    }

	double AddInfoFromEdge(const std::vector<int>& distances, const std::vector<double>& weights, std::vector<pair<int,double> >& histogram, const BidirectionalPath& path, size_t j, double threshold)
	{
		double mean = 0.0;
		double sum  = 0.0;

		for (size_t l = 0; l < distances.size(); ++ l){
			if (distances[l] > max(0, (int) path.LengthAt(j) - (int) g_.k()) && weights[l] >= threshold) {
                mean += ((distances[l] - (int) path.LengthAt(j)) * weights[l]);
                sum += weights[l];
                histogram.push_back(make_pair(distances[l] - path.LengthAt(j), weights[l]));
			}
		}
		return mean / sum;
	}


    int CountMean(vector< pair<int,double> >& histogram)
    {
        double dist = 0.0;
        double sum = 0;
        for (size_t i = 0; i < histogram.size(); ++ i) {
			 sum += histogram[i].second;
		}
        for (size_t i = 0; i < histogram.size(); ++ i) {
			 dist += (histogram[i].first * histogram[i].second / sum);
		}
        return (int) round(dist);
    }

    int CountDev(vector< pair<int,double> >& histogram, int avg)
    {
        double dev = 0.0;
        double sum = 0;
        for (size_t i = 0; i < histogram.size(); ++ i) {
             sum += histogram[i].second;
        }
        for (size_t i = 0; i < histogram.size(); ++ i) {
             dev += (((double) (histogram[i].first - avg)) * ((double) (histogram[i].first - avg)) * ((double) histogram[i].second));
        }
        return (int) round(sqrt(dev / sum));
    }

    vector< pair<int,double> > FilterHistogram(vector< pair<int,double> >& histogram, int start, int end, int threshold)
    {
        vector< pair<int,double> > res;

        for (size_t i = 0; i < histogram.size(); ++i){
            if (histogram[i].first >= start && histogram[i].first <= end && histogram[i].second >= threshold) {
                res.push_back(histogram[i]);
            }
        }

        return res;
    }

    double CountAvrgDists(BidirectionalPath& path, EdgeId e, std::vector<pair<int,double> > & histogram)
    {
		std::vector<int> distances;
		std::vector<double> weights;

		double max_weight = 0.0;
		//bool print = true;
		for (size_t j = 0; j < path.Size(); ++ j) {
			wc_->GetDistances(path.At(j), e, distances, weights);

			for (size_t l = 0; l < weights.size(); ++l){
				if (weights[l] > max_weight) {
				    max_weight = weights[l];
				}
			}

			if (distances.size() > 0) {
				AddInfoFromEdge(distances, weights, histogram, path, j, 0);
			}
			distances.clear();
		}
		return max_weight;
    }

    void FindBestFittedEdges(BidirectionalPath& path, EdgeContainer& edges, EdgeContainer& result)
    {
		std::vector<pair<int,double> > histogram;
		for (size_t i = 0; i < edges.size(); ++i){
			histogram.clear();
			double max_w = CountAvrgDists(path, edges[i].e_, histogram);

			for (int j = 0; j < 2; ++j) {
                int mean = CountMean(histogram);
                int dev = CountDev(histogram, mean);
                double cutoff = min(max_w * cfg::get().pe_params.param_set.scaffolder_options.rel_cutoff, (double) cfg::get().pe_params.param_set.scaffolder_options.cutoff);
                histogram = FilterHistogram(histogram, mean - (5 - j)  * dev, mean + (5 - j) * dev, (int) round(cutoff));
			}

			double sum = 0.0;
			for (size_t j = 0; j < histogram.size(); ++j) {
			    sum += histogram[j].second;
			}

			if (sum > cfg::get().pe_params.param_set.scaffolder_options.sum_threshold) {
				sort(histogram.begin(), histogram.end(), compare);
				int gap = CountMean(histogram);

				if (wc_->CountIdealInfo(path, edges[i].e_, gap) > 0.0) {
					result.push_back(EdgeWithDistance(edges[i].e_, gap));
                }
			}
		}
    }


    void FindBestFittedEdgesForClustered(BidirectionalPath& path, EdgeContainer& edges, EdgeContainer& result)
    {
        std::vector<pair<int,double> > histogram;
        for (size_t i = 0; i < edges.size(); ++i){
            histogram.clear();
            CountAvrgDists(path, edges[i].e_, histogram);

            double sum = 0.0;
            for (size_t j = 0; j < histogram.size(); ++j) {
                sum += histogram[j].second;
            }
            if (sum > cfg::get().pe_params.param_set.scaffolder_options.cl_threshold) {
                sort(histogram.begin(), histogram.end(), compare);
                int gap = CountMean(histogram);
                if (wc_->CountIdealInfo(path, edges[i].e_, gap) > 0.0) {
                    DEBUG("scaffolding " << g_.int_id(edges[i].e_) << " gap " << gap);
                    result.push_back(EdgeWithDistance(edges[i].e_, gap));
                }
            }
        }
    }

    virtual EdgeContainer Filter(BidirectionalPath& path, EdgeContainer& edges) {
        if (edges.empty()) {
            return edges;
        }
        EdgeContainer result;

        if (cluster_info_) {
            FindBestFittedEdgesForClustered(path, edges, result);
        } else {
            FindBestFittedEdges(path, edges, result);
        }

        return result;
    }
};

bool EdgeWithWeightCompareReverse(const pair<EdgeId, double>& p1,
                                      const pair<EdgeId, double>& p2) {
    return p1.second > p2.second;
}

class LongReadsExtensionChooser : public ExtensionChooser {
public:
    LongReadsExtensionChooser(const Graph& g, PathContainer& pc,
                              double filtering_threshold,
                              double weight_priority_threshold,
                              double unique_edge_priority_threshold)
            : ExtensionChooser(g, 0, .0),
              filtering_threshold_(filtering_threshold),
              weight_priority_threshold_(weight_priority_threshold),
              unique_edge_priority_threshold_(unique_edge_priority_threshold),
              coverage_map_(g, pc),
              unique_edges_founded_(false) {
    }

    /* Choose extension as correct only if we have reads that traverse a unique edge from the path and this extension.
     * Edge is unique if all reads mapped to this edge are consistent.
     * Two reads are consistent if they can form one path in the graph.
     */
    virtual EdgeContainer Filter(BidirectionalPath& path,
                                 EdgeContainer& edges) {
        if (!unique_edges_founded_) {
            FindAllUniqueEdges();
        }
        if (edges.empty()) {
            return edges;
        }DEBUG("We in Filter of LongReadsExtensionChooser");
        path.Print();
        map<EdgeId, double> weights_cands;
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            weights_cands.insert(make_pair(it->e_, 0.0));
        }
        set<EdgeId> filtered_cands;
        auto support_paths = coverage_map_.GetCoveringPaths(path.Back());
        for (auto it = support_paths.begin(); it != support_paths.end(); ++it) {
            auto positions = (*it)->FindAll(path.Back());
            for (size_t i = 0; i < positions.size(); ++i) {
                if ((int) positions[i] < (int) (*it)->Size() - 1 &&
                        EqualBegins(path, (int) path.Size() - 1, **it, positions[i])) {

                    if (UniqueBackPath(**it, positions[i])) {
                        EdgeId next = (*it)->At(positions[i] + 1);
                        weights_cands[next] += (*it)->GetWeight();
                        filtered_cands.insert(next);
                    }
                }
            }
        }DEBUG("Candidates");
        for (auto iter = weights_cands.begin(); iter != weights_cands.end();
                ++iter) {
            DEBUG("Candidate " << g_.int_id(iter->first) << " weight " << iter->second);
        }
        vector<pair<EdgeId, double> > sort_res = MapToSortVector(weights_cands);
        if (sort_res.size() < 1 || sort_res[0].second < filtering_threshold_) {
            filtered_cands.clear();
        } else if (sort_res.size() > 1
                && sort_res[0].second > weight_priority_threshold_ * sort_res[1].second) {
            filtered_cands.clear();
            filtered_cands.insert(sort_res[0].first);
        }
        EdgeContainer result;
        for (auto it = edges.begin(); it != edges.end(); ++it) {
            if (filtered_cands.find(it->e_) != filtered_cands.end()) {
                result.push_back(*it);
            }
        }
        return result;
    }

private:
    void FindAllUniqueEdges() {
        DEBUG("Looking for unique edges");
        for (auto iter = g_.SmartEdgeBegin(); !iter.IsEnd(); ++iter) {
            if (UniqueEdge(*iter)) {
                unique_edges_.insert(*iter);
                unique_edges_.insert(g_.conjugate(*iter));
            }
        }
        unique_edges_founded_ = true;
        DEBUG("Unique edges are found");
    }

    bool UniqueBackPath(const BidirectionalPath& path, size_t pos) const {
        int int_pos = (int) pos;
        while (int_pos >= 0) {
            if (unique_edges_.count(path.At(int_pos)) > 0)
                return true;
            int_pos--;
        }
        return false;
    }

    bool UniqueEdge(EdgeId e) const {
        if (g_.length(e) > cfg::get().max_repeat_length)
            return true;
        DEBUG("Analyze unique edge " << g_.int_id(e));
        auto cov_paths = coverage_map_.GetCoveringPaths(e);
        TRACE("***start***" << cov_paths.size() <<"***");
        /*for (auto it1 = cov_paths.begin(); it1 != cov_paths.end(); ++it1) {
            (*it1)->Print();
        }*/

        for (auto it1 = cov_paths.begin(); it1 != cov_paths.end(); ++it1) {
            auto pos1 = (*it1)->FindAll(e);
            if (pos1.size() > 1) {
                DEBUG("***not unique " << g_.int_id(e) << " len " << g_.length(e) << "***");
                return false;
            }
            for (auto it2 = it1; it2 != cov_paths.end(); it2++) {
                auto pos2 = (*it2)->FindAll(e);
                if (pos2.size() > 1) {
                    DEBUG("***not unique " << g_.int_id(e) << " len " << g_.length(e) << "***");
                    return false;
                }
                if (!ConsistentPath(**it1, pos1[0], **it2, pos2[0])) {
                    if (CheckInconsistence(**it1, pos1[0], **it2, pos2[0],
                                           cov_paths)) {
                        DEBUG("***not unique " << g_.int_id(e) << " len " << g_.length(e) << "***");
                        return false;
                    }
                }
            }
        }DEBUG("***edge " << g_.int_id(e) << " is unique.***");
        return true;
    }

    bool ConsistentPath(const BidirectionalPath& path1, size_t pos1,
                        const BidirectionalPath& path2, size_t pos2) const {
        return EqualBegins(path1, pos1, path2, pos2)
                && EqualEnds(path1, pos1, path2, pos2);
    }

    bool SignificantlyDiffWeights(double w1, double w2) const {
        if (w1 > filtering_threshold_ and w2 > filtering_threshold_) {
            if (w1 > w2 * unique_edge_priority_threshold_
                    or w2 > w1 * unique_edge_priority_threshold_) {
                return true;
            }
            return false;
        }
        return true;
    }

    bool CheckInconsistence(
            const BidirectionalPath& path1, size_t pos1,
            const BidirectionalPath& path2, size_t pos2,
            const std::set<BidirectionalPath*>& cov_paths) const {
        int first_diff_pos1 = FirstNotEqualPosition(path1, pos1, path2, pos2);
        int first_diff_pos2 = FirstNotEqualPosition(path2, pos2, path1, pos1);
        if (first_diff_pos1 != -1) {
            const BidirectionalPath cand1 = path1.SubPath(first_diff_pos1,
                                                          pos1 + 1);
            const BidirectionalPath cand2 = path2.SubPath(first_diff_pos2,
                                                          pos2 + 1);
            std::pair<double, double> weights = GetSubPathsWeights(cand1, cand2,
                                                                   cov_paths);
            DEBUG("Not equal begin " << g_.int_id(path1.At(first_diff_pos1))
                  << " weight " << weights.first
                  << "; " << g_.int_id(path2.At(first_diff_pos2))
                  << " weight " << weights.second);
            if (!SignificantlyDiffWeights(weights.first, weights.second)) {
                DEBUG("not significantly different");
                return true;
            }
        }
        size_t last_diff_pos1 = LastNotEqualPosition(path1, pos1, path2, pos2);
        size_t last_diff_pos2 = LastNotEqualPosition(path2, pos2, path1, pos1);
        if (last_diff_pos1 != -1UL) {
            const BidirectionalPath cand1 = path1.SubPath(pos1,
                                                          last_diff_pos1 + 1);
            const BidirectionalPath cand2 = path2.SubPath(pos2,
                                                          last_diff_pos2 + 1);
            std::pair<double, double> weights = GetSubPathsWeights(cand1, cand2,
                                                                   cov_paths);
            DEBUG("Not equal end " << g_.int_id(path1.At(last_diff_pos1))
                  << " weight " << weights.first
                  << "; " << g_.int_id(path2.At(last_diff_pos2))
                  << " weight " << weights.second);
            if (!SignificantlyDiffWeights(weights.first, weights.second)) {
                DEBUG("not significantly different");
                return true;
            }
        }
        return false;
    }

    std::pair<double, double> GetSubPathsWeights(
            const BidirectionalPath& cand1, const BidirectionalPath& cand2,
            const std::set<BidirectionalPath*>& cov_paths) const {
        double weight1 = 0.0;
        double weight2 = 0.0;
        for (auto iter = cov_paths.begin(); iter != cov_paths.end(); ++iter) {
            BidirectionalPath* path = *iter;
            if (ContainSubPath(*path, cand1)) {
                weight1 += path->GetWeight();
            } else if (ContainSubPath(*path, cand2)) {
                weight2 += path->GetWeight();
            }
        }
        return std::make_pair(weight1, weight2);
    }

    bool ContainSubPath(const BidirectionalPath& path,
                        const BidirectionalPath& subpath) const {
        for (size_t i = 0; i < path.Size(); ++i) {
            if (path.CompareFrom(i, subpath)) {
                return true;
            }
        }
        return false;
    }

    vector<pair<EdgeId, double> > MapToSortVector(
            map<EdgeId, double>& map) const {
        vector<pair<EdgeId, double> > result1(map.begin(), map.end());
        std::sort(result1.begin(), result1.end(), EdgeWithWeightCompareReverse);
        return result1;
    }

    double filtering_threshold_;
    double weight_priority_threshold_;
    double unique_edge_priority_threshold_;
    GraphCoverageMap coverage_map_;
    bool unique_edges_founded_;
    std::set<EdgeId> unique_edges_;
};

class MatePairExtensionChooser : public ExtensionChooser {
public:
    MatePairExtensionChooser(const Graph& g, PairedInfoLibrary& lib,
                             const GraphCoverageMap& cov_map)
            : ExtensionChooser(g, 0, .0),
              g_(g),
              lib_(lib),
              search_dist_(lib.GetISMax()),
              weight_counter_(g, lib),
              path_searcher_(g_, cov_map, lib_.GetISMax(), weight_counter_) {
    }
    virtual EdgeContainer Filter(BidirectionalPath& path,
                                 EdgeContainer& edges) {
        if (edges.size() == 0 || path.Length() < lib_.GetISMin()) {
            return EdgeContainer();
        }
        map<EdgeId, BidirectionalPath*> best_paths;
        for (size_t iedge = 0; iedge < edges.size(); ++iedge) {
            set<BidirectionalPath*> next_paths = path_searcher_.FindNextPaths(path, edges[iedge].e_);
            vector<BidirectionalPath*> max_weighted = MaxWeightedPath(path, next_paths);
            if (max_weighted.size() == 0) {
                //TODO: delete all information
                return EdgeContainer();
            }
            best_paths[edges[iedge].e_] = new BidirectionalPath( *max_weighted[0]);
            for (auto iter = next_paths.begin(); iter != next_paths.end(); ++iter) {
                delete (*iter);
            }
        }
        set<BidirectionalPath*> next_paths;
        for (size_t iedge = 0; iedge < edges.size(); ++iedge) {
            next_paths.insert(best_paths[edges[iedge].e_]);
        }
        DEBUG("Try to choose from best paths...");
        vector<BidirectionalPath*> best_path = MaxWeightedPath(path, next_paths);
        EdgeContainer result;
        if (best_path.size() == 1) {
            result.push_back(EdgeWithDistance((*best_path.begin())->At(0), 0.0));
        } else if (best_path.size() > 1) {
            set<BidirectionalPath*> best_set(best_path.begin(), best_path.end());
            result = TryToScaffold(path, best_set);
        }
        for (auto iter = next_paths.begin(); iter != next_paths.end();
                ++iter) {
            delete (*iter);
        }
        return result;
    }
private:
    bool SignificallyDifferentEdges(const BidirectionalPath& path,
                                    const map<size_t, double>& pi1,
                                    double w1,
                                    const map<size_t, double>& pi2,
                                    double w2) const {
        size_t not_common_length = 0;
        size_t common_length = 0;
        double not_common_w1 = 0.0;
        double common_w = 0.0;
        for (auto iter = pi1.begin(); iter != pi1.end(); ++iter) {
            auto iter2 = pi2.find(iter->first);
            double w = 0.0;
            if (iter2 == pi2.end() || math::eq(iter2->second, 0.0)) {
                not_common_length += g_.length(path.At(iter->first));
            } else {
                w = min(iter2->second, iter->second);
                common_length += g_.length(path.At(iter->first));
            }
            not_common_w1 += iter->second -w;
            common_w += w;
        }
        if(common_w < 0.9*(not_common_w1 + common_w) || math::eq(w2, 0.0)){
            return true;
        } else {
           DEBUG("common pi more then 0.9");
           return false;
        }
    }

    void DeleteSmallWeights(const BidirectionalPath& path, map<BidirectionalPath*, double>& weights,
                            set<BidirectionalPath*>& paths,
                            const std::map<BidirectionalPath*, map<size_t, double> >& all_pi) const {
        double max_weight = 0.0;
        BidirectionalPath* max_path;
        for (auto iter = weights.begin(); iter != weights.end(); ++iter) {
            if (iter->second > max_weight) {
                max_weight = max(max_weight, iter->second);
                max_path = iter->first;
            }
        }
        for (auto iter = weights.begin(); iter != weights.end(); ++iter) {
            if (math::gr(max_weight, iter->second * 1.5) &&
                    SignificallyDifferentEdges(path, all_pi.find(max_path)->second, weights.find(max_path)->second,
                                               all_pi.find(iter->first)->second, weights.find(iter->first)->second))
                paths.erase(iter->first);
        }
    }

    void DeleteCommonPi(const BidirectionalPath& path,
            const std::map<BidirectionalPath*, map<size_t, double> >& all_pi) {
        weight_counter_.ClearCommonWeight();
        for (size_t i = 0; i < path.Size(); ++i) {
            double common = DBL_MAX;
            for (auto iter = all_pi.begin(); iter != all_pi.end(); ++iter) {
                if (iter->second.count(i) == 0) {
                    common = 0.0;
                    break;
                } else {
                    common = min(common, iter->second.at(i));
                }
            }
            weight_counter_.SetCommonWeightFrom(i, common);
        }
    }

    void CountAllPairInfo(
            const BidirectionalPath& path,
            const set<BidirectionalPath*>& next_paths,
            std::map<BidirectionalPath*, map<size_t, double> >& result) const {
        result.clear();
        for (BidirectionalPath* next : next_paths) {
            result[next] = weight_counter_.FindPairInfoFromPath(path, *next);
        }
    }

    map<BidirectionalPath*, double> CountWeightsAndFilter(
            const BidirectionalPath& path,
            set<BidirectionalPath*>& next_paths, bool delete_small_w) {
        std::map<BidirectionalPath*, map<size_t, double> > all_pi;
        CountAllPairInfo(path, next_paths, all_pi);
        DeleteCommonPi(path, all_pi);
        map<BidirectionalPath*, double> result;
        for (BidirectionalPath* next : next_paths) {
            result[next] = weight_counter_.CountPairInfo(path, 0, path.Size(),
                                                         *next, 0,
                                                         next->Size());
        }
        if (delete_small_w) {
            DeleteSmallWeights(path, result, next_paths, all_pi);
        }
        return result;
    }

    vector<BidirectionalPath*> SortResult(const BidirectionalPath& path,
            set<BidirectionalPath*>& next_paths) {
        map<BidirectionalPath*, double> weights = CountWeightsAndFilter(path, next_paths, false);
        vector<BidirectionalPath*> result;
        while (weights.size() > 0) {
            auto max_iter = weights.begin();
            for (auto iter = weights.begin(); iter != weights.end(); ++iter) {
                if (iter->second > max_iter->second)
                    max_iter = iter;
                if (max_iter->second != iter->second)
                    continue;
                if (!PathCompare(iter->first, max_iter->first))
                    max_iter = iter;
            }
            result.push_back(max_iter->first);
            weights.erase(max_iter);
        }
        return result;
    }

    vector<BidirectionalPath*> MaxWeightedPath(
            const BidirectionalPath& path,
            const set<BidirectionalPath*>& following_paths) {
        set<BidirectionalPath*> result(following_paths);
        set<BidirectionalPath*> prev_result;
        while (prev_result.size() != result.size()) {
            prev_result = result;
            DEBUG("iteration with paths " << result.size());
            map<BidirectionalPath*, double> weights = CountWeightsAndFilter(path, result, true);
            if (result.size() == 0)
                result = prev_result;
            if (result.size() == 1)
                break;
        }
        if (result.size() == 0) {
            INFO("bad case");
            return vector<BidirectionalPath*>();
        }
        return SortResult(path, result);
    }

    EdgeContainer TryToScaffold(const BidirectionalPath& path,
                                const set<BidirectionalPath*>& paths) {
        if (paths.size() == 0) {
            return EdgeContainer();
        }

        size_t max_common_end = 0;
        BidirectionalPath max_path(g_);
        for (auto it1 = paths.begin(); it1 != paths.end(); ++it1) {
            BidirectionalPath* path1 = *it1;
            bool contain_all = true;
            for (size_t i = 1; i < path1->Size() && contain_all; ++i) {
                BidirectionalPath subpath = path1->SubPath(path1->Size() - i);
                for (auto it2 = paths.begin(); it2 != paths.end() && contain_all; ++it2) {
                    if (!(*it2)->Contains(subpath))
                        contain_all = false;
                }
                if (contain_all && i >= max_common_end) {
                    max_common_end = i;
                    max_path.Clear();
                    max_path.PushBack(*path1);
                }
            }
        }
        if (max_path.Size() == 0) {
            return EdgeContainer();
        }
        weight_counter_.ClearCommonWeight();
        double common = weight_counter_.CountPairInfo(
                path, 0, path.Size(), max_path,
                max_path.Size() - max_common_end, max_path.Size(), false);
        double not_common = weight_counter_.CountPairInfo(
                path, 0, path.Size(), max_path, 0,
                max_path.Size() - max_common_end, false);
        DEBUG("common " << common << " not common " << not_common << " max common end " << max_common_end);
        max_path.Print();
        EdgeContainer result;
        if (common > not_common) {
            size_t to_add = max_path.Size() - max_common_end;
            size_t gap_length = max_path.Length() - max_path.LengthAt(to_add);
            DEBUG(" edge to add " << g_.int_id(max_path.At(to_add)) << " with length " << gap_length);
            result.push_back(EdgeWithDistance(max_path.At(to_add), gap_length));
        }
        return result;
    }

    const Graph& g_;
    PairedInfoLibrary& lib_;
    size_t search_dist_;
    PathsWeightCounter weight_counter_;
    NextPathSearcher path_searcher_;
};
}
#endif /* EXTENSION_HPP_ */
