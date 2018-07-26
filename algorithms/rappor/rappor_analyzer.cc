// Copyright 2017 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algorithms/rappor/rappor_analyzer.h"

#include <glog/logging.h>

#include "algorithms/rappor/rappor_encoder.h"
#include "third_party/lossmin/lossmin/losses/inner-product-loss-function.h"
#include "third_party/lossmin/lossmin/minimizers/gradient-evaluator.h"
#include "third_party/lossmin/lossmin/minimizers/parallel-boosting-with-momentum.h"
#include "util/crypto_util/hash.h"
#include "util/log_based_metrics.h"

namespace cobalt {
namespace rappor {

// Stackdriver metric contants
namespace {
const char kAnalyzeFailure[] = "rappor-analyzer-analyze-failure";
}  // namespace

using crypto::byte;

RapporAnalyzer::RapporAnalyzer(const RapporConfig& config,
                               const RapporCandidateList* candidates)
    : bit_counter_(config), config_(bit_counter_.config()) {
  candidate_map_.candidate_list = candidates;
  // candidate_map_.candidate_cohort_maps remains empty for now. It
  // will be populated by BuildCandidateMap.
}

bool RapporAnalyzer::AddObservation(const RapporObservation& obs) {
  VLOG(5) << "RapporAnalyzer::AddObservation() cohort=" << obs.cohort();
  return bit_counter_.AddObservation(obs);
}

grpc::Status RapporAnalyzer::Analyze(
    std::vector<CandidateResult>* results_out) {
  CHECK(results_out);

  // TODO(rudominer) Consider inserting here an analysis of the distribution
  // of the number of Observations over the set of cohorts. The mathematics
  // of our algorithm below assumes that this distribution is uniform. If
  // it is not uniform in practice this may indicate a problem with client-
  // side code and we may wish to take some corrective action.

  auto status = BuildCandidateMap();
  if (!status.ok()) {
    return status;
  }

  // This is the right-hand side vector b from the equation Ax = b that
  // we are estimating. See comments on the declaration of
  // ExtractEstimatedBitCountRatios() for a description of this vector.
  Eigen::VectorXf est_bit_count_ratios;
  status = ExtractEstimatedBitCountRatios(&est_bit_count_ratios);
  if (!status.ok()) {
    return status;
  }

  ///////////////////////////////////////////////////////////////////////////
  // Note(rudominer) The code below is a temporary proof-of-concept.
  // It is not intended to be used for Cobalt production. The goal is to
  // estimate a solution to Ax=b where A is the candidate_matrix_ and b is
  // the est_bit_count_ratios vector. Below we use the
  // ParallelBoostingWithMomentum minimizer from the lossmin library with a
  // LinearRegressionLossFunction. Although this code gives seemingly good
  // results in very simple test situations, I have no confidence that
  // this implementation is correct and I fully expect this code to be
  // rewritten by somebody more expert than me on this topic.
  // The purpose of this code is mostly to act as a starting point
  // and in particular to indicate how the lossmin and Eigen libraries may be
  // integrated into this class.
  //
  // TODO(mironov) Rewrite this code to be what we actually want.
  //
  ///////////////////////////////////////////////////////////////////////////

  // Note(rudominer) The GradientEvaluator constructor takes a
  // const LabelSet& parameter but est_bit_count_ratios is a
  // VectorXf. These types are different:
  // LabelSet = Matrix<float, Dynamic, Dynamic, RowMajor>
  // VectorXf = Matrix<float, Dynamic, 1>
  // These are different in two ways: VectorXf has a static number of columns
  // and VectorXf uses ColumnMajor order.
  // Here we copy est_bit_count_ratios to a label set before passing it to
  // the GradientEvaluator. This works fine. Some notes about this:
  // (1) Eigen defines copy constructors that do the right thing.
  // (2) There is no compilation error if we pass est_bit_count_ratios directly
  //     to the GradientEvaluator constructor. Somehow this works--I'm not sure
  //     why. But doing this leads to some unknown memory corruption which
  //     causes very bad non-deterministic runtime behavior. Be careful not
  //     to do this.
  // (3) We could avoid doing a copy here by just declaring est_bit_count_ratios
  //     as a LabelSet to begin with. But I don't like doing this because it
  //     makes the code less understandable to define a known column vector
  //     as a matrix with a dynamic number of columns in RowMajor order.
  lossmin::LabelSet as_label_set = est_bit_count_ratios;
  lossmin::LinearRegressionLossFunction loss_function;
  lossmin::GradientEvaluator grad_eval(candidate_matrix_, as_label_set,
                                       &loss_function);

  // Set the parameters for the convergence algorithm.
  // l1 and l2 must be >= 0. In order to achieve
  // behavior similar to LASSO, we need l1 > 0. Small positive value of
  // l2 (two or three orders of magnitude smaller than l1) may
  // also be desirable for stability. The value of kConvergenceThreshold should
  // be small but not too small. For single precision (float) it should
  // probably be something between 1e-5 and 1e-7. kLossEpochs and
  // kConvergencepochs should be small positive numbers (smaller than
  // kMaxEpochs).
  // TODO(bazyli) design and implement how the whole algorithm is run, including
  // values of parameters.

  // Scale the penalty terms so that they have the same interpretation for any
  // number of bits and cohorts. This is introduced because lossmin scales the
  // gradient of the unpenalized part of the objective by
  // 1 / candidate_matrix_.rows() == 1 / (num_bits * num_cohorts)
  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();
  const float l1 = 0.5f / (num_bits * num_cohorts);
  const float l2 = 1e-3f / (num_bits * num_cohorts);
  const float kConvergenceThreshold = 1e-6;
  const int kLossEpochs = 5;         // how often record loss
  const int kConvergenceEpochs = 5;  // how often check convergence
  const int kMaxEpochs = 10000;      // maximum number of iterations
  const bool kUseSimpleConvergenceCheck = true;

  lossmin::ParallelBoostingWithMomentum minimizer(l1, l2, grad_eval);
  minimizer.set_convergence_threshold(kConvergenceThreshold);
  minimizer.set_use_simple_convergence_check(kUseSimpleConvergenceCheck);
  minimizer.Setup();

  const int num_candidates = candidate_matrix_.cols();
  // Initialize the weight vector to the constant 1/n vector.
  lossmin::Weights est_candidate_weights =
      lossmin::Weights::Constant(num_candidates, 1.0 / num_candidates);
  std::vector<float> loss_history;
  if (!minimizer.Run(kMaxEpochs, kLossEpochs, kConvergenceEpochs,
                     &est_candidate_weights, &loss_history)) {
    std::string message =
        "ParallelBoostingWithMomentum did not converge after 10,000 epochs.";
    LOG_STACKDRIVER_COUNT_METRIC(ERROR, kAnalyzeFailure) << message;
    return grpc::Status(grpc::INTERNAL, message);
  }

  // Save minimizer data afer run
  minimizer_data_.num_epochs_run = minimizer.num_epochs_run();
  minimizer_data_.converged = minimizer.converged();
  if (!loss_history.empty()) {
    minimizer_data_.final_loss = loss_history.back();
  }
  minimizer_data_.l1 = minimizer.l1();
  minimizer_data_.l2 = minimizer.l2();
  minimizer_data_.convergence_threshold = kConvergenceThreshold;

  results_out->resize(num_candidates);
  for (auto i = 0; i < num_candidates; i++) {
    results_out->at(i).count_estimate =
        est_candidate_weights(i) * bit_counter_.num_observations();
  }

  return grpc::Status::OK;
}

grpc::Status RapporAnalyzer::ExtractEstimatedBitCountRatios(
    Eigen::VectorXf* est_bit_count_ratios) {
  VLOG(5) << "RapporAnalyzer::ExtractEstimatedBitCountRatios()";
  CHECK(est_bit_count_ratios);

  if (!config_->valid()) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Invalid RapporConfig passed to constructor.");
  }

  if (candidate_map_.candidate_list == nullptr ||
      candidate_map_.candidate_list->candidates_size() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Cannot perform RAPPOR analysis because no candidate "
                        "list was specified.");
  }

  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();

  est_bit_count_ratios->resize(num_cohorts * num_bits);

  const std::vector<CohortCounts>& estimated_counts =
      bit_counter_.EstimateCounts();
  CHECK(estimated_counts.size() == num_cohorts);

  int cohort_block_base = 0;
  for (auto& cohort_data : estimated_counts) {
    CHECK(cohort_data.count_estimates.size() == num_bits);
    for (size_t bit_index = 0; bit_index < num_bits; bit_index++) {
      // |bit_index| is an index "from the right".
      size_t bloom_index = num_bits - 1 - bit_index;
      (*est_bit_count_ratios)(cohort_block_base + bloom_index) =
          cohort_data.count_estimates[bit_index] /
          static_cast<double>(cohort_data.num_observations);
    }
    cohort_block_base += num_bits;
  }

  return grpc::Status::OK;
}

grpc::Status RapporAnalyzer::BuildCandidateMap() {
  VLOG(5) << "RapporAnalyzer::BuildCandidateMap()";
  if (!config_->valid()) {
    return grpc::Status(grpc::FAILED_PRECONDITION,
                        "Invalid RapporConfig passed to constructor.");
  }

  if (candidate_map_.candidate_list == nullptr ||
      candidate_map_.candidate_list->candidates_size() == 0) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Cannot perform RAPPOR analysis because no candidate "
                        "list was specified.");
  }

  // TODO(rudominer) We should cache candidate_matrix_ rather than recomputing
  // candidate_map_ and candidate_matrix_ each time.

  const uint32_t num_bits = config_->num_bits();
  const uint32_t num_cohorts = config_->num_cohorts();
  const uint32_t num_hashes = config_->num_hashes();
  const uint32_t num_candidates =
      candidate_map_.candidate_list->candidates_size();

  if (VLOG_IS_ON(4)) {
    VLOG(4) << "RapporAnalyzer: Start list of " << num_candidates
            << " candidates:";
    for (const std::string& candidate :
         candidate_map_.candidate_list->candidates()) {
      VLOG(4) << "RapporAnalyzer: candidate: " << candidate;
    }
    VLOG(4) << "RapporAnalyzer: End list of " << num_candidates
            << " candidates.";
  }

  candidate_matrix_.resize(num_cohorts * num_bits, num_candidates);
  std::vector<Eigen::Triplet<float>> sparse_matrix_triplets;
  sparse_matrix_triplets.reserve(num_candidates * num_cohorts * num_hashes);

  int column = 0;
  for (const std::string& candidate :
       candidate_map_.candidate_list->candidates()) {
    // In rappor_encoder.cc it is not std::strings that are encoded but rather
    // |ValuePart|s. So here we want to take the candidate as a string and
    // convert it into a serialized |ValuePart|.
    ValuePart candidate_as_value_part;
    candidate_as_value_part.set_string_value(candidate);
    std::string serialized_candidate;
    candidate_as_value_part.SerializeToString(&serialized_candidate);

    // Append a CohortMap for this candidate.
    candidate_map_.candidate_cohort_maps.emplace_back();
    CohortMap& cohort_map = candidate_map_.candidate_cohort_maps.back();

    // Iterate through the cohorts.
    int row_block_base = 0;
    for (size_t cohort = 0; cohort < num_cohorts; cohort++) {
      // Append an instance of |Hashes| for this cohort.
      cohort_map.cohort_hashes.emplace_back();
      Hashes& hashes = cohort_map.cohort_hashes.back();

      // Form one big hashed value of the serialized_candidate. This will be
      // used to obtain multiple bit indices.
      byte hashed_value[crypto::hash::DIGEST_SIZE];
      if (!RapporEncoder::HashValueAndCohort(serialized_candidate, cohort,
                                             num_hashes, hashed_value)) {
        return grpc::Status(grpc::INTERNAL,
                            "Hash operation failed unexpectedly.");
      }

      // bloom_filter is indexed "from the left". That is bloom_filter[0]
      // corresponds to the most significant bit of the first byte of the
      // Bloom filter.
      std::vector<bool> bloom_filter(num_bits, false);

      // Extract one bit index for each of the hashes in the Bloom filter.
      for (size_t hash_index = 0; hash_index < num_hashes; hash_index++) {
        uint32_t bit_index =
            RapporEncoder::ExtractBitIndex(hashed_value, hash_index, num_bits);
        hashes.bit_indices.push_back(bit_index);
        // |bit_index| is an index "from the right".
        bloom_filter[num_bits - 1 - bit_index] = true;
      }

      // Add triplets to the sparse matrix representation. For the current
      // column and the current block of rows we add a 1 into the row
      // corresponding to the index of each set bit in the Bloom filter.
      for (size_t bloom_index = 0; bloom_index < num_bits; bloom_index++) {
        if (bloom_filter[bloom_index]) {
          int row = row_block_base + bloom_index;
          sparse_matrix_triplets.emplace_back(row, column, 1.0);
        }
      }

      // In our sparse matrix representation each cohort corresponds to a block
      // of |num_bits| rows.
      row_block_base += num_bits;
    }
    // In our sparse matrix representation a column corresponds to a candidate.
    column++;
    row_block_base = 0;
  }

  candidate_matrix_.setFromTriplets(sparse_matrix_triplets.begin(),
                                    sparse_matrix_triplets.end());

  return grpc::Status::OK;
}

}  // namespace rappor
}  // namespace cobalt

/*

Justification for the formula used in ExtractEstimatedBitCountRatios
-------------------------------------------------------------------
See the comments at the declaration of the method
ExtractEstimatedBitCountRatios() in rappor_analyzer.h for the context and
the definitions of the symbols used here.

Here we justify the use of the formula

     est_bit_count_ratios[i*k +j] = est_count_i_j / n_i.

Let A be the binary sparse matrix produced by the method BuildCandidateMap()
and stored in candidate_matrix_. Let b be the column vector produced by
the method ExtractEstimatedBitCountRatios() and stored in the variable
est_bit_count_ratios.  In RapporAnalyzer::Analyze() we compute an estimate
of a solution to the equation Ax = b. The question we want to address here
is how do we know we are using the correct value of b? In particular, why is it
appropriate to divide each entry by n_i, the number of observations from
cohort i?

The assumption that underlies the justifcation is that the probability of
a given candidate string occurring is the same in each cohort. That is, there
is a probability distribution vector x_0 of length s = # of candidates such
that for each cohort i < m, and each candidate index r < s,
x_0[r] =
   (number of true observations of candidate r in cohort i) /
        (number of observations from cohort i)

Assume such an x_0 exists. Now let n_i = (number of observations from cohort i).
Then consider the vector b_i = A (n_i) x_0. We are only concerned with the
entries in b_i corresponding to cohort i, that is the entries
i*k + j for 0 <= j < k. Fix such a j and note that
b_i[i*k + j] = "the true count of 1's for bit j in cohort i". That is, the
count of 1's for bit j in cohort i prior to flipping bits for randomized
response. In other words, the count of 1's if we use p = 0, q = 1.

Dividing both sides of the equation A (n_i) x_0 = b_i by n_i and focusing
only on cohort i we get
     A x_0 [i*k + j] = "the true count of 1's for bit j in cohort i" / n_i

Let b* = A x_0. Then we have:

(i) x_0 is a solution to the equation Ax = b*
(ii) b*[i*k + j] = "the true count of 1's for bit j in cohort i" / n_i

This justifies our use of the vector b. We have
 b[i*k + j] = "the estimated count of 1's for bit j in cohort i" / n_i

 and we seek an estimate to an x such that Ax = b. Such an x may therefore
 naturally be considered to be an estimate of x_0.

*/
