// Copyright (c) 2020 Robert Vaser

#ifndef BIOSOUP_NUCLEIC_ACID_HPP_
#define BIOSOUP_NUCLEIC_ACID_HPP_

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <string>
#include <stdexcept>
#include <vector>
#include <assert.h>
#include <math.h> 

namespace biosoup {

constexpr static std::uint8_t kNucleotideCoder[] = {
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255,   0, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255,   0,   1 ,  1,   0, 255, 255,   2,
      3, 255, 255,   2, 255,   1,   0, 255,
    255, 255,   0,   1,   3,   3,   2,   0,
    255,   3, 255, 255, 255, 255, 255, 255,
    255,   0,   1,   1,   0, 255, 255,   2,
      3, 255, 255,   2, 255,   1,   0, 255,
    255, 255,   0,   1,   3,   3,   2,   0,
    255,   3, 255, 255, 255, 255, 255, 255
};

constexpr static char kNucleotideDecoder[] = {
    'A', 'C', 'G', 'T'
};

constexpr std::uint32_t const QUALITY_BLOCK_SIZE = 1;

class NucleicAcid {
 public:
  NucleicAcid() = default;

  NucleicAcid(
      const std::string& name,
      const std::string& data)
      : NucleicAcid(
          name.c_str(), name.size(),
          data.c_str(), data.size()) {}

  NucleicAcid(
      const char* name, std::uint32_t name_len,
      const char* data, std::uint32_t data_len)
      : id(num_objects++),
        name(name, name_len),
        deflated_data(),
        block_quality(),
        inflated_len(data_len),
        is_reverse_complement(0) {

    deflated_data.reserve(data_len / 32. + .999);
    std::uint64_t block = 0;
    for (std::uint32_t i = 0; i < data_len; ++i) {
      std::uint64_t c = kNucleotideCoder[static_cast<std::uint8_t>(data[i])];
      if (c == 255ULL) {
        throw std::invalid_argument(
            "[biosoup::NucleicAcid::NucleicAcid] error: not a nucleotide");
      }
      block |= c << ((i << 1) & 63);
      if (((i + 1) & 31) == 0 || i == data_len - 1) {
        deflated_data.emplace_back(block);
        block = 0;
      }
    }
  }

  NucleicAcid(
      const std::string& name,
      const std::string& data,
      const std::string& quality,
      std::uint32_t quality_block_size_=QUALITY_BLOCK_SIZE)
      : NucleicAcid(
          name.c_str(), name.size(),
          data.c_str(), data.size(),
          quality.c_str(), quality.size(),
          quality_block_size_) {}

  NucleicAcid(
      const char* name, std::uint32_t name_len,
      const char* data, std::uint32_t data_len,
      const char* quality, std::uint32_t quality_len,
      std::uint32_t quality_block_size_=QUALITY_BLOCK_SIZE)
      : NucleicAcid(
          name, name_len,
          data, data_len) {

    quality_block_size = quality_block_size_;
    assert(quality_block_size > 0);
    block_quality.reserve(static_cast<double>(quality_len) / quality_block_size + .999);

    for (std::uint32_t i = 0; i < quality_len; i += quality_block_size) {
      std::uint32_t j = std::min(i + quality_block_size, quality_len);
      auto block = std::accumulate(
          quality + i,
          quality + j,
          0,
          [] (const std::uint32_t& sum, const char& q) -> std::uint32_t {
            return sum + (q - '!');
          });
      block_quality.emplace_back(block / (j - i));
    }
  }

  NucleicAcid(const NucleicAcid&) = default;
  NucleicAcid& operator=(const NucleicAcid&) = default;

  NucleicAcid(NucleicAcid&&) = default;
  NucleicAcid& operator=(NucleicAcid&&) = default;

  ~NucleicAcid() = default;

  std::uint64_t operator[] (int index){
    return Code(index);
  }

  std::uint32_t size (){
    return inflated_len;
  }

  std::uint64_t Code(std::uint32_t i) const {
    std::uint64_t x = 0;
    if (is_reverse_complement) {
      i = inflated_len - i - 1;
      x = 3;
    }
    //i >> 5 the block in which the value is stored, one block contains 32 nucleotides
    return ((deflated_data[i >> 5] >> ((i << 1) & 63)) & 3) ^ x;
  }

  std::uint8_t Score(std::uint32_t i) const {
    if (is_reverse_complement) {
      i = inflated_len - i - 1;
    }
    return block_quality[static_cast<int>(floor(i / quality_block_size))];
  }

  std::string InflateData(std::uint32_t i = 0, std::uint32_t len = -1) const {
    if (i >= inflated_len) {
      return std::string{};
    }
    len = std::min(len, inflated_len - i);

    std::string dst{};
    dst.reserve(len);
    for (; len; ++i, --len) {
      dst += kNucleotideDecoder[Code(i)];
    }
    return dst;
  }

  std::string InflateQuality(std::uint32_t i = 0, std::uint32_t len = -1) const {  // NOLINT
    if (block_quality.empty() || i >= inflated_len) {
      return std::string{};
    }
    len = std::min(len, inflated_len - i);

    std::string dst{};
    dst.reserve(len);
    for (; len; ++i, --len) {
      dst += Score(i) + '!';
    }
    return dst;
  }

  void ReverseAndComplement() {   // Watson-Crick base pairing
    is_reverse_complement ^= 1;
  }

  std::uint32_t Id(){
    return id;
  }

  std::string Name(){
    return name;
  }

  static std::atomic<std::uint32_t> num_objects;

  std::uint32_t id;  // (optional) initialize num_objects to 0
  std::string name;
  std::vector<std::uint64_t> deflated_data;
  std::vector<std::uint8_t> block_quality;  // (optional) Phred quality scores
  std::uint32_t quality_block_size;
  std::uint32_t inflated_len;
  bool is_reverse_complement;
};

}  // namespace biosoup

#endif  // BIOSOUP_NUCLEIC_ACID_HPP_
