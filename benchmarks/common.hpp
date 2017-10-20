#pragma once

#include "log.hpp"

#include <hnsw/distance.hpp>
#include <hnsw/index.hpp>
#include <hnsw/key_mapper.hpp>

#include <boost/optional.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>


using random_t = std::minstd_rand;

using vector_t = std::vector<float>;
using dataset_t = std::vector<std::pair<std::string, vector_t>>;


inline void shuffle(dataset_t &vectors, random_t &random) {
    std::shuffle(vectors.begin(), vectors.end(), random);
}


inline void normalize(dataset_t &vectors) {
    for (auto &v: vectors) {
        float coef = 1.0f / std::sqrt(hnsw::detail::dot_product(v.second.data(), v.second.data(), v.second.size()));

        for (auto &x: v.second) {
            x *= coef;
        }
    }
}


inline size_t get_control_size(const dataset_t &vectors, boost::optional<size_t> size) {
    if (size) {
        return *size;
    } else {
        return std::min(vectors.size(), std::max<size_t>(1, vectors.size() / 100));
    }
}

inline void split_dataset(dataset_t &main, dataset_t &control, size_t control_size) {
    control.assign(main.begin(), main.begin() + control_size);

    dataset_t new_main(main.begin() + control_size, main.end());
    main = std::move(new_main);
}


struct index_t {
    virtual ~index_t() { }

    virtual void insert(const std::string &key, const vector_t &target) = 0;
    virtual void remove(const std::string &key) = 0;
    virtual std::vector<std::pair<std::string, float>> search(const vector_t &target, size_t neighbors) const = 0;
    virtual bool check() const = 0;
    virtual size_t size() const = 0;

    virtual void prepare_dataset(dataset_t &dataset) const = 0;
};


template<class Index, bool NormalizeDataset>
struct hnsw_index : index_t {
    Index wrapped;

    void insert(const std::string &key, const vector_t &target) override {
        wrapped.insert(key, target);
    }

    void remove(const std::string &key) override {
        wrapped.remove(key);
    }

    std::vector<std::pair<std::string, float>> search(const vector_t &target, size_t neighbors) const override {
        auto r = wrapped.search(target, neighbors);

        std::vector<std::pair<std::string, float>> result;
        result.reserve(r.size());

        for (auto &x: r) {
            result.emplace_back(std::move(x.key), x.distance);
        }

        return result;
    }

    bool check() const override {
        return wrapped.check();
    }

    size_t size() const override {
        return wrapped.index.nodes.size();
    }

    void prepare_dataset(dataset_t &dataset) const override {
        if (NormalizeDataset) {
            normalize(dataset);
        }
    }
};


inline std::unique_ptr<index_t>
make_index(std::string type,
           boost::optional<size_t> max_links,
           boost::optional<size_t> ef_construction,
           boost::optional<std::string> insert_method,
           boost::optional<std::string> remove_method)
{
    hnsw::index_options_t options;

    if (max_links) {
        options.max_links = *max_links;
    }

    if (ef_construction) {
        options.ef_construction = *ef_construction;
    }

    if (insert_method && *insert_method == "link_nearest") {
        options.insert_method = hnsw::index_options_t::insert_method_t::link_nearest;
    } else if (insert_method && *insert_method == "link_diverse") {
        options.insert_method = hnsw::index_options_t::insert_method_t::link_diverse;
    } else if (insert_method) {
        throw std::runtime_error("make_index: unknown insert method: " + *insert_method);
    }

    if (remove_method && *remove_method == "no_link") {
        options.remove_method = hnsw::index_options_t::remove_method_t::no_link;
    } else if (remove_method && *remove_method == "compensate_incomming_links") {
        options.remove_method = hnsw::index_options_t::remove_method_t::compensate_incomming_links;
    } else if (remove_method) {
        throw std::runtime_error("make_index: unknown remove method: " + *insert_method);
    }

    if (type == "dot_product") {
        using hnsw_index_t = hnsw::key_mapper<std::string, hnsw::hnsw_index<uint32_t, vector_t, hnsw::dot_product_distance_t>>;
        auto index = std::make_unique<hnsw_index<hnsw_index_t, true>>();
        index->wrapped.index.options = options;
        return std::unique_ptr<index_t>(std::move(index));
    } else if (type == "cosine") {
        using hnsw_index_t = hnsw::key_mapper<std::string, hnsw::hnsw_index<uint32_t, vector_t, hnsw::cosine_distance_t>>;
        auto index = std::make_unique<hnsw_index<hnsw_index_t, false>>();
        index->wrapped.index.options = options;
        return std::unique_ptr<index_t>(std::move(index));
    } else {
        throw std::runtime_error("make_index: unknown index type: " + type);
    }
}
