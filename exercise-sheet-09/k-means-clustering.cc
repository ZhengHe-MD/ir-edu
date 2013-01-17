// Copyright 2013 Eugen Sawin <esawin@me73.com>
#include "./k-means-clustering.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include "./index.h"

using std::vector;

const int KMeansClustering::kInvalidId = -1;

KMeansClustering::KMeansClustering(const Index& index)
    : index_(index) {}

void KMeansClustering::Truncate(const size_t m, vector<IdScore>* vec) {
  if (vec->size() <= m) {
    return;
  }
  std::partial_sort(vec->begin(), vec->begin() + m, vec->end(),
      [](const IdScore& lhs, const IdScore& rhs) {
    return lhs.score > rhs.score || (lhs.score == rhs.score && lhs < rhs);
  });
  vec->resize(m);
  std::sort(vec->begin(), vec->end());
}

void KMeansClustering::Normalize(vector<IdScore>* vec) {
  float norm = 0.0f;
  for (const KMeansClustering::IdScore& idsc: *vec) {
    norm += idsc.score * idsc.score;
  }
  norm = std::sqrt(norm);
  if (norm == 0.0f) {
    return;
  }
  for (IdScore& idsc: *vec) {
    idsc.score /= norm;
  }
}

float KMeansClustering::Distance(const vector<IdScore>& v1,
                                 const vector<IdScore>& v2) const {
  auto it1 = v1.begin();
  const auto end1 = v1.end();
  auto it2 = v2.begin();
  const auto end2 = v2.end();
  float dist = 0.0f;
  while (it1 != end1 && it2 != end2) {
    while (it1 != end1 && it1->id < it2->id) {
      ++it1;
    }
    if (it1 == end1) {
      break;
    }
    while (it2 != end2 && it2->id < it1->id) {
      ++it2;
    }
    while (it1 != end1 && it2 != end2 && it1->id == it2->id) {
      dist += it1->score * it2->score;
      ++it1;
      ++it2;
    }
  }
  return 1.0f - dist;
}

auto KMeansClustering::Average(const vector<int>& records) const
    -> vector<IdScore> {
  vector<IdScore> merged;
  for (const int id: records) {
    const vector<IdScore>& vec = RecordVector(id);
    merged.insert(merged.end(), vec.begin(), vec.end());
  }
  std::sort(merged.begin(), merged.end());
  vector<IdScore> avg;
  for (const IdScore& idsc: merged) {
    if (avg.size() && avg.back().id == idsc.id) {
      // Same record, increase sum.
      avg.back().score += idsc.score;
    } else {
      avg.push_back(idsc);
    }
  }
  const int num_records = records.size();
  for (IdScore& idsc: avg) {
    idsc.score /= num_records;
  }
  return avg;
}

void KMeansClustering::ConstructMatrix() {
  const int num_records = index_.NumRecords();
  const int num_keywords = index_.NumKeywords();
  record_matrix_.resize(num_records);
  for (int k = 0; k < num_keywords; ++k) {
    const Index::Keyword& keyword = index_.KeywordById(k);
    for (const Index::Item& item: keyword.items) {
      record_matrix_[item.record_id].push_back({k, item.score});
    }
  }
}

int KMeansClustering::NextFarthestCentroid(
    const vector<vector<IdScore> >& centroids, vector<float>* dists) const {
  const size_t num_records = record_matrix_.size();
  int farthest_id = 0;
  float farthest_dist = 0.0f;
  for (size_t r = 0; r < num_records; ++r) {
    float closest_dist = std::numeric_limits<float>::max();
    for (const vector<IdScore>& vec: centroids) {
      const float dist = Distance(RecordVector(r), vec);
      closest_dist = std::min(closest_dist, dist);
    }
    if (dists) {
      (*dists)[r] = closest_dist;
    }
    if (closest_dist > farthest_dist) {
      farthest_id = r;
      farthest_dist = closest_dist;
    }
  }
  return farthest_id;
}

auto KMeansClustering::PPCentroids(const size_t k) const
    -> vector<vector<IdScore> > {
  std::mt19937 engine;
  engine.seed(7355);
  const size_t num_records = record_matrix_.size();
  vector<vector<IdScore> > centroids;
  centroids.reserve(k);
  const int initial_centroid = ((num_records % 73) * 253) % num_records;
  centroids.push_back(RecordVector(initial_centroid));
  vector<float> probs;
  probs.reserve(num_records);
  for (size_t c = 1; c < k; ++c) {
    vector<float> dists(num_records);
    NextFarthestCentroid(centroids, &dists);
    probs.clear();
    for (size_t r = 0; r < num_records; ++r) {
      const float prob = 10000.0f * dists[r] * dists[r] +
          (probs.size() ? probs.back() : 0.0f);
      probs.push_back(prob);  
    }
    const float next = std::uniform_real_distribution<float>(0,
        probs.back())(engine);
    std::cout << "\rSeeded " << c << "/" << k
              << ": " << next << "/" << probs.back()
              << "     " << std::flush;
    int r = 0;
    while (probs[r] < next) {
      ++r;
    }
    centroids.push_back(RecordVector(r));
  }
  std::cout << "\r                                        ";
  return centroids;
}

auto KMeansClustering::FarthestCentroids(const size_t k) const
    -> vector<vector<IdScore> > {
  const size_t num_records = record_matrix_.size();
  vector<vector<IdScore> > centroids;
  centroids.reserve(k);
  const int initial_centroid = ((num_records % 73) * 253) % num_records;
  centroids.push_back(RecordVector(initial_centroid));
  for (size_t c = 1; c < k; ++c) {
    const int next_id = NextFarthestCentroid(centroids);
    centroids.push_back(RecordVector(next_id));
  }
  return centroids;
}

auto KMeansClustering::RandomCentroids(const size_t k) const
    -> vector<vector<IdScore> > {
  vector<vector<IdScore> > centroids;
  centroids.reserve(k);
  const size_t step = record_matrix_.size() / k;
  for (size_t i = 0; i < k; ++i) {
    // That's random.
    centroids.push_back(record_matrix_[i * step]);
  }
  return centroids;
}

void KMeansClustering::ComputeClustering(
    const size_t k, const size_t m, const float min_roc,
    const size_t max_num_iter) {
  for (vector<IdScore>& vec: record_matrix_) {
    // Truncate(m, &vec);
    Normalize(&vec);
  }
  centroids_ = PPCentroids(k);
  // centroids_ = FarthestCentroids(k);
  // centroids_ = RandomCentroids(k);
  for (vector<IdScore>& vec: centroids_) {
    Truncate(m, &vec);
    Normalize(&vec);
  }
  size_t num_records = record_matrix_.size();
  size_t num_iter = 0;
  float prev_rss = std::numeric_limits<float>::max();
  while (num_iter++ < max_num_iter) {
    float rss = 0.0f;
    clusters_.clear();
    clusters_.resize(k);
    for (size_t r = 0; r < num_records; ++r) {
      int best_id = 0;
      float best_dist = std::numeric_limits<float>::max();
      for (size_t c = 0; c < k; ++c) {
        const float dist = Distance(record_matrix_[r], centroids_[c]);
        if (dist < best_dist) {
          best_dist = dist;
          best_id = c;
        }
      }
      clusters_[best_id].push_back(r);
      rss += best_dist * best_dist;
    }
    assert(rss <= prev_rss);
    if (prev_rss - rss < min_roc) {
      break;
    }
    std::cout << "\rIteration: " << num_iter
              << "; RSS: " << rss << "    " << std::flush;
    prev_rss = rss;
    for (size_t c = 0; c < k; ++c) {
      if (clusters_[c].size()) {
        centroids_[c] = Average(clusters_[c]);
      } else {
        centroids_[c] = RecordVector(NextFarthestCentroid(centroids_));
      }
      Truncate(m, &centroids_[c]);
      Normalize(&centroids_[c]);
    }
  }
  std::cout << std::endl;
}

auto KMeansClustering::Centroid(const int id) const -> const vector<IdScore>& {
  assert(id > -1 && id < static_cast<int>(centroids_.size()));
  return centroids_[id];
}

auto KMeansClustering::Cluster(const int id) const -> const vector<int>& {
  assert(id > -1 && id < static_cast<int>(cluster_.size()));
  return clusters_[id];
}

auto KMeansClustering::RecordVector(const int record_id) const
    -> const vector<IdScore>& {
  assert(record_id > -1 && record_id < static_cast<int>(record_matrix_.size()));
  return record_matrix_[record_id];
}
