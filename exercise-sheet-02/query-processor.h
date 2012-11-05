// Copyright 2012 Eugen Sawin <esawin@me73.com>
#ifndef EXERCISE_SHEET_02_QUERY_PROCESSOR_H_
#define EXERCISE_SHEET_02_QUERY_PROCESSOR_H_

#include <string>
#include <vector>
#include "./inverted-index.h"
#include "./clock.h"

// Query processor based on an inverted index.
class QueryProcessor {
 public:
  // Initializes the query processor for given index.
  explicit QueryProcessor(const Index& index);

  // Returns the matching record ids for given query.
  // The items are sorted by record id, with one item for each keyword
  // occurrence (therefore possibly multiple items per record id).
  // Note: the max number of records argument is currently ignored.
  std::vector<Index::Item> Answer(const std::string& query,
                                  const int max_num_records) const;

  // Returns the number of records found in the last call to Answer.
  size_t LastRecordsFound() const;

  // Returns the duration of the last query processing in microseconds.
  Clock::Diff LastDuration() const;

 private:
  // Intersects inverted lists and returns the result list.
  std::vector<Index::Item> Intersect(
      const std::vector<const std::vector<Index::Item>*>& lists,
      const int max_num) const;

  const Index& index_;
  mutable size_t last_num_records_;
  mutable Clock::Diff last_duration_;
};

#endif  // EXERCISE_SHEET_02_QUERY_PROCESSOR_H_
