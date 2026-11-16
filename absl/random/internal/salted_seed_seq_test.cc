// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/random/internal/salted_seed_seq.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/random/internal/seed_material.h"

using absl::random_internal::GetSaltMaterial;
using absl::random_internal::MakeSaltedSeedSeq;
using absl::random_internal::SaltedSeedSeq;
using testing::Eq;
using testing::Pointwise;

namespace {

template <typename Sseq>
void ConformsToInterface() {
  // Check that the SeedSequence can be default-constructed.
  {
    Sseq default_constructed_seq;
  }
  // Check that the SeedSequence can be constructed with two iterators.
  {
    uint32_t init_array[] = {1, 3, 5, 7, 9};
    Sseq iterator_constructed_seq(std::begin(init_array), std::end(init_array));
  }
  // Check that the SeedSequence can be std::initializer_list-constructed.
  {
    Sseq list_constructed_seq = {1, 3, 5, 7, 9, 11, 13};
  }
  // Check that param() and size() return state provided to constructor.
  {
    uint32_t init_array[] = {1, 2, 3, 4, 5};
    Sseq seq(std::begin(init_array), std::end(init_array));
    EXPECT_EQ(seq.size(), std::size(init_array));

    std::vector<uint32_t> state_vector;
    seq.param(std::back_inserter(state_vector));

    EXPECT_EQ(state_vector.size(), std::size(init_array));
    for (int i = 0; i < state_vector.size(); i++) {
      EXPECT_EQ(state_vector[i], i + 1);
    }
  }
  // Check for presence of generate() method.
  {
    Sseq seq;
    uint32_t seeds[5];

    seq.generate(std::begin(seeds), std::end(seeds));
  }
}

TEST(SaltedSeedSeq, CheckInterfaces) {
  // Control case
  ConformsToInterface<std::seed_seq>();

  // Abseil classes
  ConformsToInterface<SaltedSeedSeq<std::seed_seq>>();
}

TEST(SaltedSeedSeq, CheckConstructingFromOtherSequence) {
  std::vector<uint32_t> seed_values(10, 1);
  std::seed_seq seq(seed_values.begin(), seed_values.end());
  auto salted_seq = MakeSaltedSeedSeq(std::move(seq));

  EXPECT_EQ(seq.size(), salted_seq.size());

  std::vector<uint32_t> param_result;
  seq.param(std::back_inserter(param_result));

  EXPECT_EQ(seed_values, param_result);
}

TEST(SaltedSeedSeq, SaltedSaltedSeedSeqIsNotDoubleSalted) {
  uint32_t init[] = {1, 3, 5, 7, 9};

  std::seed_seq seq(std::begin(init), std::end(init));

  // The first salting.
  SaltedSeedSeq<std::seed_seq> salted_seq = MakeSaltedSeedSeq(std::move(seq));
  uint32_t a[16];
  salted_seq.generate(std::begin(a), std::end(a));

  // The second salting (rvalue).
  SaltedSeedSeq<std::seed_seq> salted_salted_seq =
      MakeSaltedSeedSeq(std::move(salted_seq));
  uint32_t b[16];
  salted_salted_seq.generate(std::begin(b), std::end(b));

  // ... both should be equal.
  EXPECT_THAT(b, Pointwise(Eq(), a)) << "a[0] " << a[0];

  // Lvalue SaltedSeedSeq must also not be double-salted.
  auto lvalue_salted_seq = MakeSaltedSeedSeq(salted_salted_seq);
  uint32_t c[16];
  lvalue_salted_seq.generate(std::begin(c), std::end(c));
  EXPECT_THAT(c, Pointwise(Eq(), a));

  // Const lvalue and const rvalue SaltedSeedSeq must also not be double-salted.
  const SaltedSeedSeq<std::seed_seq> const_salted_seq(std::begin(init),
                                                      std::end(init));
  auto from_const_lvalue = MakeSaltedSeedSeq(const_salted_seq);
  uint32_t e[16];
  from_const_lvalue.generate(std::begin(e), std::end(e));
  EXPECT_THAT(e, Pointwise(Eq(), a));

  auto from_const_rvalue = MakeSaltedSeedSeq(std::move(const_salted_seq));
  uint32_t f[16];
  from_const_rvalue.generate(std::begin(f), std::end(f));
  EXPECT_THAT(f, Pointwise(Eq(), a));
}

TEST(SaltedSeedSeq, BraceMoveInvokesMoveConstructor) {
  SaltedSeedSeq<std::seed_seq> expected_seq{1, 3, 5, 7, 9};
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  SaltedSeedSeq<std::seed_seq> source{1, 3, 5, 7, 9};
  SaltedSeedSeq<std::seed_seq> brace_moved{std::move(source)};
  uint32_t actual[16];
  brace_moved.generate(std::begin(actual), std::end(actual));
  EXPECT_THAT(actual, Pointwise(Eq(), expected));
}

TEST(SaltedSeedSeq, MakeSaltedSeedSeqReturnsOwningFromNonOwningRef) {
  uint32_t init[] = {1, 3, 5, 7, 9};
  SaltedSeedSeq<std::seed_seq> expected_seq(std::begin(init), std::end(init));
  uint32_t expected[16];
  expected_seq.generate(std::begin(expected), std::end(expected));

  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_mutable;
  std::optional<SaltedSeedSeq<std::seed_seq>> owned_from_const;
  {
    std::seed_seq raw(std::begin(init), std::end(init));
    auto made_mutable = MakeSaltedSeedSeq(SaltedSeedSeq<std::seed_seq&>(raw));
    static_assert(
        std::is_same_v<decltype(made_mutable), SaltedSeedSeq<std::seed_seq>>);
    owned_from_mutable.emplace(std::move(made_mutable));

    const std::seed_seq const_raw(std::begin(init), std::end(init));
    auto made_const =
        MakeSaltedSeedSeq(SaltedSeedSeq<const std::seed_seq&>(const_raw));
    static_assert(
        std::is_same_v<decltype(made_const), SaltedSeedSeq<std::seed_seq>>);
    owned_from_const.emplace(std::move(made_const));
  }

  uint32_t from_mutable[16];
  owned_from_mutable->generate(std::begin(from_mutable),
                               std::end(from_mutable));
  EXPECT_THAT(from_mutable, Pointwise(Eq(), expected));

  uint32_t from_const[16];
  owned_from_const->generate(std::begin(from_const), std::end(from_const));
  EXPECT_THAT(from_const, Pointwise(Eq(), expected));
}

TEST(SaltedSeedSeq, ConstructorConstraints) {
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<int>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<uint32_t>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                        std::initializer_list<uint64_t>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                         std::initializer_list<double>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq>,
                                         std::initializer_list<const char*>>);
  static_assert(std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, uint32_t*,
                                        uint32_t*>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, double*, double*>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, double, double>);
  static_assert(
      !std::is_constructible_v<SaltedSeedSeq<std::seed_seq>, void*, void*>);

  static_assert(
      !std::is_default_constructible_v<SaltedSeedSeq<std::seed_seq&>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>,
                                         std::initializer_list<uint32_t>>);
  static_assert(!std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>,
                                         uint32_t*, uint32_t*>);
  static_assert(
      std::is_constructible_v<SaltedSeedSeq<std::seed_seq&>, std::seed_seq&>);

  SaltedSeedSeq<std::seed_seq> from_u32{1u, 2u, 3u};
  EXPECT_EQ(from_u32.size(), 3u);

  SaltedSeedSeq<std::seed_seq> from_u64{uint64_t{1}, uint64_t{2}};
  EXPECT_EQ(from_u64.size(), 2u);
}

TEST(SaltedSeedSeq, SeedMaterialIsSalted) {
  const size_t kNumBlocks = 16;

  uint32_t seed_material[kNumBlocks];
  std::random_device urandom{"/dev/urandom"};
  for (uint32_t& seed : seed_material) {
    seed = urandom();
  }

  std::seed_seq seq(std::begin(seed_material), std::end(seed_material));
  SaltedSeedSeq<std::seed_seq> salted_seq(std::begin(seed_material),
                                          std::end(seed_material));

  bool salt_is_available = GetSaltMaterial().has_value();

  // If salt is available generated sequence should be different.
  if (salt_is_available) {
    uint32_t outputs[kNumBlocks];
    uint32_t salted_outputs[kNumBlocks];

    seq.generate(std::begin(outputs), std::end(outputs));
    salted_seq.generate(std::begin(salted_outputs), std::end(salted_outputs));

    EXPECT_THAT(outputs, Pointwise(testing::Ne(), salted_outputs));
  }
}

TEST(SaltedSeedSeq, GenerateAcceptsDifferentTypes) {
  const size_t kNumBlocks = 4;

  SaltedSeedSeq<std::seed_seq> seq({1, 2, 3});

  uint32_t expected[kNumBlocks];
  seq.generate(std::begin(expected), std::end(expected));

  // 32-bit outputs
  {
    unsigned long seed_material[kNumBlocks];  // NOLINT(runtime/int)
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }
  {
    unsigned int seed_material[kNumBlocks];  // NOLINT(runtime/int)
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }

  // 64-bit outputs.
  {
    uint64_t seed_material[kNumBlocks];
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }
  {
    int64_t seed_material[kNumBlocks];
    seq.generate(std::begin(seed_material), std::end(seed_material));
    EXPECT_THAT(seed_material, Pointwise(Eq(), expected));
  }
}

}  // namespace
