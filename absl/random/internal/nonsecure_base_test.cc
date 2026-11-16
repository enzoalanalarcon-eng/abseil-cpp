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

#include "absl/random/internal/nonsecure_base.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <thread>  // NOLINT
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"

namespace {

using ExampleNonsecureURBG =
    absl::random_internal::NonsecureURBGBase<std::mt19937>;

template <typename T>
void Use(const T&) {}

}  // namespace

TEST(NonsecureURBGBase, DefaultConstructorIsValid) {
  ExampleNonsecureURBG urbg;
}

// Ensure that the recommended template-instantiations are valid.
TEST(RecommendedTemplates, CanBeConstructed) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;
}

TEST(RecommendedTemplates, CanDiscardValues) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;

  default_generator.discard(5);
  insecure_generator.discard(5);
}

TEST(NonsecureURBGBase, StandardInterface) {
  // Names after definition of [rand.req.urbg] in C++ standard.
  // e us a value of E
  // v is a lvalue of E
  // x, y are possibly const values of E
  // s is a value of T
  // q is a value satisfying requirements of seed_sequence
  // z is a value of type unsigned long long
  // os is a some specialization of basic_ostream
  // is is a some specialization of basic_istream

  using E = absl::random_internal::NonsecureURBGBase<std::minstd_rand>;

  using T = typename E::result_type;

  static_assert(!std::is_copy_constructible_v<E>,
                "NonsecureURBGBase should not be copy constructible");
  static_assert(!std::is_constructible_v<E, E&>,
                "NonsecureURBGBase should not be constructible from non-const "
                "lvalue reference");
  static_assert(!std::is_constructible_v<E, int>,
                "NonsecureURBGBase should not be constructible from int");
  static_assert(!std::is_constructible_v<E, T>,
                "NonsecureURBGBase should not be constructible from "
                "result_type");
  static_assert(!std::is_constructible_v<absl::BitGen, int>,
                "BitGen should not be constructible from int");
  static_assert(!std::is_constructible_v<absl::BitGen, absl::BitGen&>,
                "BitGen should not be constructible from BitGen&");

  static_assert(!std::is_copy_assignable_v<E>,
                "NonsecureURBGBase should not be copy assignable");

  static_assert(std::is_move_constructible_v<E>,
                "NonsecureURBGBase should be move constructible");

  static_assert(std::is_move_assignable_v<E>,
                "NonsecureURBGBase should be move assignable");

  static_assert(std::is_same_v<decltype(std::declval<E>()()), T>,
                "return type of operator() must be result_type");

  {
    const E x, y;
    Use(x);
    Use(y);

    static_assert(std::is_same_v<decltype(x == y), bool>,
                  "return type of operator== must be bool");

    static_assert(std::is_same_v<decltype(x != y), bool>,
                  "return type of operator== must be bool");
  }

  E e;
  std::seed_seq q{1, 2, 3};

  E{};
  E{q};

  // Copy constructor not supported.
  // E{x};

  // result_type seed constructor not supported.
  // E{T{1}};

  // Move constructors are supported.
  {
    E tmp(q);
    E m = std::move(tmp);
    E n(std::move(m));
    EXPECT_TRUE(e != n);
  }

  // Comparisons work.
  {
    // MSVC emits error 2718 when using EXPECT_EQ(e, x)
    //  * actual parameter with __declspec(align('#')) won't be aligned
    E a(q);
    E b(q);

    EXPECT_TRUE(a != e);
    EXPECT_TRUE(a == b);

    a();
    EXPECT_TRUE(a != b);
  }

  // e.seed(s) not supported.

  // [rand.req.eng] specifies the parameter as 'unsigned long long'
  // e.discard(unsigned long long) is supported.
  unsigned long long z = 1;  // NOLINT(runtime/int)
  e.discard(z);
}

TEST(NonsecureURBGBase, SeedSeqConstructorIsValid) {
  std::seed_seq seq;
  ExampleNonsecureURBG rbg(seq);
}

TEST(NonsecureURBGBase, CompatibleWithDistributionUtils) {
  ExampleNonsecureURBG rbg;

  absl::Uniform(rbg, 0, 100);
  absl::Uniform(rbg, 0.5, 0.7);
  absl::Poisson<uint32_t>(rbg);
  absl::Exponential<float>(rbg);
}

TEST(NonsecureURBGBase, CompatibleWithStdDistributions) {
  ExampleNonsecureURBG rbg;

  // Cast to void to suppress [[nodiscard]] warnings
  static_cast<void>(std::uniform_int_distribution<uint32_t>(0, 100)(rbg));
  static_cast<void>(std::uniform_real_distribution<float>()(rbg));
  static_cast<void>(std::bernoulli_distribution(0.2)(rbg));
}

TEST(NonsecureURBGBase, ConsecutiveDefaultInstancesYieldUniqueVariates) {
  const size_t kNumSamples = 128;

  ExampleNonsecureURBG rbg1;
  ExampleNonsecureURBG rbg2;

  for (size_t i = 0; i < kNumSamples; i++) {
    EXPECT_NE(rbg1(), rbg2());
  }
}

TEST(NonsecureURBGBase, EqualSeedSequencesYieldEqualVariates) {
  std::seed_seq seq;

  ExampleNonsecureURBG rbg1(seq);
  ExampleNonsecureURBG rbg2(seq);

  // ExampleNonsecureURBG rbg3({1, 2, 3});  // Should not compile.

  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }

  rbg1.discard(100);
  rbg2.discard(100);

  // The sequences should continue after discarding
  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }
}

TEST(NonsecureURBGBase, DistinctSequencesPerThread) {
  constexpr int kNumThreads = 12;
  constexpr size_t kValuesPerThread = 32;
  using result_type = absl::BitGen::result_type;

  // Acquire initial sequences from multiple threads.
  std::vector<std::vector<result_type>> data;
  {
    absl::Mutex mu;
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
      threads.emplace_back([&]() {
        absl::BitGen gen;

        std::vector<result_type> v(kValuesPerThread);
        absl::c_generate(v, [&]() { return gen(); });
        absl::MutexLock l(mu);
        data.push_back(std::move(v));
      });
    }
    for (auto& t : threads) t.join();
  }

  EXPECT_EQ(data.size(), kNumThreads);

  // There should be essentially no duplicates in the sequences.
  size_t expected_size = 0;
  absl::flat_hash_set<result_type> seen;
  for (const auto& v : data) {
    expected_size += v.size();
    for (result_type x : v) seen.insert(x);
  }
  EXPECT_GE(seen.size(), expected_size - 1);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU64) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint64_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
  EXPECT_FALSE((state[0] >> 32) == 0 && (state[1] >> 32) == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForS32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  int32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForVector) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  std::vector<uint32_t> state(2);
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SizeIsConstAndZero) {
  const absl::random_internal::RandenPoolSeedSeq const_seeder;
  EXPECT_EQ(const_seeder.size(), 0u);
}

TEST(NonsecureURBGBase, LvalueAndRvalueSaltedSeedSeqMatch) {
  absl::SeedSeq s1{1, 2, 3, 4};
  absl::SeedSeq s2{1, 2, 3, 4};

  absl::BitGen gen_lvalue(s1);
  absl::BitGen gen_rvalue(std::move(s2));
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(gen_lvalue(), gen_rvalue());
  }
}

namespace {
template <typename URBG>
std::array<typename URBG::result_type, 16> GenerateSequence(URBG& gen) {
  std::array<typename URBG::result_type, 16> out;
  for (auto& value : out) {
    value = gen();
  }
  return out;
}

template <typename ResultT, bool kConstGenerate = true, uint32_t kStart = 1,
          bool kConstSize = true, bool kHasSize = true>
struct TestSeedSeq {
  using result_type = ResultT;
  size_t count = 4;
  uint32_t start = kStart;

  explicit TestSeedSeq(size_t n = 4) : count(n) {}

  template <bool H = kHasSize, bool C = kConstSize,
            std::enable_if_t<H && C, int> = 0>
  size_t size() const {
    return count;
  }
  template <bool H = kHasSize, bool C = kConstSize,
            std::enable_if_t<H && !C, int> = 0>
  size_t size() {
    return count;
  }

  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<C, int> = 0>
  void generate(OutIt begin, OutIt end) const {
    uint32_t v = start;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<!C, int> = 0>
  void generate(OutIt begin, OutIt end) {
    uint32_t v = start;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
};

struct NoResultTypeSeedSeq {
  size_t size() const { return 2; }
  template <typename OutIt>
  void generate(OutIt begin, OutIt end) const {
    uint32_t v = 1;
    for (OutIt it = begin; it != end; ++it) *it = v++;
  }
};

template <bool kConstGenerate>
struct ZeroSizeCounterSeedSeq {
  using result_type = uint32_t;
  mutable uint32_t counter = 1;
  size_t size() const { return 0; }
  template <typename OutIt>
  void param(OutIt) const {}
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<C, int> = 0>
  void generate(OutIt begin, OutIt end) const {
    for (OutIt it = begin; it != end; ++it) *it = counter++;
  }
  template <typename OutIt, bool C = kConstGenerate,
            std::enable_if_t<!C, int> = 0>
  void generate(OutIt begin, OutIt end) {
    for (OutIt it = begin; it != end; ++it) *it = counter++;
  }
};

template <bool kConstGenerate, bool kHasSize>
struct TestParamSeedSeq
    : TestSeedSeq<uint32_t, kConstGenerate, 1, /*kConstSize=*/false, kHasSize> {
  std::vector<uint32_t> values;
  TestParamSeedSeq() = default;
  template <typename It>
  TestParamSeedSeq(It begin, It end) : values(begin, end) {
    this->count = values.size();
    this->start = values.empty() ? 1u : values[0];
  }
  template <typename OutIt>
  void param(OutIt out) const {
    std::copy(values.begin(), values.end(), out);
  }
};

using StatefulZeroSizeSeedSeq = ZeroSizeCounterSeedSeq<false>;
using ConstGenerateZeroSizeSeedSeq = ZeroSizeCounterSeedSeq<true>;
using SeedSeq64 = TestSeedSeq<uint64_t, /*kConstGenerate=*/false, 0xdeadbeefu>;
using SeedSeq32 = TestSeedSeq<uint32_t, /*kConstGenerate=*/false, 0xdeadbeefu>;
using SignedResultTypeSeedSeq = TestSeedSeq<int32_t>;
using NarrowResultTypeSeedSeq = TestSeedSeq<uint16_t>;
using VoidResultTypeSeedSeq = TestSeedSeq<void>;
using ConstGenerateNonConstSizeSeedSeq =
    TestSeedSeq<uint32_t, /*kConstGenerate=*/true, 1, /*kConstSize=*/false>;
using ConstGenerateNoSizeSeedSeq =
    TestSeedSeq<uint32_t, /*kConstGenerate=*/true, 1, /*kConstSize=*/true,
                /*kHasSize=*/false>;
using ConstGenerateNonConstSizeWithParamSeedSeq = TestParamSeedSeq<true, true>;
using ConstParamNoSizeSeedSeq = TestParamSeedSeq<false, false>;
}  // namespace

TEST(NonsecureURBGBase, SeedSeqConstructibilityTraits) {
  static_assert(std::is_constructible_v<absl::BitGen, absl::SeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, absl::SeedSeq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, const absl::SeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, const absl::SeedSeq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, std::seed_seq&>);
  static_assert(std::is_constructible_v<absl::BitGen, std::seed_seq&&>);
  static_assert(std::is_constructible_v<absl::BitGen, const std::seed_seq&>);
  static_assert(std::is_constructible_v<absl::BitGen, const std::seed_seq&&>);
  static_assert(
      std::is_constructible_v<absl::BitGen, StatefulZeroSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const StatefulZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen,
                                        const ConstGenerateZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::BitGen, SeedSeq64&>);
  static_assert(std::is_constructible_v<absl::BitGen, SeedSeq64&&>);
  static_assert(!std::is_constructible_v<absl::BitGen, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, NarrowResultTypeSeedSeq&>);
  static_assert(!std::is_constructible_v<absl::BitGen, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::BitGen, ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen,
                               const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, ConstGenerateNoSizeSeedSeq&>);
  static_assert(!std::is_constructible_v<absl::BitGen,
                                         const ConstGenerateNoSizeSeedSeq&>);
  static_assert(
      std::is_constructible_v<
          absl::BitGen, const ConstGenerateNonConstSizeWithParamSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::BitGen, const ConstParamNoSizeSeedSeq&>);

  static_assert(
      std::is_constructible_v<absl::InsecureBitGen, const absl::SeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::InsecureBitGen, const std::seed_seq&>);
  static_assert(!std::is_constructible_v<absl::InsecureBitGen,
                                         const StatefulZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstGenerateZeroSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen, SeedSeq64&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, NarrowResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<absl::InsecureBitGen, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<absl::InsecureBitGen,
                              const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstGenerateNoSizeSeedSeq&>);
  static_assert(std::is_constructible_v<
                absl::InsecureBitGen,
                const ConstGenerateNonConstSizeWithParamSeedSeq&>);
  static_assert(std::is_constructible_v<absl::InsecureBitGen,
                                        const ConstParamNoSizeSeedSeq&>);

  static_assert(std::is_constructible_v<ExampleNonsecureURBG, SeedSeq64&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, NoResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, SignedResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, NarrowResultTypeSeedSeq&>);
  static_assert(
      !std::is_constructible_v<ExampleNonsecureURBG, VoidResultTypeSeedSeq&>);
  static_assert(
      std::is_constructible_v<ExampleNonsecureURBG,
                              const ConstGenerateNonConstSizeSeedSeq&>);
  static_assert(std::is_constructible_v<ExampleNonsecureURBG,
                                        const ConstGenerateNoSizeSeedSeq&>);
}

TEST(NonsecureURBGBase, ConstSaltedSeedSeqMatchesMutable) {
  absl::SeedSeq mutable_lvalue{1, 2, 3, 4};
  absl::SeedSeq mutable_rvalue{1, 2, 3, 4};
  const absl::SeedSeq const_lvalue{1, 2, 3, 4};
  const absl::SeedSeq const_rvalue{1, 2, 3, 4};

  absl::BitGen bitgen_mutable_lvalue(mutable_lvalue);
  absl::BitGen bitgen_mutable_rvalue(std::move(mutable_rvalue));
  absl::BitGen bitgen_const_lvalue(const_lvalue);
  absl::BitGen bitgen_const_rvalue(std::move(const_rvalue));
  const auto expected_bitgen = GenerateSequence(bitgen_mutable_lvalue);
  EXPECT_THAT(GenerateSequence(bitgen_mutable_rvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_lvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_rvalue),
              testing::Eq(expected_bitgen));

  absl::InsecureBitGen insecure_mutable(mutable_lvalue);
  absl::InsecureBitGen insecure_const(const_lvalue);
  EXPECT_THAT(GenerateSequence(insecure_const),
              testing::Eq(GenerateSequence(insecure_mutable)));
}

TEST(NonsecureURBGBase, ConstStdSeedSeqMatchesMutable) {
  std::seed_seq mutable_lvalue{1, 2, 3, 4};
  std::seed_seq mutable_rvalue{1, 2, 3, 4};
  const std::seed_seq const_lvalue{1, 2, 3, 4};
  const std::seed_seq const_rvalue{1, 2, 3, 4};

  absl::BitGen bitgen_mutable_lvalue(mutable_lvalue);
  absl::BitGen bitgen_mutable_rvalue(std::move(mutable_rvalue));
  absl::BitGen bitgen_const_lvalue(const_lvalue);
  absl::BitGen bitgen_const_rvalue(std::move(const_rvalue));
  const auto expected_bitgen = GenerateSequence(bitgen_mutable_lvalue);
  EXPECT_THAT(GenerateSequence(bitgen_mutable_rvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_lvalue),
              testing::Eq(expected_bitgen));
  EXPECT_THAT(GenerateSequence(bitgen_const_rvalue),
              testing::Eq(expected_bitgen));

  absl::InsecureBitGen insecure_mutable(mutable_lvalue);
  absl::InsecureBitGen insecure_const(const_lvalue);
  EXPECT_THAT(GenerateSequence(insecure_const),
              testing::Eq(GenerateSequence(insecure_mutable)));
}

TEST(NonsecureURBGBase, StatefulZeroSizeSeedSeqPreservesEntropy) {
  StatefulZeroSizeSeedSeq seeder;
  absl::BitGen gen1(seeder);
  absl::BitGen gen2(seeder);
  EXPECT_THAT(GenerateSequence(gen1), testing::Ne(GenerateSequence(gen2)));
}

TEST(NonsecureURBGBase, ConstGenerateZeroSizeSeedSeqPreservesEntropy) {
  const ConstGenerateZeroSizeSeedSeq seeder;
  absl::BitGen gen1(seeder);
  absl::BitGen gen2(seeder);
  EXPECT_THAT(GenerateSequence(gen1), testing::Ne(GenerateSequence(gen2)));
}

TEST(NonsecureURBGBase, SeedSequenceWith64BitResultTypeMatches32Bit) {
  SeedSeq64 seq64;
  SeedSeq32 seq32;

  absl::BitGen bitgen64(seq64);
  absl::BitGen bitgen32(seq32);
  EXPECT_THAT(GenerateSequence(bitgen64),
              testing::Eq(GenerateSequence(bitgen32)));

  absl::InsecureBitGen insecure64(seq64);
  absl::InsecureBitGen insecure32(seq32);
  EXPECT_THAT(GenerateSequence(insecure64),
              testing::Eq(GenerateSequence(insecure32)));

  ExampleNonsecureURBG example64(seq64);
  ExampleNonsecureURBG example32(seq32);
  EXPECT_THAT(GenerateSequence(example64),
              testing::Eq(GenerateSequence(example32)));
}

TEST(NonsecureURBGBase, ConstFallbackViaParamMatchesDirectSeeding) {
  std::array<uint32_t, 3> init = {10, 20, 30};
  ConstGenerateNonConstSizeWithParamSeedSeq mutable_with_param(init.begin(),
                                                               init.end());
  const ConstGenerateNonConstSizeWithParamSeedSeq const_with_param(init.begin(),
                                                                   init.end());
  absl::BitGen bitgen_from_mutable_param(mutable_with_param);
  absl::BitGen bitgen_from_const_param(const_with_param);
  EXPECT_THAT(GenerateSequence(bitgen_from_const_param),
              testing::Eq(GenerateSequence(bitgen_from_mutable_param)));
}

TEST(NonsecureURBGBase, NonConstSizeAndNoSizeSeedSeqs) {
  ConstGenerateNonConstSizeSeedSeq mutable_non_const_size;
  absl::BitGen bitgen_mutable(mutable_non_const_size);
  EXPECT_THAT(GenerateSequence(bitgen_mutable),
              testing::Ne(std::array<absl::BitGen::result_type, 16>{}));

  const ConstGenerateNonConstSizeSeedSeq const_non_const_size;
  absl::InsecureBitGen insecure_non_const_size(const_non_const_size);
  ExampleNonsecureURBG example_non_const_size(const_non_const_size);
  EXPECT_THAT(GenerateSequence(insecure_non_const_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_non_const_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));

  const ConstGenerateNoSizeSeedSeq const_no_size;
  absl::InsecureBitGen insecure_no_size(const_no_size);
  ExampleNonsecureURBG example_no_size(const_no_size);
  EXPECT_THAT(GenerateSequence(insecure_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
  EXPECT_THAT(GenerateSequence(example_no_size),
              testing::Ne(std::array<ExampleNonsecureURBG::result_type, 16>{}));

  std::array<uint32_t, 3> init = {10, 20, 30};
  const ConstParamNoSizeSeedSeq const_param_no_size(init.begin(), init.end());
  absl::InsecureBitGen insecure_param_no_size(const_param_no_size);
  EXPECT_THAT(GenerateSequence(insecure_param_no_size),
              testing::Ne(std::array<absl::InsecureBitGen::result_type, 16>{}));
}
