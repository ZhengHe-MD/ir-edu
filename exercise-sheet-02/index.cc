// Copyright 2012 Eugen Sawin <esawin@me73.com>
#include "./index.h"
#include <unordered_map>
#include <cassert>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

using std::unordered_map;
using std::string;
using std::vector;

const size_t Index::kMinKeywordSize = 2;
const int Index::kInvalidId = -1;

auto Index::ExtractKeywords(const string& content, const size_t beg,
                            const size_t end) -> vector<PosSize> {
  using std::isalnum;

  // Keyword density approximation.
  static float density = 1.0f / kMinKeywordSize;

  const size_t content_size = content.size();
  assert(beg < end && end <= content_size);
  vector<PosSize> keywords;
  keywords.reserve(content_size * density * 1.5f);
  size_t pos = beg;
  while (pos < end) {
    // Found beginning of the next keyword (any alphanumeric character).
    if (isalnum(content[pos])) {
      bool valid = isalpha(content[pos]);
      const size_t key_beg = pos;
      ++pos;
      while (pos < end && isalnum(content[pos])) {
        valid = valid || isalpha(content[pos]);
        ++pos;
      }
      // Found end of keyword.
      const size_t keyword_size = pos - key_beg;
      // Valid keyword need at least one alphabetic character and a certain
      // minimum size.
      if (valid && keyword_size >= kMinKeywordSize) {
        keywords.push_back({key_beg, keyword_size});
      }
    }
    ++pos;
  }
  // Update density approximation.
  density = (density * 3.0f +
             keywords.size() / static_cast<float>(content_size))
            * 0.25f;
  return keywords;
}

void Index::AddRecordsFromCsv(const string& file_content,
                              Index* inverted_index) {
  const size_t content_size = file_content.size();
  string prev_url;
  int record_id = kInvalidId;
  size_t pos = 0;
  while (pos < content_size) {
    // Skip to second column after first tab.
    const size_t content_beg = file_content.find('\t', pos);
    assert(content_beg != string::npos && "CSV file has wrong format");
    const size_t content_end = file_content.find('\n', content_beg);
    assert(content_end != string::npos && "CSV file has wrong format");
    assert(content_end > content_beg);
    const string url = file_content.substr(pos, content_beg - pos);
    const string content = file_content.substr(content_beg + 1,
                                               content_end - content_beg);
    size_t offset = 0u;
    // Assuming the records are continuous within the list.
    // TODO(esawin): assert that!
    if (url != prev_url) {
      // New record found in file contents.
      record_id = inverted_index->AddRecord(url, content);
      prev_url = url;
    } else {
      // Known record, add the content.
      offset = inverted_index->ExtendRecord(record_id, content);
    }
    // Extract the keyword positions.
    const vector<PosSize> keywords = ExtractKeywords(content, 0,
                                                     content.size());
    // Add each keyword from the content to the index.
    for (auto it = keywords.cbegin(), end = keywords.cend();
         it != end; ++it) {
      // Extract the keyword string and add it to the index.
      const string keyword = content.substr(it->pos, it->size);
      inverted_index->AddItem(keyword, record_id, it->pos + offset);
    }
    pos = content_end + 1;
  }
  // TODO(esawin): Should we call CalculateScores? This would degrade this
  // function to a constructor.
}

Index::Index()
    : num_items_(0u),
      total_size_(0u) {}

void Index::ComputeScores(const float b, const float k) {
  const float num_records = NumRecords();
  const float inv_avg_record_size = num_records / TotalSize();
  for (auto it = index_.begin(), end = index_.end(); it != end; ++it) {
    const string& keyword = it->first;
    auto freq_it = record_freq_.find(keyword);
    assert(freq_it != record_freq_.end());
    assert(freq_it->second != 0u);
    const float inv_record_freq = std::log2(num_records / freq_it->second);
    vector<Item>& items = it->second;
    for (auto it2 = items.begin(), end2 = items.end(); it2 != end2; ++it2) {
      Item& item = *it2;
      const float record_size  = RecordById(item.record_id).content.size();
      item.score = item.score * (k + 1.0f) /
                   (k * (1.0f - b + b * record_size * inv_avg_record_size) +
                    item.score) * inv_record_freq;
    }
  }
}

const Index::Record& Index::RecordById(const int record_id) const {
  assert(record_id >= 0 && record_id < static_cast<int>(records_.size()));
  return records_[record_id];
}

Index::Record& Index::recordById(const int record_id) {
  assert(record_id >= 0 && record_id < static_cast<int>(records_.size()));
  return records_[record_id];
}

auto Index::Items(const string& keyword) const -> const vector<Item>& {
  static vector<Item> kEmptyList;
  string low = keyword;
  std::transform(keyword.cbegin(), keyword.cend(), low.begin(), ::tolower);
  auto const it = index_.find(low);
  if (it == index_.end()) {
    return kEmptyList;
  }
  return it->second;
}

int Index::AddRecord(const string& url, const string& content) {
  // TODO(esawin): Check for duplicates.
  records_.push_back({url, content});
  total_size_ += content.size();
  return records_.size() - 1;
}

size_t Index::ExtendRecord(const int record_id, const string& content) {
  Record& record = recordById(record_id);
  const size_t size = record.content.size();
  record.content += content;
  total_size_ += content.size();
  return size;
}

int Index::AddItem(const string& keyword, const int record_id,
                   const size_t pos) {
  string low = keyword;
  std::transform(keyword.cbegin(), keyword.cend(), low.begin(), ::tolower);
  auto it = index_.find(low);
  if (it == index_.end()) {
    // New keyword, create a new item.
    index_.insert(std::make_pair(low, vector<Item>({Item(record_id, {pos},
                                                    low.size(), 1.0f)})));
    record_freq_.insert(std::make_pair(low, 1u));
  } else {
    // Keyword already in the index.
    vector<Item>& items = it->second;
    Item& last = items.back();
    if (last.record_id == record_id) {
      // Add another keyword position within known record and increase
      // term frequency.
      last.positions.push_back(pos);
      last.score += 1.0f;
    } else {
      // Keyword occurs in a new record.
      items.push_back(Item(record_id, {pos}, low.size(), 1.0f));
      ++record_freq_[low];
    }
  }
  return ++num_items_;
}

void Index::ReserveRecords(const size_t num) {
  records_.reserve(num);
}

size_t Index::TotalSize() const {
  return total_size_;
}

size_t Index::NumRecords() const {
  return records_.size();
}

size_t Index::NumItems() const {
  return num_items_;
}

void Index::OutputInvertedListLengths() const {
  for (auto it = index_.cbegin(), end = index_.cend();
       it != end; ++it) {
    const string& word = it->first;
    const size_t size = it->second.size();
    std::cout << word << "\t" << size << std::endl;
  }
}
